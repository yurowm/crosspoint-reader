#include "LibraryActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Xtc.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <unordered_map>

#include "LibraryFiltersActivity.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/BookCacheUtils.h"

namespace {
constexpr int DIRECTORY_STACK_RESERVE = 16;
constexpr int FILE_NAME_BUFFER_SIZE = 500;
constexpr int ROW_GAP = 3;
constexpr int ROW_TEXT_VERTICAL_INSET = 7;
constexpr int TEXT_GAP = 14;
constexpr int PROGRESS_BAR_HEIGHT = 5;
constexpr size_t SCAN_PROGRESS_UPDATES = 10;
constexpr unsigned long FILTER_HOLD_MS = 1000;

bool isBookFile(const std::string& path) {
  return FsHelpers::hasEpubExtension(path) || FsHelpers::hasXtcExtension(path) || FsHelpers::hasTxtExtension(path) ||
         FsHelpers::hasMarkdownExtension(path);
}

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
      return {.icon = ChevronUp};
    case MappedInputManager::NavigationAction::Next:
      return {.icon = ChevronDown};
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
      const std::string thumb = epub.getThumbBmpPath(140);
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
      const std::string thumb = xtc.getThumbBmpPath(140);
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

  std::sort(updatedBooks.begin(), updatedBooks.end(), [](const LibraryBook& left, const LibraryBook& right) {
    if (left.title == right.title) {
      return FsHelpers::naturalLess(left.path, right.path);
    }
    return FsHelpers::naturalLess(left.title, right.title);
  });
  books = std::move(updatedBooks);

  const bool libraryChanged = !indexLoaded || !changedFiles.empty() || removedBooks;
  if (libraryChanged) {
    indexDirty = !LibraryIndex::save(books);
  }
  return libraryChanged;
}

void LibraryActivity::onEnter() {
  Activity::onEnter();
  selectorIndex = 0;
  lockNextConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  indexDirty = false;

  // A valid index paints the complete library immediately. The directory pass
  // that follows only opens book metadata for new or changed source files.
  const bool indexLoaded = LibraryIndex::load(books);
  scanning = !indexLoaded;
  requestUpdateAndWait();
  const bool libraryChanged = scanLibrary(indexLoaded);
  scanning = false;
  if (libraryChanged) requestUpdate();
}

void LibraryActivity::onExit() {
  Activity::onExit();
  if (indexDirty && !LibraryIndex::save(books)) {
    LOG_ERR("LIB", "Failed to persist lazy cover updates");
  }
  books.clear();
  filters.clear();
}

