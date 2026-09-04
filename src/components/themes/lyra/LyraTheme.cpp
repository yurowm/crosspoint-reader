#include "LyraTheme.h"

#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "ReadingStats.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/icons/cover.h"
#include "fontIds.h"

// Internal constants
namespace {
constexpr int hPaddingInSelection = 8;
constexpr int homeCoverSidePadding = 8;
constexpr int cornerRadius = 6;
constexpr int topHintButtonY = 345;
constexpr int maxListValueWidth = 200;
constexpr int mainMenuIconSize = 32;
constexpr int listIconSize = 24;
constexpr int mainMenuColumns = 2;
int coverWidth = 0;
}  // namespace

void LyraTheme::fillBatteryIcon(const GfxRenderer& renderer, Rect rect, uint16_t percentage) const {
  const bool charging = gpio.isUsbConnected();

  if (charging) {
    // Solid fill when charging so lightning bolt is visible
    renderer.fillRect(rect.x + 2, rect.y + 2, rect.width - 5, rect.height - 4);
    drawBatteryLightningBolt(renderer, rect.x + 4, rect.y + 2);
  } else {
    if (percentage > 10) {
      renderer.fillRect(rect.x + 2, rect.y + 2, 3, rect.height - 4);
    }
    if (percentage > 40) {
      renderer.fillRect(rect.x + 6, rect.y + 2, 3, rect.height - 4);
    }
    if (percentage > 70) {
      renderer.fillRect(rect.x + 10, rect.y + 2, 3, rect.height - 4);
    }
  }
}

void LyraTheme::drawSubHeader(const GfxRenderer& renderer, Rect rect, const char* label, const char* rightLabel) const {
  const int contentWidth = std::max(0, rect.width - LyraMetrics::values.contentSidePadding * 2);

  int labelWidth = contentWidth;
  if (rightLabel) {
    auto truncatedRightLabel = renderer.truncatedText(SMALL_FONT_ID, rightLabel, contentWidth, EpdFontFamily::REGULAR);
    const int rightLabelWidth = renderer.getTextWidth(SMALL_FONT_ID, truncatedRightLabel.c_str());
    renderer.drawText(SMALL_FONT_ID, rect.x + rect.width - LyraMetrics::values.contentSidePadding - rightLabelWidth,
                      rect.y + 7, truncatedRightLabel.c_str());
    labelWidth = std::max(0, contentWidth - rightLabelWidth - hPaddingInSelection);
  }

  if (labelWidth > 0) {
    auto truncatedLabel = renderer.truncatedText(UI_10_FONT_ID, label, labelWidth, EpdFontFamily::REGULAR);
    renderer.drawText(UI_10_FONT_ID, rect.x + LyraMetrics::values.contentSidePadding, rect.y + 6,
                      truncatedLabel.c_str(), true, EpdFontFamily::REGULAR);
  }

  renderer.drawLine(rect.x, rect.y + rect.height - 1, rect.x + rect.width - 1, rect.y + rect.height - 1, true);
}

int LyraTheme::getListRowStep(bool hasSubtitle) const {
  int rowHeight = (hasSubtitle) ? LyraMetrics::values.listWithSubtitleRowHeight : LyraMetrics::values.listRowHeight;
  return rowHeight;
}

int LyraTheme::getListPageItems(int contentHeight, bool hasSubtitle) const {
  const int rowStep = getListRowStep(hasSubtitle);
  if (rowStep <= 0) return 1;
  return std::max(1, contentHeight / rowStep);
}

