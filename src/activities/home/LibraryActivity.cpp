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

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int DIRECTORY_STACK_RESERVE = 16;
constexpr int FILE_NAME_BUFFER_SIZE = 500;
constexpr int ROW_GAP = 3;
constexpr int ROW_TEXT_VERTICAL_INSET = 7;
constexpr int TEXT_GAP = 14;
constexpr int PROGRESS_BAR_HEIGHT = 5;
constexpr size_t SCAN_PROGRESS_UPDATES = 10;

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

void LibraryActivity::addBook(const std::string& path) {
  LibraryBook book;
  book.path = path;
  book.title = filenameStem(path);

  if (FsHelpers::hasEpubExtension(path)) {
    Epub epub(path, "/.crosspoint");
    // The first library opening creates CrossPoint's normal book cache. Later
    // openings load only that cache and do not parse the EPUB CSS.
    if (epub.load(true, true)) {
      if (!epub.getTitle().empty()) book.title = epub.getTitle();
      book.author = epub.getAuthor();
      book.series = epub.getSeries();
      book.seriesIndex = epub.getSeriesIndex();
      const std::string thumb = epub.getThumbBmpPath(140);
      if (Storage.exists(thumb.c_str()) || epub.generateThumbBmp(140)) {
        book.coverBmpPath = thumb;
      }
      book.started = readEpubProgress(epub, book.progressPercent);
    }
  } else if (FsHelpers::hasXtcExtension(path)) {
    Xtc xtc(path, "/.crosspoint");
    if (xtc.load()) {
      if (!xtc.getTitle().empty()) book.title = xtc.getTitle();
      book.author = xtc.getAuthor();
      const std::string thumb = xtc.getThumbBmpPath(140);
      if (Storage.exists(thumb.c_str()) || xtc.generateThumbBmp(140)) {
        book.coverBmpPath = thumb;
      }
      book.started = readXtcProgress(xtc, book.progressPercent);
    }
  }

  books.push_back(std::move(book));
}

void LibraryActivity::scanDirectory(const std::string& path, std::vector<std::string>& bookPaths) {
  std::vector<std::string> directories;
  directories.reserve(DIRECTORY_STACK_RESERVE);
  directories.push_back(path);
  char fileName[FILE_NAME_BUFFER_SIZE] = {};

  while (!directories.empty()) {
    std::string directory = std::move(directories.back());
    directories.pop_back();

    HalFile root = Storage.open(directory.c_str());
    if (!root || !root.isDirectory()) {
      continue;
    }
    root.rewindDirectory();

    for (HalFile entry = root.openNextFile(); entry; entry = root.openNextFile()) {
      entry.getName(fileName, sizeof(fileName));
      const bool isDirectory = entry.isDirectory();
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
        bookPaths.push_back(std::move(fullPath));
      }
    }
    root.close();
  }
}

void LibraryActivity::scanLibrary() {
  books.clear();
  scannedBookCount = 0;
  totalBookCount = 0;

  std::vector<std::string> bookPaths;
  scanDirectory("/", bookPaths);
  totalBookCount = bookPaths.size();

  // Now that the total is known, paint 0/N before the slower metadata and
  // thumbnail generation begins.
  requestUpdateAndWait();

  const size_t updateInterval = std::max<size_t>(1, (totalBookCount + SCAN_PROGRESS_UPDATES - 1) /
                                                         SCAN_PROGRESS_UPDATES);
  for (const auto& path : bookPaths) {
    addBook(path);
    scannedBookCount++;

    // Full e-ink refreshes are deliberately limited to about ten per scan.
    if (scannedBookCount % updateInterval == 0 || scannedBookCount == totalBookCount) {
      requestUpdateAndWait();
    }
  }

  std::sort(books.begin(), books.end(), [](const LibraryBook& left, const LibraryBook& right) {
    if (left.title == right.title) {
      return FsHelpers::naturalLess(left.path, right.path);
    }
    return FsHelpers::naturalLess(left.title, right.title);
  });
}

void LibraryActivity::onEnter() {
  Activity::onEnter();
  selectorIndex = 0;
  lockNextConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);

  // Show a complete e-ink frame before metadata and thumbnails are read from
  // the SD card. The initial scan can take a while for uncached EPUB files.
  scanning = true;
  requestUpdateAndWait();
  scanLibrary();
  scanning = false;
  requestUpdate();
}

void LibraryActivity::onExit() {
  Activity::onExit();
  books.clear();
}

void LibraryActivity::loop() {
  if (lockNextConfirmRelease && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    lockNextConfirmRelease = false;
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome();
    return;
  }

  const int bookCount = static_cast<int>(books.size());
  if (bookCount == 0) {
    return;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int rowHeight = std::max(1, (contentHeight - ROW_GAP * (BOOKS_PER_PAGE - 1)) / BOOKS_PER_PAGE);
  const int pageStart = static_cast<int>(selectorIndex / BOOKS_PER_PAGE) * BOOKS_PER_PAGE;
  const int visibleRows = std::min(BOOKS_PER_PAGE, bookCount - pageStart);

  int row = -1;
  const auto touch = mappedInput.rowTouch(row, contentTop, rowHeight + ROW_GAP, visibleRows, 0, renderer.getScreenWidth(),
                                          rowHeight);
  if (touch != MappedInputManager::RowTouch::None) {
    selectorIndex = static_cast<size_t>(pageStart + row);
    if (touch == MappedInputManager::RowTouch::Tap) {
      onSelectBook(books[selectorIndex].path);
    } else {
      requestUpdate();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    onSelectBook(books[selectorIndex].path);
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
      GUI.drawProgressBar(renderer,
                          Rect{(pageWidth - progressWidth) / 2, scanningTextY + renderer.getLineHeight(UI_12_FONT_ID) +
                                                                       renderer.getLineHeight(UI_10_FONT_ID) + 28,
                               progressWidth, 16},
                          scannedBookCount, totalBookCount);
    }
  } else if (books.empty()) {
    UITheme::drawCenteredText(renderer, Rect{0, contentTop, pageWidth, contentHeight}, UI_12_FONT_ID,
                              contentTop + contentHeight / 2, tr(STR_NO_LIBRARY_BOOKS));
  } else {
    const int rowHeight = std::max(1, (contentHeight - ROW_GAP * (BOOKS_PER_PAGE - 1)) / BOOKS_PER_PAGE);
    const int pageStart = static_cast<int>(selectorIndex / BOOKS_PER_PAGE) * BOOKS_PER_PAGE;
    const int contentSidePadding = metrics.contentSidePadding;
    const int rowWidth = pageWidth - contentSidePadding * 2;
    const int coverHeight = std::max(1, rowHeight - 2);
    const int coverWidth = std::max(1, coverHeight * 2 / 3);
    const int textX = contentSidePadding + coverWidth + TEXT_GAP;
    const int textWidth = pageWidth - contentSidePadding - textX;

    for (int index = pageStart; index < static_cast<int>(books.size()) && index < pageStart + BOOKS_PER_PAGE; index++) {
      const int rowY = contentTop + (index - pageStart) * (rowHeight + ROW_GAP);
      const bool selected = index == static_cast<int>(selectorIndex);
      if (selected) {
        renderer.fillRoundedRect(contentSidePadding, rowY, rowWidth, rowHeight, 5, Color::LightGray);
      }

      const LibraryBook& book = books[index];
      drawBookCover(book, contentSidePadding + 1, rowY + 1, coverWidth, coverHeight);
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

  const auto labels = mappedInput.mapLabels(tr(STR_HOME), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