LibraryActivity::CoverAttemptResult LibraryActivity::ensureNextVisibleCover() {
  const auto bookIndices = filteredBookIndices();
  if (bookIndices.empty()) return CoverAttemptResult::None;

  const size_t pageStart = selectorIndex / BOOKS_PER_PAGE * BOOKS_PER_PAGE;
  const size_t pageEnd = std::min(bookIndices.size(), pageStart + BOOKS_PER_PAGE);
  for (size_t index = pageStart; index < pageEnd; index++) {
    LibraryBook& book = books[bookIndices[index]];
    if (book.coverAttempted) continue;
    book.coverAttempted = true;

    if (!book.coverBmpPath.empty() && Storage.exists(book.coverBmpPath.c_str())) {
      continue;
    }
    book.coverBmpPath.clear();

    if (FsHelpers::hasEpubExtension(book.path)) {
      Epub epub(book.path, "/.crosspoint");
      if (epub.loadMetadata()) {
        const std::string thumb = epub.getThumbBmpPath(140);
        if (Storage.exists(thumb.c_str()) || epub.generateThumbBmp(140)) {
          book.coverBmpPath = thumb;
          indexDirty = true;
          return CoverAttemptResult::Updated;
        }
      }
    } else if (FsHelpers::hasXtcExtension(book.path)) {
      Xtc xtc(book.path, "/.crosspoint");
      if (xtc.load()) {
        const std::string thumb = xtc.getThumbBmpPath(140);
        if (Storage.exists(thumb.c_str()) || xtc.generateThumbBmp(140)) {
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
  return CoverAttemptResult::None;
}

bool LibraryActivity::matchesFilters(const LibraryBook& book) const {
  if (!matchesSelection(book.authors, filters.authors)) {
    return false;
  }
  if (!filters.series.empty() && !filters.series.contains(book.series)) {
    return false;
  }
  return matchesSelection(book.tags, filters.tags);
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

void LibraryActivity::openFilters() {
  lockLongPressBack = true;
  startActivityForResult(std::make_unique<LibraryFiltersActivity>(renderer, mappedInput, books, filters),
                         [this](const ActivityResult&) {
                           selectorIndex = 0;
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
  const int rowHeight = std::max(1, (contentHeight - ROW_GAP * (BOOKS_PER_PAGE - 1)) / BOOKS_PER_PAGE);
  const int pageStart = static_cast<int>(selectorIndex / BOOKS_PER_PAGE) * BOOKS_PER_PAGE;
  const int visibleRows = std::min(BOOKS_PER_PAGE, bookCount - pageStart);

  int row = -1;
  const auto touch =
      mappedInput.rowTouch(row, contentTop, rowHeight + ROW_GAP, visibleRows, 0, renderer.getScreenWidth(), rowHeight);
  if (touch != MappedInputManager::RowTouch::None) {
    selectorIndex = static_cast<size_t>(pageStart + row);
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
    selectorIndex = ButtonNavigator::nextPageIndex(static_cast<int>(selectorIndex), bookCount, BOOKS_PER_PAGE);
    requestUpdate();
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    selectorIndex = ButtonNavigator::previousPageIndex(static_cast<int>(selectorIndex), bookCount, BOOKS_PER_PAGE);
    requestUpdate();
    return;
  }

  buttonNavigator.onNextRelease([this, bookCount] {
    selectorIndex = ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), bookCount);
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this, bookCount] {
    selectorIndex = ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), bookCount);
    requestUpdate();
  });
  buttonNavigator.onNextContinuous([this, bookCount] {
    selectorIndex = ButtonNavigator::nextPageIndex(static_cast<int>(selectorIndex), bookCount, BOOKS_PER_PAGE);
    requestUpdate();
  });
  buttonNavigator.onPreviousContinuous([this, bookCount] {
    selectorIndex = ButtonNavigator::previousPageIndex(static_cast<int>(selectorIndex), bookCount, BOOKS_PER_PAGE);
    requestUpdate();
  });

  if (ensureNextVisibleCover() == CoverAttemptResult::Updated) {
    requestUpdate();
  }
}

void LibraryActivity::drawBookCover(const LibraryBook& book, const int x, const int y, const int width,
                                    const int height) const {
  if (!book.coverBmpPath.empty()) {
    HalFile coverFile;
    if (Storage.openFileForRead("LIB", book.coverBmpPath, coverFile)) {
      Bitmap bitmap(coverFile);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        const float scale = std::min(1.0f, std::min(static_cast<float>(width) / bitmap.getWidth(),
                                                    static_cast<float>(height) / bitmap.getHeight()));
        const int drawWidth = std::max(1, static_cast<int>(bitmap.getWidth() * scale));
        const int drawHeight = std::max(1, static_cast<int>(bitmap.getHeight() * scale));
        renderer.drawBitmap(bitmap, x + (width - drawWidth) / 2, y + (height - drawHeight) / 2, drawWidth, drawHeight);
      }
      coverFile.close();
    }
  }
}

void LibraryActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_LIBRARY));

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
    const auto bookIndices = filteredBookIndices();
    if (bookIndices.empty()) {
      UITheme::drawCenteredText(renderer, Rect{0, contentTop, pageWidth, contentHeight}, UI_12_FONT_ID,
                                contentTop + contentHeight / 2,
                                filters.isActive() ? tr(STR_NO_LIBRARY_FILTERED_BOOKS) : tr(STR_NO_LIBRARY_BOOKS));
    } else {
      const int rowHeight = std::max(1, (contentHeight - ROW_GAP * (BOOKS_PER_PAGE - 1)) / BOOKS_PER_PAGE);
      const int pageStart = static_cast<int>(selectorIndex / BOOKS_PER_PAGE) * BOOKS_PER_PAGE;
      const int contentSidePadding = metrics.contentSidePadding;
      const int rowWidth = pageWidth - contentSidePadding * 2;
      const int coverHeight = rowHeight;
      const int coverWidth = std::max(1, coverHeight * 2 / 3);
      const int textX = contentSidePadding + coverWidth + TEXT_GAP;
      const int textWidth = pageWidth - contentSidePadding - textX;

      for (int index = pageStart; index < static_cast<int>(bookIndices.size()) && index < pageStart + BOOKS_PER_PAGE;
           index++) {
        const int rowY = contentTop + (index - pageStart) * (rowHeight + ROW_GAP);
        const bool selected = index == static_cast<int>(selectorIndex);
        if (selected) {
          renderer.fillRoundedRect(contentSidePadding, rowY, rowWidth, rowHeight, 5, Color::LightGray);
        }

        const LibraryBook& book = books[bookIndices[index]];
        drawBookCover(book, contentSidePadding, rowY, coverWidth, coverHeight);
        renderer.drawRoundedRect(contentSidePadding, rowY, rowWidth, rowHeight, 1, 5, true);

        const int titleY = rowY + ROW_TEXT_VERTICAL_INSET;
        const auto title = renderer.truncatedText(UI_12_FONT_ID, book.title.c_str(), textWidth, EpdFontFamily::BOLD);
        renderer.drawText(UI_12_FONT_ID, textX, titleY, title.c_str(), true, EpdFontFamily::BOLD);

        const int authorY = titleY + renderer.getLineHeight(UI_12_FONT_ID) + 5;
        if (!book.author.empty()) {
          const auto author = renderer.truncatedText(UI_10_FONT_ID, book.author.c_str(), textWidth);
          renderer.drawText(UI_10_FONT_ID, textX, authorY, author.c_str());
        }

        const int seriesY = authorY + renderer.getLineHeight(UI_10_FONT_ID) + 5;
        if (!book.series.empty()) {
          std::string series = book.series;
          if (!book.seriesIndex.empty()) {
            series += " · " + displaySeriesIndex(book.seriesIndex);
          }
          const auto seriesText = renderer.truncatedText(SMALL_FONT_ID, series.c_str(), textWidth);
          renderer.drawText(SMALL_FONT_ID, textX, seriesY, seriesText.c_str());
        }

        if (book.started) {
          const std::string progressText = std::to_string(book.progressPercent) + "%";
          const int progressY = rowY + rowHeight - ROW_TEXT_VERTICAL_INSET - renderer.getLineHeight(SMALL_FONT_ID);
          renderer.drawText(SMALL_FONT_ID, textX, progressY, progressText.c_str());
          const int labelWidth = renderer.getTextWidth(SMALL_FONT_ID, progressText.c_str());
          const int barX = textX + labelWidth + 8;
          const int barRight = pageWidth - contentSidePadding - 3;
          const int barWidth = std::max(0, barRight - barX);
          const int barY = progressY + renderer.getLineHeight(SMALL_FONT_ID) / 2 - PROGRESS_BAR_HEIGHT / 2;
          if (barWidth > 0) {
            renderer.drawRect(barX, barY, barWidth, PROGRESS_BAR_HEIGHT);
            const int fillWidth = std::max(0, (barWidth - 2) * book.progressPercent / 100);
            if (fillWidth > 0) {
              renderer.fillRect(barX + 1, barY + 1, fillWidth, PROGRESS_BAR_HEIGHT - 2);
            }
          }
        }
      }
    }
  }

  const auto actions = mappedInput.mapNavigationActions();
  GUI.drawIconButtonHints(renderer, libraryButtonHint(actions.btn1), libraryButtonHint(actions.btn2),
                          libraryButtonHint(actions.btn3), libraryButtonHint(actions.btn4));
  renderer.displayBuffer();
}