void LyraTheme::drawList(const GfxRenderer& renderer, Rect rect, int itemCount, int selectedIndex,
                         const std::function<std::string(int index)>& rowTitle,
                         const std::function<std::string(int index)>& rowSubtitle,
                         const std::function<UIIcon(int index)>& rowIcon,
                         const std::function<std::string(int index)>& rowValue, bool highlightValue,
                         const std::function<bool(int index)>& rowDimmed) const {
  int rowHeight =
      (rowSubtitle != nullptr) ? LyraMetrics::values.listWithSubtitleRowHeight : LyraMetrics::values.listRowHeight;
  int pageItems = rowHeight > 0 ? std::max(1, rect.height / rowHeight) : 1;

  const int totalPages = (itemCount + pageItems - 1) / pageItems;
  if (totalPages > 1) {
    const int scrollAreaHeight = rect.height;

    // Draw scroll bar
    const int scrollBarHeight = (scrollAreaHeight * pageItems) / itemCount;
    const int currentPage = selectedIndex / pageItems;
    const int scrollBarY = rect.y + ((scrollAreaHeight - scrollBarHeight) * currentPage) / (totalPages - 1);
    const int scrollBarX = rect.x + rect.width - LyraMetrics::values.scrollBarRightOffset;
    renderer.drawLine(scrollBarX, rect.y, scrollBarX, rect.y + scrollAreaHeight, true);
    renderer.fillRect(scrollBarX - LyraMetrics::values.scrollBarWidth, scrollBarY, LyraMetrics::values.scrollBarWidth,
                      scrollBarHeight, true);
  }

  // Draw selection
  int contentWidth =
      rect.width -
      (totalPages > 1 ? (LyraMetrics::values.scrollBarWidth + LyraMetrics::values.scrollBarRightOffset) : 1);
  if (selectedIndex >= 0) {
    renderer.fillRoundedRect(
        rect.x + LyraMetrics::values.contentSidePadding, rect.y + selectedIndex % pageItems * rowHeight,
        contentWidth - LyraMetrics::values.contentSidePadding * 2, rowHeight, cornerRadius, Color::LightGray);
  }

  int textX = rect.x + LyraMetrics::values.contentSidePadding + hPaddingInSelection;
  int textWidth = contentWidth - LyraMetrics::values.contentSidePadding * 2 - hPaddingInSelection * 2;
  int iconSize;
  if (rowIcon != nullptr) {
    iconSize = (rowSubtitle != nullptr) ? mainMenuIconSize : listIconSize;
    textX += iconSize + hPaddingInSelection;
    textWidth -= iconSize + hPaddingInSelection;
  }

  // Draw all items
  const auto pageStartIndex = selectedIndex / pageItems * pageItems;
  int iconY = (rowSubtitle != nullptr) ? 16 : 10;
  for (int i = pageStartIndex; i < itemCount && i < pageStartIndex + pageItems; i++) {
    const int itemY = rect.y + (i % pageItems) * rowHeight;
    int rowTextWidth = textWidth;

    // Draw name
    int valueWidth = 0;
    std::string valueText = "";
    if (rowValue != nullptr) {
      valueText = rowValue(i);
      valueText = renderer.truncatedText(UI_10_FONT_ID, valueText.c_str(), maxListValueWidth);
      valueWidth = renderer.getTextWidth(UI_10_FONT_ID, valueText.c_str()) + hPaddingInSelection;
      rowTextWidth -= valueWidth;
    }

    auto itemName = rowTitle(i);
    auto item = renderer.truncatedText(UI_10_FONT_ID, itemName.c_str(), rowTextWidth);
    renderer.drawText(UI_10_FONT_ID, textX, itemY + 7, item.c_str(), true);

    // Apply checkerboard dither to create gray text effect for dimmed items
    if (rowDimmed && rowDimmed(i) && i != selectedIndex) {
      const int titleWidth = renderer.getTextWidth(UI_10_FONT_ID, item.c_str());
      const int lineH = renderer.getLineHeight(UI_10_FONT_ID);
      for (int py = itemY + 7; py < itemY + 7 + lineH; py++)
        for (int px = textX; px < textX + titleWidth; px++)
          if ((px + py) % 2 == 0) renderer.drawPixel(px, py, false);
    }

    if (rowIcon != nullptr) {
      UIIcon icon = rowIcon(i);
      const uint8_t* iconBitmap = getUIIconBitmap(icon, iconSize);
      if (iconBitmap != nullptr) {
        drawUIIcon(renderer, icon, rect.x + LyraMetrics::values.contentSidePadding + hPaddingInSelection, itemY + iconY,
                   iconSize);
      }
    }

    if (rowSubtitle != nullptr) {
      // Draw subtitle
      std::string subtitleText = rowSubtitle(i);
      auto subtitle = renderer.truncatedText(SMALL_FONT_ID, subtitleText.c_str(), rowTextWidth);
      renderer.drawText(SMALL_FONT_ID, textX, itemY + 30, subtitle.c_str(), true);
    }

    // Draw value
    if (!valueText.empty()) {
      if (i == selectedIndex && highlightValue) {
        renderer.fillRoundedRect(
            rect.x + contentWidth - LyraMetrics::values.contentSidePadding - hPaddingInSelection - valueWidth, itemY,
            valueWidth + hPaddingInSelection, rowHeight, cornerRadius, Color::Black);
      }

      int valueY = itemY + 6;
      if (rowSubtitle != nullptr) {
        valueY = itemY + 16;
      }
      renderer.drawText(UI_10_FONT_ID, rect.x + contentWidth - LyraMetrics::values.contentSidePadding - valueWidth,
                        valueY, valueText.c_str(), !(i == selectedIndex && highlightValue));
    }
  }
}

