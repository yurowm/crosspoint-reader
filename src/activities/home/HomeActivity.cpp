#include "HomeActivity.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>
#include <Utf8.h>
#include <Xtc.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "LibraryIndex.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "ReadingStats.h"
#include "RecentBooksStore.h"
#include "activities/reader/ReadingStatsActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/BookPageEstimator.h"

namespace {
uint16_t readLe16(const uint8_t* data) { return static_cast<uint16_t>(data[0] | (data[1] << 8)); }

uint32_t readLe32(const uint8_t* data) {
  return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
         (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
}

std::string displaySeriesIndex(std::string index) {
  const size_t decimalPoint = index.find('.');
  if (decimalPoint != std::string::npos && decimalPoint + 1 < index.size() &&
      index.find_first_not_of('0', decimalPoint + 1) == std::string::npos) {
    index.erase(decimalPoint);
  }
  return index;
}

struct PageCountLookup {
  const std::string& path;
  uint32_t charactersPerPage;
  uint32_t pageCount = 0;
};

bool findPageCount(const LibraryBook& book, void* context) {
  auto& lookup = *static_cast<PageCountLookup*>(context);
  if (book.path != lookup.path) return true;
  lookup.pageCount = BookPageEstimator::pageCount(book.visibleCharacterCount, lookup.charactersPerPage);
  return false;
}

uint32_t estimatedPageCount(const GfxRenderer& renderer, const std::string& path) {
  PageCountLookup lookup{path, BookPageEstimator::charactersPerPage(renderer)};
  LibraryIndex::visitBooks(findPageCount, &lookup);
  return lookup.pageCount;
}

bool readEpubProgress(const Epub& epub, uint16_t& progressBasisPoints) {
  HalFile file;
  if (!Storage.openFileForRead("HOME", epub.getCachePath() + "/progress.bin", file)) {
    return false;
  }

  std::array<uint8_t, 6> data{};
  const bool valid = file.read(data.data(), data.size()) == static_cast<int>(data.size());
  file.close();
  if (!valid || epub.getSpineItemsCount() <= 0) {
    return false;
  }

  const int spineCount = epub.getSpineItemsCount();
  const int savedSpineIndex = readLe16(data.data());
  const int currentPage = readLe16(data.data() + 2);
  const int pageCount = readLe16(data.data() + 4);
  if (savedSpineIndex >= spineCount) {
    progressBasisPoints = ReadingStats::PROGRESS_COMPLETE;
    return true;
  }

  const int spineIndex = std::clamp(savedSpineIndex, 0, spineCount - 1);
  const float chapterProgress = pageCount > 0 ? static_cast<float>(currentPage) / pageCount : 0.0f;
  const int basisPoints =
      static_cast<int>(epub.calculateProgress(spineIndex, chapterProgress) * ReadingStats::PROGRESS_COMPLETE + 0.5f);
  progressBasisPoints = static_cast<uint16_t>(std::clamp(basisPoints, 0, 9999));
  return true;
}

bool readXtcProgress(const Xtc& xtc, uint16_t& progressBasisPoints) {
  HalFile file;
  if (!Storage.openFileForRead("HOME", xtc.getCachePath() + "/progress.bin", file)) {
    return false;
  }

  std::array<uint8_t, 4> data{};
  const bool valid = file.read(data.data(), data.size()) == static_cast<int>(data.size());
  file.close();
  if (!valid || xtc.getPageCount() == 0) {
    return false;
  }

  const uint32_t page = std::min(readLe32(data.data()), xtc.getPageCount() - 1);
  progressBasisPoints = static_cast<uint16_t>(
      std::min<uint64_t>(((static_cast<uint64_t>(page) + 1) * ReadingStats::PROGRESS_COMPLETE) / xtc.getPageCount(),
                         ReadingStats::PROGRESS_COMPLETE));
  return true;
}

ButtonHint homeButtonHint(const MappedInputManager::NavigationAction action) {
  switch (action) {
    case MappedInputManager::NavigationAction::Confirm:
      return {.icon = Check};
    case MappedInputManager::NavigationAction::Previous:
      return {.icon = ChevronUp};
    case MappedInputManager::NavigationAction::Next:
      return {.icon = ChevronDown};
    default:
      return {};
  }
}

}  // namespace

int HomeActivity::getMenuItemCount() const {
  int count = 5;  // Library, Deferred, File transfer, Statistics, Settings
  if (!recentBooks.empty()) {
    count++;  // Continue Reading
  }
  if (hasOpdsServers) {
    count++;
  }
  return count;
}

void HomeActivity::loadRecentBooks(int maxBooks) {
  recentBooks.clear();
  const auto& books = RECENT_BOOKS.getBooks();
  recentBooks.reserve(std::min(static_cast<int>(books.size()), maxBooks));

  for (const RecentBook& book : books) {
    // Limit to maximum number of recent books
    if (recentBooks.size() >= maxBooks) {
      break;
    }

    // Skip if file no longer exists
    if (RecentBooksStore::isMissing(book)) {
      continue;
    }

    recentBooks.push_back(book);
  }
}

void HomeActivity::loadRecentCovers(int coverHeight) {
  recentsLoading = true;
  bool showingLoading = false;
  Rect popupRect;

  int progress = 0;
  for (RecentBook& book : recentBooks) {
    if (!book.coverBmpPath.empty()) {
      std::string coverPath = UITheme::getCoverThumbPath(book.coverBmpPath, coverHeight);
      if (!Storage.exists(coverPath.c_str())) {
        // If epub, try to load the metadata for title/author and cover
        if (FsHelpers::hasEpubExtension(book.path)) {
          Epub epub(book.path, "/.crosspoint");
          // Skip loading css since we only need metadata here
          epub.load(false, true);

          // Try to generate thumbnail image for Continue Reading card
          if (!showingLoading) {
            showingLoading = true;
            popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
          }
          GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
          bool success = epub.generateThumbBmp(coverHeight);
          if (!success) {
            RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
            book.coverBmpPath = "";
          }
          freeCoverBuffer();
          coverRendered = false;
          requestUpdate();
        } else if (FsHelpers::hasXtcExtension(book.path)) {
          // Handle XTC file
          Xtc xtc(book.path, "/.crosspoint");
          if (xtc.load()) {
            // Try to generate thumbnail image for Continue Reading card
            if (!showingLoading) {
              showingLoading = true;
              popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
            }
            GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
            bool success = xtc.generateThumbBmp(coverHeight);
            if (!success) {
              RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
              book.coverBmpPath = "";
            }
            freeCoverBuffer();
            coverRendered = false;
            requestUpdate();
          }
        }
      }
    }
    progress++;
  }

  recentsLoaded = true;
  recentsLoading = false;
}

void HomeActivity::loadCurrentBookDetails() {
  if (recentBooks.empty()) {
    return;
  }

  RecentBook& book = recentBooks.front();
  if (FsHelpers::hasEpubExtension(book.path)) {
    Epub epub(book.path, "/.crosspoint");
    if (!epub.load(false, true)) {
      return;
    }
    if (!epub.getTitle().empty()) book.title = epub.getTitle();
    if (!epub.getAuthor().empty()) book.author = epub.getAuthor();
    book.series = epub.getSeries();
    if (!epub.getSeriesIndex().empty()) {
      if (!book.series.empty()) book.series += " · ";
      book.series += displaySeriesIndex(epub.getSeriesIndex());
    }
    book.started = readEpubProgress(epub, book.progressBasisPoints);
  } else if (FsHelpers::hasXtcExtension(book.path)) {
    Xtc xtc(book.path, "/.crosspoint");
    if (!xtc.load()) {
      return;
    }
    if (!xtc.getTitle().empty()) book.title = xtc.getTitle();
    if (!xtc.getAuthor().empty()) book.author = xtc.getAuthor();
    book.started = readXtcProgress(xtc, book.progressBasisPoints);
  }

  if (book.started) {
    book.progressPercent = book.progressBasisPoints >= ReadingStats::PROGRESS_COMPLETE
                               ? 100
                               : static_cast<uint8_t>(std::min<uint16_t>((book.progressBasisPoints + 50) / 100, 99));
    ReadingStatsData stats;
    if (ReadingStats::loadBook(book.path, stats)) {
      book.remainingReadingSeconds =
          ReadingStats::remainingSeconds(stats, book.progressBasisPoints, estimatedPageCount(renderer, book.path));
    }
  }
}

void HomeActivity::onEnter() {
  Activity::onEnter();

  hasOpdsServers = OPDS_STORE.hasServers();

  const auto& metrics = UITheme::getInstance().getMetrics();
  loadRecentBooks(metrics.homeRecentBooksCount);
  loadCurrentBookDetails();

  const int continueOffset = recentBooks.empty() ? 0 : 1;
  selectorIndex =
      initialMenuItem == HomeMenuItem::NONE ? 0 : continueOffset + menuItemToIndex(initialMenuItem, hasOpdsServers);

  // Trigger first update
  requestUpdate();
}

void HomeActivity::onExit() {
  Activity::onExit();

  // Free the stored cover buffer if any
  freeCoverBuffer();
}

bool HomeActivity::storeCoverBuffer() {
  // render() must have already set the cover rect; without it we'd be back to
  // cloning the whole framebuffer.
  if (coverRectW <= 0 || coverRectH <= 0) return false;
  freeCoverBuffer();
  const size_t needed = renderer.getRegionByteSize(coverRectX, coverRectY, coverRectW, coverRectH);
  if (needed == 0) return false;
  coverBuffer = static_cast<uint8_t*>(malloc(needed));
  if (!coverBuffer) {
    LOG_ERR("HOME", "OOM: cover buffer (%u bytes)", (unsigned)needed);
    return false;
  }
  coverBufferSize = needed;
  if (!renderer.copyRegionToBuffer(coverRectX, coverRectY, coverRectW, coverRectH, coverBuffer, coverBufferSize)) {
    free(coverBuffer);
    coverBuffer = nullptr;
    coverBufferSize = 0;
    return false;
  }
  return true;
}

bool HomeActivity::restoreCoverBuffer() {
  if (!coverBuffer || coverRectW <= 0 || coverRectH <= 0) return false;
  return renderer.copyBufferToRegion(coverRectX, coverRectY, coverRectW, coverRectH, coverBuffer, coverBufferSize);
}

void HomeActivity::freeCoverBuffer() {
  if (coverBuffer) {
    free(coverBuffer);
    coverBuffer = nullptr;
  }
  coverBufferSize = 0;
  coverBufferStored = false;
}

void HomeActivity::loop() {
  const int menuCount = getMenuItemCount();
  const auto& metrics = UITheme::getInstance().getMetrics();

  auto activateSelection = [this] {
    if (!recentBooks.empty() && selectorIndex == 0) {
      onSelectBook(recentBooks.front().path);
      return;
    }
    const int menuIndex = selectorIndex - (recentBooks.empty() ? 0 : 1);
    switch (indexToMenuItem(menuIndex, hasOpdsServers)) {
      case HomeMenuItem::LIBRARY:
        onLibraryOpen();
        break;
      case HomeMenuItem::DEFERRED:
        onDeferredOpen();
        break;
      case HomeMenuItem::OPDS_BROWSER:
        onOpdsBrowserOpen();
        break;
      case HomeMenuItem::FILE_TRANSFER:
        onFileTransferOpen();
        break;
      case HomeMenuItem::STATISTICS:
        onStatisticsOpen();
        break;
      case HomeMenuItem::SETTINGS_MENU:
        onSettingsOpen();
        break;
      default:
        break;
    }
  };

  buttonNavigator.onNext([this, menuCount] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
    requestUpdate();
  });

