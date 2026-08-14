#include "LibraryActivity.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <Xtc.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <unordered_map>

#include "LibraryFiltersActivity.h"
#include "MappedInputManager.h"
#include "components/BookListItem.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/BookCacheUtils.h"

namespace {
constexpr int DIRECTORY_STACK_RESERVE = 16;
constexpr int FILE_NAME_BUFFER_SIZE = 500;
constexpr size_t SCAN_PROGRESS_UPDATES = 10;
constexpr unsigned long FILTER_HOLD_MS = 1000;
constexpr unsigned long NAVIGATION_REPEAT_START_MS = 500;
constexpr unsigned long NAVIGATION_REPEAT_INTERVAL_MS = 500;

bool isBookFile(const std::string& path) {
  return FsHelpers::hasEpubExtension(path) || FsHelpers::hasXtcExtension(path) || FsHelpers::hasTxtExtension(path) ||
         FsHelpers::hasMarkdownExtension(path);
}

uint16_t readLe16(const uint8_t* data) { return static_cast<uint16_t>(data[0] | (data[1] << 8)); }

uint32_t readLe32(const uint8_t* data) {
  return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
         (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
}

bool matchesSelection(const std::vector<std::string>& values, const std::set<std::string>& selected) {
  if (selected.empty()) {
    return true;
  }
  return std::any_of(values.begin(), values.end(),
                     [&selected](const std::string& value) { return selected.contains(value); });
}

ButtonHint libraryButtonHint(const MappedInputManager::NavigationAction action) {
  switch (action) {
    case MappedInputManager::NavigationAction::Back:
      return {.icon = House, .holdIcon = Filters};
    case MappedInputManager::NavigationAction::Confirm:
      return {.icon = Check};
    case MappedInputManager::NavigationAction::Previous:
      return {.icon = ChevronLeft};
    case MappedInputManager::NavigationAction::Next:
      return {.icon = ChevronRight};
    default:
      return {};
  }
}

}  // namespace

std::string LibraryActivity::filenameStem(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  const size_t start = slash == std::string::npos ? 0 : slash + 1;
  const size_t dot = path.find_last_of('.');
  if (dot == std::string::npos || dot <= start) {
    return path.substr(start);
  }
  return path.substr(start, dot - start);
}

bool LibraryActivity::readEpubProgress(const Epub& epub, uint8_t& progressPercent) {
  HalFile file;
  if (!Storage.openFileForRead("LIB", epub.getCachePath() + "/progress.bin", file)) {
    return false;
  }

  std::array<uint8_t, 6> data{};
  const bool valid = file.read(data.data(), data.size()) == static_cast<int>(data.size());
  file.close();
  if (!valid) {
    return false;
  }

  const int spineIndex = readLe16(data.data());
  const int currentPage = readLe16(data.data() + 2);
  const int pageCount = readLe16(data.data() + 4);
  const int spineCount = epub.getSpineItemsCount();
  if (spineCount <= 0) {
    return false;
  }

  const int clampedSpine = std::clamp(spineIndex, 0, spineCount - 1);
  const float chapterProgress = pageCount > 0 ? static_cast<float>(currentPage) / pageCount : 0.0f;
  const int percent = static_cast<int>(epub.calculateProgress(clampedSpine, chapterProgress) * 100.0f + 0.5f);
  progressPercent = static_cast<uint8_t>(std::clamp(percent, 0, 100));
  return true;
}

bool LibraryActivity::readXtcProgress(const Xtc& xtc, uint8_t& progressPercent) {
  HalFile file;
  if (!Storage.openFileForRead("LIB", xtc.getCachePath() + "/progress.bin", file)) {
    return false;
  }

  std::array<uint8_t, 4> data{};
  const bool valid = file.read(data.data(), data.size()) == static_cast<int>(data.size());
  file.close();
  if (!valid || xtc.getPageCount() == 0) {
    return false;
  }

  const uint32_t page = std::min(readLe32(data.data()), xtc.getPageCount() - 1);
  progressPercent = xtc.calculateProgress(page);
  return true;
}

LibraryBook LibraryActivity::loadBook(const LibraryFileInfo& file) {
  LibraryBook book;
  book.path = file.path;
  book.fileSize = file.fileSize;
  book.modifiedDate = file.modifiedDate;
  book.modifiedTime = file.modifiedTime;
  book.title = filenameStem(file.path);

  if (FsHelpers::hasEpubExtension(file.path)) {
    Epub epub(file.path, "/.crosspoint");
    const bool hasProgress = Storage.exists((epub.getCachePath() + "/progress.bin").c_str());
    // Started books need cumulative spine sizes to convert their saved chapter
    // position to a whole-book percentage. Everything else takes the metadata-
    // only path and skips the spine/TOC pass entirely.
    const bool loadedForProgress = hasProgress && epub.load(/*buildIfMissing=*/false, /*skipLoadingCss=*/true);
    if (loadedForProgress || epub.loadMetadata()) {
      if (!epub.getTitle().empty()) book.title = epub.getTitle();
      book.author = epub.getAuthor();
      book.authors = epub.getAuthors();
      if (book.authors.empty() && !book.author.empty()) {
        book.authors.push_back(book.author);
      }
      book.series = epub.getSeries();
      book.seriesIndex = epub.getSeriesIndex();
      book.tags = epub.getSubjects();
      const std::string thumb = epub.getThumbBmpPath(BookListItem::DEFAULT_COVER_CACHE_HEIGHT);
      if (Storage.exists(thumb.c_str())) {
        book.coverBmpPath = thumb;
      }
      if (loadedForProgress) {
        book.started = readEpubProgress(epub, book.progressPercent);
      }
    }
  } else if (FsHelpers::hasXtcExtension(file.path)) {
    Xtc xtc(file.path, "/.crosspoint");
    if (xtc.load()) {
      if (!xtc.getTitle().empty()) book.title = xtc.getTitle();
      book.author = xtc.getAuthor();
      const std::string thumb = xtc.getThumbBmpPath(BookListItem::DEFAULT_COVER_CACHE_HEIGHT);
      if (Storage.exists(thumb.c_str())) {
        book.coverBmpPath = thumb;
      }
      book.started = readXtcProgress(xtc, book.progressPercent);
    }
  }

  return book;
}

bool LibraryActivity::scanDirectory(const std::string& path, std::vector<LibraryFileInfo>& bookFiles) {
  std::vector<std::string> directories;
  directories.reserve(DIRECTORY_STACK_RESERVE);
  directories.push_back(path);
  char fileName[FILE_NAME_BUFFER_SIZE] = {};
  bool complete = true;

  while (!directories.empty()) {
    std::string directory = std::move(directories.back());
    directories.pop_back();

    HalFile root = Storage.open(directory.c_str());
    if (!root || !root.isDirectory()) {
      complete = false;
      continue;
    }
    root.rewindDirectory();

    for (HalFile entry = root.openNextFile(); entry; entry = root.openNextFile()) {
      entry.getName(fileName, sizeof(fileName));
      const bool isDirectory = entry.isDirectory();
      const uint64_t fileSize = isDirectory ? 0 : entry.fileSize64();
      uint16_t modifiedDate = 0;
      uint16_t modifiedTime = 0;
      if (!isDirectory) entry.getModifyDateTime(modifiedDate, modifiedTime);
      entry.close();

      // Internal caches and host-created system folders are not books.
      if (fileName[0] == '.' || strcmp(fileName, "System Volume Information") == 0) {
        continue;
      }

      std::string fullPath = directory;
      if (fullPath.back() != '/') {
        fullPath += '/';
      }
      fullPath += fileName;

      if (isDirectory) {
        directories.push_back(std::move(fullPath));
      } else if (isBookFile(fullPath)) {
        bookFiles.push_back({std::move(fullPath), fileSize, modifiedDate, modifiedTime});
      }
    }
    root.close();
  }
  return complete;
}

bool LibraryActivity::scanLibrary(const bool indexLoaded) {
  scannedBookCount = 0;
  totalBookCount = 0;

  std::vector<LibraryFileInfo> bookFiles;
  if (!scanDirectory("/", bookFiles)) {
    LOG_ERR("LIB", "Library directory scan was incomplete; keeping the previous index");
    return !indexLoaded;
  }

  std::vector<LibraryBook> cachedBooks = std::move(books);
  std::unordered_map<std::string, size_t> cachedByPath;
  cachedByPath.reserve(cachedBooks.size());
  for (size_t i = 0; i < cachedBooks.size(); i++) {
    cachedByPath.emplace(cachedBooks[i].path, i);
  }

  std::vector<LibraryBook> updatedBooks;
  updatedBooks.reserve(bookFiles.size());
  std::vector<LibraryFileInfo> changedFiles;
  changedFiles.reserve(bookFiles.size());
  for (const auto& file : bookFiles) {
    const auto cached = cachedByPath.find(file.path);
    if (cached != cachedByPath.end() && LibraryIndex::sourceMatches(cachedBooks[cached->second], file)) {
      updatedBooks.push_back(std::move(cachedBooks[cached->second]));
    } else {
      changedFiles.push_back(file);
    }
  }

  const bool removedBooks = updatedBooks.size() + changedFiles.size() != cachedBooks.size();
  totalBookCount = changedFiles.size();

  if (!changedFiles.empty()) {
    // The cached list was already painted on entry. Only replace it with the
    // progress screen when there is actual metadata work to perform.
    scanning = true;
    requestUpdateAndWait();
  }

  const size_t updateInterval =
      std::max<size_t>(1, (totalBookCount + SCAN_PROGRESS_UPDATES - 1) / SCAN_PROGRESS_UPDATES);
  for (const auto& file : changedFiles) {
    if (cachedByPath.contains(file.path)) {
      // The source changed in place, so its path-keyed reader cache and saved
      // progress refer to the old bytes and must not be reused.
      clearBookCache(file.path);
    }
    updatedBooks.push_back(loadBook(file));
    scannedBookCount++;

    // Full e-ink refreshes are deliberately limited to about ten per scan.
    if (scannedBookCount % updateInterval == 0 || scannedBookCount == totalBookCount) {
      requestUpdateAndWait();
    }
  }

  books = std::move(updatedBooks);
  sortBooks();
  restoreSelector();

  const bool libraryChanged = !indexLoaded || !changedFiles.empty() || removedBooks;
  if (libraryChanged) {
    indexDirty = !LibraryIndex::save(books);
  }
  return libraryChanged;
}

void LibraryActivity::onEnter() {
  Activity::onEnter();
  selectorIndex = 0;
  LibraryViewStateFile::load(viewState);
  lockNextConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  indexDirty = false;

  // A valid index paints the complete library immediately. The directory pass
  // that follows only opens book metadata for new or changed source files.
  const bool indexLoaded = LibraryIndex::load(books);
  sortBooks();
  restoreSelector();
  scanning = !indexLoaded;
  requestUpdateAndWait();
  const bool libraryChanged = scanLibrary(indexLoaded);
  scanning = false;
  if (libraryChanged) requestUpdate();
}

void LibraryActivity::onExit() {
  Activity::onExit();
  rememberSelectedBook();
  if (viewState.dirty && !LibraryViewStateFile::save(viewState)) {
    LOG_ERR("LIB", "Failed to persist library view state");
  }
  if (indexDirty && !LibraryIndex::save(books)) {
    LOG_ERR("LIB", "Failed to persist lazy cover updates");
  }
  books.clear();
}

LibraryActivity::CoverAttemptResult LibraryActivity::ensureNextVisibleCover() {
  const auto bookIndices = filteredBookIndices();
  if (bookIndices.empty()) return CoverAttemptResult::None;

  const size_t pageStart = selectorIndex / BookListItem::ITEMS_PER_PAGE * BookListItem::ITEMS_PER_PAGE;
  const size_t pageEnd = std::min(bookIndices.size(), pageStart + BookListItem::ITEMS_PER_PAGE);
  bool attemptedAny = false;
  for (size_t index = pageStart; index < pageEnd; index++) {
    LibraryBook& book = books[bookIndices[index]];
    if (book.coverAttempted) continue;
    book.coverAttempted = true;
    attemptedAny = true;

    if (!book.coverBmpPath.empty()) {
      const std::string thumbPath =
          UITheme::getCoverThumbPath(book.coverBmpPath, BookListItem::DEFAULT_COVER_CACHE_HEIGHT);
      if (thumbPath != book.coverBmpPath) {
        book.coverBmpPath = thumbPath;
        indexDirty = true;
      }
    }

    if (!book.coverBmpPath.empty() && Storage.exists(book.coverBmpPath.c_str())) {
      continue;
    }
    book.coverBmpPath.clear();

    if (FsHelpers::hasEpubExtension(book.path)) {
      Epub epub(book.path, "/.crosspoint");
      if (epub.loadMetadata()) {
        const std::string thumb = epub.getThumbBmpPath(BookListItem::DEFAULT_COVER_CACHE_HEIGHT);
        if (Storage.exists(thumb.c_str()) || epub.generateThumbBmp(BookListItem::DEFAULT_COVER_CACHE_HEIGHT)) {
          book.coverBmpPath = thumb;
          indexDirty = true;
          return CoverAttemptResult::Updated;
        }
      }
    } else if (FsHelpers::hasXtcExtension(book.path)) {
      Xtc xtc(book.path, "/.crosspoint");
      if (xtc.load()) {
        const std::string thumb = xtc.getThumbBmpPath(BookListItem::DEFAULT_COVER_CACHE_HEIGHT);
        if (Storage.exists(thumb.c_str()) || xtc.generateThumbBmp(BookListItem::DEFAULT_COVER_CACHE_HEIGHT)) {
          book.coverBmpPath = thumb;
          indexDirty = true;
          return CoverAttemptResult::Updated;
        }
      }
    }
    // Do at most one potentially expensive generation per main-loop pass so
    // the first text-only frame remains visible and responsive.
    return CoverAttemptResult::Attempted;
  }
  return attemptedAny ? CoverAttemptResult::Attempted : CoverAttemptResult::None;
}

bool LibraryActivity::matchesFilters(const LibraryBook& book) const {
  if (!matchesSelection(book.authors, viewState.authors)) {
    return false;
  }
  if (!viewState.series.empty() && !viewState.series.contains(book.series)) {
    return false;
  }
  return matchesSelection(book.tags, viewState.tags);
}

std::vector<size_t> LibraryActivity::filteredBookIndices() const {
  std::vector<size_t> indices;
  indices.reserve(books.size());
  for (size_t i = 0; i < books.size(); i++) {
    if (matchesFilters(books[i])) {
      indices.push_back(i);
    }
  }
  return indices;
}

void LibraryActivity::sortBooks() {
  const auto byTitle = [](const LibraryBook& left, const LibraryBook& right) {
    if (left.title == right.title) return FsHelpers::naturalLess(left.path, right.path);
    return FsHelpers::naturalLess(left.title, right.title);
  };
  const auto byAuthor = [&byTitle](const LibraryBook& left, const LibraryBook& right) {
    if (left.author == right.author) return byTitle(left, right);
    if (left.author.empty()) return false;
    if (right.author.empty()) return true;
    return FsHelpers::naturalLess(left.author, right.author);
  };
  const auto bySeries = [&byTitle](const LibraryBook& left, const LibraryBook& right) {
    if (left.series != right.series) {
      if (left.series.empty()) return false;
      if (right.series.empty()) return true;
      return FsHelpers::naturalLess(left.series, right.series);
    }
    if (left.seriesIndex != right.seriesIndex) {
      if (left.seriesIndex.empty()) return false;
      if (right.seriesIndex.empty()) return true;
      return FsHelpers::naturalLess(left.seriesIndex, right.seriesIndex);
    }
    return byTitle(left, right);
  };

  switch (viewState.sortMode) {
    case LibrarySortMode::Author:
      std::sort(books.begin(), books.end(), byAuthor);
      break;
    case LibrarySortMode::Series:
      std::sort(books.begin(), books.end(), bySeries);
      break;
    case LibrarySortMode::Title:
    case LibrarySortMode::Count:
      std::sort(books.begin(), books.end(), byTitle);
      break;
  }
}

void LibraryActivity::restoreSelector() {
  selectorIndex = 0;
  if (viewState.selectedBookPath.empty()) return;
  const auto indices = filteredBookIndices();
  const auto selected = std::find_if(indices.begin(), indices.end(), [this](const size_t index) {
    return books[index].path == viewState.selectedBookPath;
  });
  if (selected != indices.end()) selectorIndex = static_cast<size_t>(selected - indices.begin());
}

void LibraryActivity::rememberSelectedBook() {
  const auto indices = filteredBookIndices();
  if (indices.empty() || selectorIndex >= indices.size()) return;
  const std::string& path = books[indices[selectorIndex]].path;
  if (viewState.selectedBookPath == path) return;
  viewState.selectedBookPath = path;
  viewState.dirty = true;
}

void LibraryActivity::moveSelection(const int bookCount, const bool next, const bool byPage) {
  const int currentIndex = static_cast<int>(selectorIndex);
  if (byPage) {
    selectorIndex = next ? ButtonNavigator::nextPageIndex(currentIndex, bookCount, BookListItem::ITEMS_PER_PAGE)
                         : ButtonNavigator::previousPageIndex(currentIndex, bookCount, BookListItem::ITEMS_PER_PAGE);
  } else {
    selectorIndex = next ? ButtonNavigator::nextIndex(currentIndex, bookCount)
                         : ButtonNavigator::previousIndex(currentIndex, bookCount);
  }
  viewState.dirty = true;
  requestUpdate();
}

void LibraryActivity::openFilters() {
  rememberSelectedBook();
  lockLongPressBack = true;
  auto options = makeUniqueNoThrow<LibraryFiltersActivity>(renderer, mappedInput, books, viewState);
  if (!options) {
    LOG_ERR("LIB", "OOM: LibraryFiltersActivity");
    lockLongPressBack = false;
    return;
  }
  startActivityForResult(std::move(options), [this](const ActivityResult&) {
    sortBooks();
    restoreSelector();
    lockLongPressBack = false;
    requestUpdate();
  });
}

void LibraryActivity::loop() {
  if (lockNextConfirmRelease && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    lockNextConfirmRelease = false;
    return;
  }

  if (!lockLongPressBack && mappedInput.isPressed(MappedInputManager::Button::Back) &&
      mappedInput.getHeldTime() >= FILTER_HOLD_MS) {
    openFilters();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome();
    return;
  }

  const auto bookIndices = filteredBookIndices();
  const int bookCount = static_cast<int>(bookIndices.size());
  if (bookCount == 0) {
    return;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int rowHeight = BookListItem::rowHeight(contentHeight);
  const int pageStart = static_cast<int>(selectorIndex / BookListItem::ITEMS_PER_PAGE) * BookListItem::ITEMS_PER_PAGE;
  const int visibleRows = std::min(BookListItem::ITEMS_PER_PAGE, bookCount - pageStart);

  int row = -1;
  const auto touch =
      mappedInput.rowTouch(row, contentTop, rowHeight + BookListItem::ROW_GAP, visibleRows, 0,
                           renderer.getScreenWidth(), rowHeight);
  if (touch != MappedInputManager::RowTouch::None) {
    const size_t touchedIndex = static_cast<size_t>(pageStart + row);
    if (selectorIndex != touchedIndex) {
      selectorIndex = touchedIndex;
      viewState.dirty = true;
    }
    if (touch == MappedInputManager::RowTouch::Tap) {
      onSelectBook(books[bookIndices[selectorIndex]].path);
    } else {
      requestUpdate();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    onSelectBook(books[bookIndices[selectorIndex]].path);
    return;
  }

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    selectorIndex =
        ButtonNavigator::nextPageIndex(static_cast<int>(selectorIndex), bookCount, BookListItem::ITEMS_PER_PAGE);
    viewState.dirty = true;
    requestUpdate();
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    selectorIndex =
        ButtonNavigator::previousPageIndex(static_cast<int>(selectorIndex), bookCount, BookListItem::ITEMS_PER_PAGE);
    viewState.dirty = true;
    requestUpdate();
    return;
  }

  const auto handleRelease = [this, bookCount](const MappedInputManager::Button button, const bool next,
                                                const bool byPage) {
    if (!mappedInput.wasReleased(button)) return false;
    if (!navigationRepeated) moveSelection(bookCount, next, byPage);
    navigationRepeated = false;
    lastNavigationRepeatTime = 0;
    return true;
  };

  if (handleRelease(MappedInputManager::Button::Down, true, false) ||
      handleRelease(MappedInputManager::Button::Up, false, false) ||
      handleRelease(MappedInputManager::Button::Right, true, true) ||
      handleRelease(MappedInputManager::Button::Left, false, true)) {
    return;
  }

  const unsigned long now = millis();
  if (mappedInput.getHeldTime() > NAVIGATION_REPEAT_START_MS &&
      now - lastNavigationRepeatTime > NAVIGATION_REPEAT_INTERVAL_MS) {
    if (mappedInput.isPressed(MappedInputManager::Button::Down) ||
        mappedInput.isPressed(MappedInputManager::Button::Right)) {
      navigationRepeated = true;
      lastNavigationRepeatTime = now;
      moveSelection(bookCount, true, true);
      return;
    }
    if (mappedInput.isPressed(MappedInputManager::Button::Up) ||
        mappedInput.isPressed(MappedInputManager::Button::Left)) {
      navigationRepeated = true;
      lastNavigationRepeatTime = now;
      moveSelection(bookCount, false, true);
      return;
    }
  }

  if (ensureNextVisibleCover() != CoverAttemptResult::None) {
    requestUpdate();
  }
}

void LibraryActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  std::vector<size_t> bookIndices;
  char pageCounter[24] = {};
  const char* headerSubtitle = nullptr;
  if (!scanning) {
    bookIndices = filteredBookIndices();
    if (!bookIndices.empty()) {
      const size_t pageCount = (bookIndices.size() + BookListItem::ITEMS_PER_PAGE - 1) / BookListItem::ITEMS_PER_PAGE;
      const size_t currentPage = selectorIndex / BookListItem::ITEMS_PER_PAGE + 1;
      snprintf(pageCounter, sizeof(pageCounter), "%u / %u", static_cast<unsigned>(currentPage),
               static_cast<unsigned>(pageCount));
      headerSubtitle = pageCounter;
    }
  }
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_LIBRARY),
                 headerSubtitle);

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  if (scanning) {
    const Rect contentRect{0, contentTop, pageWidth, contentHeight};
    const int scanningTextY = contentTop + contentHeight / 2 - 38;
    UITheme::drawCenteredText(renderer, contentRect, UI_12_FONT_ID, scanningTextY, tr(STR_LIBRARY_SCANNING));
    if (totalBookCount > 0) {
      const std::string countText = std::to_string(scannedBookCount) + " / " + std::to_string(totalBookCount);
      UITheme::drawCenteredText(renderer, contentRect, UI_10_FONT_ID,
                                scanningTextY + renderer.getLineHeight(UI_12_FONT_ID) + 13, countText.c_str());
      const int progressWidth = std::min(pageWidth - 40, 220);
      GUI.drawProgressBar(
          renderer,
          Rect{(pageWidth - progressWidth) / 2,
               scanningTextY + renderer.getLineHeight(UI_12_FONT_ID) + renderer.getLineHeight(UI_10_FONT_ID) + 28,
               progressWidth, 16},
          scannedBookCount, totalBookCount);
    }
  } else {
    if (bookIndices.empty()) {
      UITheme::drawCenteredText(renderer, Rect{0, contentTop, pageWidth, contentHeight}, UI_12_FONT_ID,
                                contentTop + contentHeight / 2,
                                viewState.filtersActive() ? tr(STR_NO_LIBRARY_FILTERED_BOOKS)
                                                          : tr(STR_NO_LIBRARY_BOOKS));
    } else {
      const int rowHeight = BookListItem::rowHeight(contentHeight);
      const int pageStart =
          static_cast<int>(selectorIndex / BookListItem::ITEMS_PER_PAGE) * BookListItem::ITEMS_PER_PAGE;
      const int contentSidePadding = metrics.contentSidePadding;
      const int rowWidth = pageWidth - contentSidePadding * 2;

      for (int index = pageStart; index < static_cast<int>(bookIndices.size()) &&
                                  index < pageStart + BookListItem::ITEMS_PER_PAGE;
           index++) {
        const int rowY = contentTop + (index - pageStart) * (rowHeight + BookListItem::ROW_GAP);
        const bool selected = index == static_cast<int>(selectorIndex);

        const LibraryBook& book = books[bookIndices[index]];
        BookListItem::draw(renderer, book, contentSidePadding, rowY, rowWidth, rowHeight, selected);
      }
    }
  }

  const auto actions = mappedInput.mapNavigationActions();
  GUI.drawIconButtonHints(renderer, libraryButtonHint(actions.btn1), libraryButtonHint(actions.btn2),
                          libraryButtonHint(actions.btn3), libraryButtonHint(actions.btn4));
  renderer.displayBuffer();
}