void LyraTheme::drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                                const char* btn4) const {
  if (gpio.hasTouch()) {
    return;
  }

  const GfxRenderer::Orientation orig_orientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  const int pageHeight = renderer.getScreenHeight();
  constexpr int buttonWidth = 80;
  constexpr int smallButtonHeight = 15;
  constexpr int buttonHeight = LyraMetrics::values.buttonHintsHeight;
  constexpr int buttonY = LyraMetrics::values.buttonHintsHeight;  // Distance from bottom
  constexpr int textYOffset = 7;                                  // Distance from top of button to text baseline
  constexpr int iconSize = 24;
  // Keyed to the portrait panel width: the 528-wide X3 gets more spacing than
  // the 480-wide boards (X4, X4 Pro, and the other 800x480 panels).
  constexpr int narrowButtonPositions[] = {58, 146, 254, 342};
  constexpr int wideButtonPositions[] = {65, 157, 291, 383};
  const int* buttonPositions = renderer.getScreenWidth() >= 528 ? wideButtonPositions : narrowButtonPositions;
  const char* labels[] = {btn1, btn2, btn3, btn4};
  constexpr uint8_t hardwareButtons[] = {HalGPIO::BTN_BACK, HalGPIO::BTN_CONFIRM, HalGPIO::BTN_LEFT,
                                         HalGPIO::BTN_RIGHT};

  for (int i = 0; i < 4; i++) {
    const int x = buttonPositions[i];
    const ButtonHint hint = buttonHintFromLabel(labels[i], hardwareButtons[i]);
    if (getButtonHintContentWidth(renderer, hint, iconSize, SMALL_FONT_ID) > 0) {
      // Draw the filled background and border for a FULL-sized button
      renderer.fillRoundedRect(x, pageHeight - buttonY, buttonWidth, buttonHeight, cornerRadius, Color::White);
      renderer.drawRoundedRect(x, pageHeight - buttonY, buttonWidth, buttonHeight, 1, cornerRadius, true, true, false,
                               false, true);
      if (hint.icon != None) {
        drawButtonHintContent(renderer, hint, x + buttonWidth / 2, pageHeight - buttonY + buttonHeight / 2, iconSize,
                              SMALL_FONT_ID);
      } else {
        drawHintLabel(renderer, SMALL_FONT_ID, labels[i], x, buttonWidth, pageHeight - buttonY, buttonHeight,
                      textYOffset);
      }
    } else {
      // Draw the filled background and border for a SMALL-sized button
      renderer.fillRoundedRect(x, pageHeight - smallButtonHeight, buttonWidth, smallButtonHeight, cornerRadius,
                               Color::White);
      renderer.drawRoundedRect(x, pageHeight - smallButtonHeight, buttonWidth, smallButtonHeight, 1, cornerRadius, true,
                               true, false, false, true);
    }
  }

  renderer.setOrientation(orig_orientation);
}

