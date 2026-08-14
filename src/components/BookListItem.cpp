#include "BookListItem.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>

#include <algorithm>

#include "fontIds.h"

namespace BookListItem {
namespace {
constexpr int TEXT_VERTICAL_INSET = 7;
constexpr int TEXT_GAP = 14;
constexpr int PROGRESS_BAR_HEIGHT = 5;

std::string displaySeriesIndex(std::string index) {
  const size_t decimalPoint = index.find('.');
  if (decimalPoint != std::string::npos && decimalPoint + 1 < index.size() &&
      index.find_first_not_of('0', decimalPoint + 1) == std::string::npos) {
    index.erase(decimalPoint);
  }
  return index;
}
}  // namespace

int rowHeight(const int contentHeight) {
  return std::max(1, (contentHeight - ROW_GAP * (ITEMS_PER_PAGE - 1)) / ITEMS_PER_PAGE);
}

int drawCover(GfxRenderer& renderer, const LibraryBook& book, const int x, const int y, const int maxWidth,
              const int height) {
  if (book.coverBmpPath.empty() || maxWidth <= 0 || height <= 0) return 0;

  HalFile coverFile;
  if (!Storage.openFileForRead("BOOK_UI", book.coverBmpPath, coverFile)) return 0;

  Bitmap bitmap(coverFile);
  if (bitmap.parseHeaders() != BmpReaderError::Ok || bitmap.getWidth() <= 0 || bitmap.getHeight() <= 0) {
    return 0;
  }

  const float scale = std::min(2.0f, std::min(static_cast<float>(maxWidth) / bitmap.getWidth(),
                                              static_cast<float>(height) / bitmap.getHeight()));
  const int drawWidth = std::max(1, static_cast<int>(bitmap.getWidth() * scale));
  const int drawHeight = std::max(1, static_cast<int>(bitmap.getHeight() * scale));
  if (bitmap.is1Bit()) {
    renderer.drawBitmap1Bit(bitmap, x, y + (height - drawHeight) / 2, drawWidth, drawHeight, true);
  } else {
    renderer.drawBitmap(bitmap, x, y + (height - drawHeight) / 2, drawWidth, drawHeight);
  }
  return drawWidth;
}

int draw(GfxRenderer& renderer, const LibraryBook& book, const int x, const int y, const int width, const int height,
         const bool selected) {
  if (selected) {
    renderer.fillRoundedRect(x, y, width, height, 5, Color::LightGray);
  }

  const int maxCoverWidth = std::max(1, width * 2 / 3);
  const int itemCoverWidth = drawCover(renderer, book, x, y, maxCoverWidth, height);
  renderer.drawRoundedRect(x, y, width, height, 1, 5, true);

  const int textX = x + itemCoverWidth + TEXT_GAP;
  const int textRight = x + width - 3;
  const int textWidth = std::max(0, textRight - textX);
  const int titleY = y + TEXT_VERTICAL_INSET;
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

  if (!book.started) return itemCoverWidth;

  const std::string progressText = std::to_string(book.progressPercent) + "%";
  const int progressY = y + height - TEXT_VERTICAL_INSET - renderer.getLineHeight(SMALL_FONT_ID);
  renderer.drawText(SMALL_FONT_ID, textX, progressY, progressText.c_str());
  const int labelWidth = renderer.getTextWidth(SMALL_FONT_ID, progressText.c_str());
  const int barX = textX + labelWidth + 8;
  const int barWidth = std::max(0, textRight - barX);
  const int barY = progressY + renderer.getLineHeight(SMALL_FONT_ID) / 2 - PROGRESS_BAR_HEIGHT / 2;
  if (barWidth <= 0) return itemCoverWidth;

  renderer.drawRect(barX, barY, barWidth, PROGRESS_BAR_HEIGHT);
  const int fillWidth = std::max(0, (barWidth - 2) * book.progressPercent / 100);
  if (fillWidth > 0) {
    renderer.fillRect(barX + 1, barY + 1, fillWidth, PROGRESS_BAR_HEIGHT - 2);
  }
  return itemCoverWidth;
}

}  // namespace BookListItem