  buttonNavigator.onPrevious([this, menuCount] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
    requestUpdate();
  });

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
    requestUpdate();
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
    requestUpdate();
    return;
  }

  // Back is otherwise unused on the home menu: open the most recently read
  // book directly (recentBooks is most-recent-first and already pruned of
  // files missing from the SD card).
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) && !recentBooks.empty()) {
    onSelectBook(recentBooks[0].path);
    return;
  }

  // The customized Home presents one current book regardless of how many
  // covers a theme can render, so the complete cover tile activates Continue.
  const int recentCount = recentBooks.empty() ? 0 : 1;
  const int coverColumnWidth = renderer.getScreenWidth();
  int touchedBook = -1;
  const auto coverTouch = mappedInput.colTouch(touchedBook, 0, coverColumnWidth, recentCount, metrics.homeTopPadding,
                                               metrics.homeTopPadding + metrics.homeCoverTileHeight, coverColumnWidth);
  if (coverTouch != MappedInputManager::RowTouch::None) {
    if (coverTouch == MappedInputManager::RowTouch::Down) {
      if (selectorIndex != 0) {
        selectorIndex = 0;
        requestUpdate();
      }
    } else {
      selectorIndex = 0;
      activateSelection();
    }
    return;
  }

  const int menuTop = metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.homeMenuTopOffset;
  int menuRow = -1;
  // Row height from the theme, not the metrics table: RoundedRaff draws
  // font-derived rows and the touch grid must match the visuals exactly.
  const int menuRowHeight = GUI.getMenuRowHeight(renderer);
  const auto menuTouch = mappedInput.rowTouch(menuRow, menuTop, menuRowHeight + metrics.menuSpacing, menuCount, 0,
                                              INT32_MAX, menuRowHeight);
  if (menuTouch != MappedInputManager::RowTouch::None) {
    const int touchedIndex = menuRow;
    if (menuTouch == MappedInputManager::RowTouch::Down) {
      if (selectorIndex != touchedIndex) {
        selectorIndex = touchedIndex;
        requestUpdate();
      }
    } else {
      selectorIndex = touchedIndex;
      activateSelection();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateSelection();
  }
}

void HomeActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  bool bufferRestored = coverBufferStored && restoreCoverBuffer();

  // Band spans topPadding..homeTopPadding: the cover tile starts at the fixed
  // homeTopPadding, so the height must shrink by topPadding. The current-book
  // title is rendered in the card's metadata block, not in the header.
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding - metrics.topPadding},
                 nullptr);

  // Record the tile rect so storeCoverBuffer (called from the theme) knows
  // which sub-region of the framebuffer to snapshot. ~16 KB in Portrait
  // instead of the 48 KB full framebuffer the previous bind captured.
  coverRectX = 0;
  coverRectY = metrics.homeTopPadding;
  coverRectW = pageWidth;
  coverRectH = metrics.homeCoverTileHeight;

  GUI.drawRecentBookCover(renderer, Rect{0, metrics.homeTopPadding, pageWidth, metrics.homeCoverTileHeight},
                          recentBooks, selectorIndex, coverRendered, coverBufferStored, bufferRestored,
                          std::bind(&HomeActivity::storeCoverBuffer, this));

  // Build menu items dynamically
  std::vector<const char*> menuItems = {tr(STR_LIBRARY), tr(STR_DEFERRED_BOOKS), tr(STR_FILE_TRANSFER),
                                        tr(STR_READING_STATS), tr(STR_SETTINGS_TITLE)};
  std::vector<UIIcon> menuIcons = {Library, Deferred, Transfer, Statistics, Settings};

  if (hasOpdsServers) {
    menuItems.insert(menuItems.begin() + 2, tr(STR_OPDS_BROWSER));
    menuIcons.insert(menuIcons.begin() + 2, Library);
  }

  if (!recentBooks.empty()) {
    menuItems.insert(menuItems.begin(), tr(STR_CONTINUE_READING));
    menuIcons.insert(menuIcons.begin(), ContinueReading);
  }

  GUI.drawButtonMenu(
      renderer,
      Rect{0, metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.homeMenuTopOffset, pageWidth,
           pageHeight - (metrics.headerHeight + metrics.homeTopPadding + metrics.verticalSpacing +
                         metrics.homeMenuTopOffset + metrics.buttonHintsHeight)},
      static_cast<int>(menuItems.size()), selectorIndex,
      [&menuItems](int index) { return std::string(menuItems[index]); },
      [&menuIcons](int index) { return menuIcons[index]; });

  const auto actions = mappedInput.mapNavigationActions();
  GUI.drawIconButtonHints(renderer, homeButtonHint(actions.btn1), homeButtonHint(actions.btn2),
                          homeButtonHint(actions.btn3), homeButtonHint(actions.btn4));
  renderer.displayBuffer(cleanInitialRefresh && !firstRenderDone ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);

  if (!firstRenderDone) {
    firstRenderDone = true;
    requestUpdate();
  } else if (!recentsLoaded && !recentsLoading) {
    recentsLoading = true;
    loadRecentCovers(metrics.homeCoverHeight);
  }
}

void HomeActivity::onSelectBook(const std::string& path) { activityManager.goToReader(path); }

void HomeActivity::onLibraryOpen() { activityManager.goToLibrary(); }

void HomeActivity::onDeferredOpen() { activityManager.goToDeferredBooks(); }

void HomeActivity::onSettingsOpen() { activityManager.goToSettings(); }

void HomeActivity::onFileTransferOpen() { activityManager.goToFileTransfer(); }

void HomeActivity::onStatisticsOpen() {
  auto stats = makeUniqueNoThrow<ReadingStatsActivity>(renderer, mappedInput);
  if (!stats) {
    LOG_ERR("HOME", "OOM: ReadingStatsActivity");
    return;
  }
  startActivityForResult(std::move(stats), nullptr);
}

void HomeActivity::onOpdsBrowserOpen() { activityManager.goToBrowser(); }