void LyraTheme::drawIconButtonHints(GfxRenderer& renderer, const ButtonHint& btn1, const ButtonHint& btn2,
                                    const ButtonHint& btn3, const ButtonHint& btn4) const {
  if (gpio.hasTouch()) {
    return;
  }

  const GfxRenderer::Orientation originalOrientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  const int pageHeight = renderer.getScreenHeight();
  constexpr int buttonWidth = 80;
  constexpr int smallButtonHeight = 15;
  constexpr int buttonHeight = LyraMetrics::values.buttonHintsHeight;
  constexpr int buttonY = LyraMetrics::values.buttonHintsHeight;
  constexpr int iconSize = 24;
  constexpr int narrowButtonPositions[] = {58, 146, 254, 342};
  constexpr int wideButtonPositions[] = {65, 157, 291, 383};
  const int* buttonPositions = renderer.getScreenWidth() >= 528 ? wideButtonPositions : narrowButtonPositions;
  const ButtonHint hints[] = {btn1, btn2, btn3, btn4};

  for (int i = 0; i < 4; i++) {
    const int x = buttonPositions[i];
    if (getButtonHintContentWidth(renderer, hints[i], iconSize, SMALL_FONT_ID) > 0) {
      const int y = pageHeight - buttonY;
      renderer.fillRoundedRect(x, y, buttonWidth, buttonHeight, cornerRadius, Color::White);
      renderer.drawRoundedRect(x, y, buttonWidth, buttonHeight, 1, cornerRadius, true, true, false, false, true);
      drawButtonHintContent(renderer, hints[i], x + buttonWidth / 2, y + buttonHeight / 2, iconSize, SMALL_FONT_ID);
    } else {
      const int y = pageHeight - smallButtonHeight;
      renderer.fillRoundedRect(x, y, buttonWidth, smallButtonHeight, cornerRadius, Color::White);
      renderer.drawRoundedRect(x, y, buttonWidth, smallButtonHeight, 1, cornerRadius, true, true, false, false, true);
    }
  }

  renderer.setOrientation(originalOrientation);
}

void LyraTheme::drawSideButtonHints(const GfxRenderer& renderer, const char* topBtn, const char* bottomBtn) const {
  if (gpio.hasTouch()) {
    return;
  }

  const int screenWidth = renderer.getScreenWidth();
  constexpr int buttonWidth = LyraMetrics::values.sideButtonHintsWidth;  // Width on screen (height when rotated)
  constexpr int buttonHeight = 78;                                       // Height on screen (width when rotated)
  constexpr int buttonMargin = 0;
  constexpr int iconSize = 24;
  const ButtonHint hints[] = {sideButtonHintFromLabel(topBtn), sideButtonHintFromLabel(bottomBtn)};
  const auto hasHint = [](const ButtonHint& hint) {
    return hint.icon != None || (hint.label != nullptr && hint.label[0] != '\0');
  };
  const auto drawHint = [&](const ButtonHint& hint, const int x, const int y) {
    if (hint.icon != None) {
      drawUIIcon(renderer, hint.icon, x + (buttonWidth - iconSize) / 2, y + (buttonHeight - iconSize) / 2, iconSize);
      return;
    }
    const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, hint.label);
    renderer.drawTextRotated90CW(SMALL_FONT_ID, x, y + (buttonHeight + textWidth) / 2, hint.label);
  };

  if (gpio.hasEdgeSideButtons()) {
    // Edge-button layout (X3, X4 Pro): Up on left side, Down on right side, positioned higher
    constexpr int x3ButtonY = 155;

    if (hasHint(hints[0])) {
      renderer.drawRoundedRect(buttonMargin, x3ButtonY, buttonWidth, buttonHeight, 1, cornerRadius, false, true, false,
                               true, true);
      drawHint(hints[0], buttonMargin, x3ButtonY);
    }

    if (hasHint(hints[1])) {
      const int rightX = screenWidth - buttonWidth;
      renderer.drawRoundedRect(rightX, x3ButtonY, buttonWidth, buttonHeight, 1, cornerRadius, true, false, true, false,
                               true);
      drawHint(hints[1], rightX, x3ButtonY);
    }
  } else {
    // X4 layout: Both buttons stacked on right side
    const int x = screenWidth - buttonWidth;

    if (hasHint(hints[0])) {
      renderer.drawRoundedRect(x, topHintButtonY, buttonWidth, buttonHeight, 1, cornerRadius, true, false, true, false,
                               true);
    }

    if (hasHint(hints[1])) {
      renderer.drawRoundedRect(x, topHintButtonY + buttonHeight + 5, buttonWidth, buttonHeight, 1, cornerRadius, true,
                               false, true, false, true);
    }

    for (int i = 0; i < 2; i++) {
      if (hasHint(hints[i])) {
        const int y = topHintButtonY + (i * buttonHeight) + 5;
        drawHint(hints[i], x, y);
      }
    }
  }
}

void LyraTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                    const int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                    bool& bufferRestored, std::function<bool()> storeCoverBuffer) const {
  (void)selectorIndex;
  (void)bufferRestored;
  if (recentBooks.empty()) {
    drawEmptyRecents(renderer, rect);
    return;
  }

  const RecentBook& book = recentBooks.front();
  const int coverX = rect.x + homeCoverSidePadding;
  const int coverHeight = rect.height;
  int localCoverWidth = std::max(1, coverHeight * 2 / 3);
  bool hasCover = false;

  if (!coverRendered && !book.coverBmpPath.empty()) {
    const std::string coverPath = UITheme::getCoverThumbPath(book.coverBmpPath, LyraMetrics::values.homeCoverHeight);
    HalFile file;
    if (Storage.openFileForRead("HOME", coverPath, file)) {
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getWidth() > 0 && bitmap.getHeight() > 0) {
        localCoverWidth =
            std::max(1, static_cast<int>(bitmap.getWidth() * static_cast<float>(coverHeight) / bitmap.getHeight()));
        localCoverWidth = std::min(localCoverWidth, std::max(1, rect.width * 2 / 3 - homeCoverSidePadding));
        renderer.drawBitmap(bitmap, coverX, rect.y, localCoverWidth, coverHeight);
        hasCover = true;
      }
      file.close();
    }
  }

  if (!coverRendered) {
    if (!hasCover) {
      renderer.fillRect(coverX, rect.y + coverHeight / 3, localCoverWidth, coverHeight * 2 / 3, true);
      renderer.drawIcon(CoverIcon, coverX + 24, rect.y + 24, 32);
    }
    coverWidth = localCoverWidth;
    coverBufferStored = storeCoverBuffer();
    coverRendered = coverBufferStored;
  }

  // The cached cover width is needed after the cover bitmap is restored from
  // the framebuffer, when there is no SD-card read to recalculate it.
  const int textX = coverX + coverWidth + 14;
  const int textRight = rect.x + rect.width - LyraMetrics::values.contentSidePadding;
  const int textWidth = std::max(1, textRight - textX);
  const int titleY = rect.y + 7;
  const auto title = renderer.truncatedText(UI_12_FONT_ID, book.title.c_str(), textWidth, EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, textX, titleY, title.c_str(), true, EpdFontFamily::BOLD);

  const int authorY = titleY + renderer.getLineHeight(UI_12_FONT_ID) + 5;
  if (!book.author.empty()) {
    const auto author = renderer.truncatedText(UI_10_FONT_ID, book.author.c_str(), textWidth);
    renderer.drawText(UI_10_FONT_ID, textX, authorY, author.c_str());
  }

  const int seriesY = authorY + renderer.getLineHeight(UI_10_FONT_ID) + 5;
  if (!book.series.empty()) {
    const auto series = renderer.truncatedText(SMALL_FONT_ID, book.series.c_str(), textWidth);
    renderer.drawText(SMALL_FONT_ID, textX, seriesY, series.c_str());
  }

  if (book.started) {
    const std::string progressText = std::to_string(book.progressPercent) + "%";
    const int progressY = rect.y + rect.height - 7 - renderer.getLineHeight(SMALL_FONT_ID);
    if (book.remainingReadingSeconds > 0) {
      char duration[24];
      char remaining[48];
      ReadingStats::formatDuration(book.remainingReadingSeconds, duration, sizeof(duration));
      snprintf(remaining, sizeof(remaining), tr(STR_HOME_TIME_REMAINING), duration);
      renderer.drawText(SMALL_FONT_ID, textX, progressY - renderer.getLineHeight(SMALL_FONT_ID) - 4, remaining);
    }
    renderer.drawText(SMALL_FONT_ID, textX, progressY, progressText.c_str());
    const int labelWidth = renderer.getTextWidth(SMALL_FONT_ID, progressText.c_str());
    const int barX = textX + labelWidth + 8;
    const int barRight = textRight - 3;
    const int barWidth = std::max(0, barRight - barX);
    const int barY = progressY + renderer.getLineHeight(SMALL_FONT_ID) / 2 - 2;
    if (barWidth > 0) {
      renderer.drawRect(barX, barY, barWidth, 5);
      const int fillWidth = std::max(0, (barWidth - 2) * book.progressPercent / 100);
      if (fillWidth > 0) {
        renderer.fillRect(barX + 1, barY + 1, fillWidth, 3);
      }
    }
  }
}

