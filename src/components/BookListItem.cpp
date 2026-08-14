#include "BookListItem.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>

#include <algorithm>
#include <cstring>

#include "fontIds.h"

namespace BookListItem {
namespace {
constexpr int TEXT_VERTICAL_INSET = 7;
constexpr int TEXT_GAP = 14;
constexpr int PROGRESS_BAR_HEIGHT = 5;
constexpr size_t TITLE_LINE_BUFFER_SIZE = 192;
constexpr char ELLIPSIS[] = "\xe2\x80\xa6";

struct PrefixFit {
  size_t bytes = 0;
  size_t lastSpace = 0;
};

size_t utf8CharBytes(const char* text, const size_t remaining) {
  if (remaining == 0) return 0;
  const uint8_t lead = static_cast<uint8_t>(text[0]);
  size_t count = 1;
  if ((lead & 0xE0) == 0xC0)
    count = 2;
  else if ((lead & 0xF0) == 0xE0)
    count = 3;
  else if ((lead & 0xF8) == 0xF0)
    count = 4;
  if (count > remaining) return 1;
  for (size_t i = 1; i < count; i++) {
    if ((static_cast<uint8_t>(text[i]) & 0xC0) != 0x80) return 1;
  }
  return count;
}

PrefixFit fitPrefix(const GfxRenderer& renderer, const int fontId, const char* text, const int maxWidth,
                    const EpdFontFamily::Style style, const bool reserveEllipsis, char* buffer) {
  PrefixFit fit;
  const size_t length = strlen(text);
  const size_t suffixBytes = reserveEllipsis ? sizeof(ELLIPSIS) - 1 : 0;
  size_t copied = 0;
  while (copied < length) {
    const size_t characterBytes = utf8CharBytes(text + copied, length - copied);
    if (copied + characterBytes + suffixBytes >= TITLE_LINE_BUFFER_SIZE) break;
    memcpy(buffer + copied, text + copied, characterBytes);
    copied += characterBytes;
    if (reserveEllipsis) memcpy(buffer + copied, ELLIPSIS, suffixBytes);
    buffer[copied + suffixBytes] = '\0';
    if (renderer.getTextWidth(fontId, buffer, style) > maxWidth) break;
    fit.bytes = copied;
    if (text[copied - characterBytes] == ' ') fit.lastSpace = copied - characterBytes;
  }
  return fit;
}

int drawTitle(const GfxRenderer& renderer, const std::string& title, const int x, const int y, const int maxWidth) {
  constexpr auto style = EpdFontFamily::BOLD;
  if (maxWidth <= 0 || title.empty()) return 1;
  if (renderer.getTextWidth(UI_12_FONT_ID, title.c_str(), style) <= maxWidth) {
    renderer.drawText(UI_12_FONT_ID, x, y, title.c_str(), true, style);
    return 1;
  }

  char line[TITLE_LINE_BUFFER_SIZE] = {};
  const PrefixFit firstFit = fitPrefix(renderer, UI_12_FONT_ID, title.c_str(), maxWidth, style, false, line);
  const size_t split = firstFit.lastSpace > 0 ? firstFit.lastSpace : firstFit.bytes;
  if (split == 0) {
    renderer.drawText(UI_12_FONT_ID, x, y, ELLIPSIS, true, style);
    return 1;
  }
  memcpy(line, title.data(), split);
  line[split] = '\0';
  renderer.drawText(UI_12_FONT_ID, x, y, line, true, style);

  const char* remainder = title.c_str() + split;
  while (*remainder == ' ') remainder++;
  if (*remainder == '\0') return 1;
  const int secondY = y + renderer.getLineHeight(UI_12_FONT_ID);
  if (renderer.getTextWidth(UI_12_FONT_ID, remainder, style) <= maxWidth) {
    renderer.drawText(UI_12_FONT_ID, x, secondY, remainder, true, style);
    return 2;
  }

  const PrefixFit secondFit = fitPrefix(renderer, UI_12_FONT_ID, remainder, maxWidth, style, true, line);
  const size_t ellipsisBytes = sizeof(ELLIPSIS) - 1;
  memcpy(line, remainder, secondFit.bytes);
  memcpy(line + secondFit.bytes, ELLIPSIS, ellipsisBytes);
  line[secondFit.bytes + ellipsisBytes] = '\0';
  renderer.drawText(UI_12_FONT_ID, x, secondY, line, true, style);
  return 2;
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
  int itemCoverWidth = drawCover(renderer, book, x, y, maxCoverWidth, height);
  if (itemCoverWidth == 0 && !book.coverAttempted) {
    itemCoverWidth = std::min(maxCoverWidth, std::max(1, height * 2 / 3));
  }
  renderer.drawRoundedRect(x, y, width, height, 1, 5, true);

  const int textX = x + itemCoverWidth + TEXT_GAP;
  const int textRight = x + width - 3;
  const int textWidth = std::max(0, textRight - textX);
  const int titleY = y + TEXT_VERTICAL_INSET;
  const int titleLineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int titleLineCount = drawTitle(renderer, book.title, textX, titleY, textWidth);
  const int authorY = titleY + titleLineCount * titleLineHeight + 5;
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