void LyraTheme::drawEmptyRecents(const GfxRenderer& renderer, const Rect rect) const {
  constexpr int padding = 48;
  renderer.drawText(UI_12_FONT_ID, rect.x + padding,
                    rect.y + rect.height / 2 - renderer.getLineHeight(UI_12_FONT_ID) - 2, tr(STR_NO_OPEN_BOOK), true,
                    EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, rect.x + padding, rect.y + rect.height / 2 + 2, tr(STR_START_READING), true);
}

void LyraTheme::drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                               const std::function<std::string(int index)>& buttonLabel,
                               const std::function<UIIcon(int index)>& rowIcon) const {
  for (int i = 0; i < buttonCount; ++i) {
    int tileWidth = rect.width - LyraMetrics::values.contentSidePadding * 2;
    Rect tileRect = Rect{rect.x + LyraMetrics::values.contentSidePadding,
                         rect.y + i * (LyraMetrics::values.menuRowHeight + LyraMetrics::values.menuSpacing), tileWidth,
                         LyraMetrics::values.menuRowHeight};

    const bool selected = selectedIndex == i;

    if (selected) {
      renderer.fillRoundedRect(tileRect.x, tileRect.y, tileRect.width, tileRect.height, cornerRadius, Color::LightGray);
    }

    std::string labelStr = buttonLabel(i);
    const char* label = labelStr.c_str();
    int textX = tileRect.x + 16;
    const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
    const int textY = tileRect.y + (LyraMetrics::values.menuRowHeight - lineHeight) / 2;

    if (rowIcon != nullptr) {
      UIIcon icon = rowIcon(i);
      const uint8_t* iconBitmap = getUIIconBitmap(icon, mainMenuIconSize);
      if (iconBitmap != nullptr) {
        drawUIIcon(renderer, icon, textX, textY, mainMenuIconSize);
        textX += mainMenuIconSize + hPaddingInSelection + 2;
      }
    }

    renderer.drawText(UI_12_FONT_ID, textX, textY, label, true);
  }
}
