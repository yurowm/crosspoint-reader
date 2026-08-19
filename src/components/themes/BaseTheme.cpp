#include "BaseTheme.h"

#include <FreeInkUIGfxRenderer.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>

#include "I18n.h"
#include "ReadingStats.h"
#include "RecentBooksStore.h"
#include "components/UIScale.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "components/UiAppHelpers.h"
#include "components/icons/book.h"
#include "components/icons/book24.h"
#include "components/icons/bookmark.h"
#include "components/icons/checkCheck20.h"
#include "components/icons/file24.h"
#include "components/icons/folder.h"
#include "components/icons/folder24.h"
#include "components/icons/hotspot.h"
#include "components/icons/image24.h"
#include "components/icons/statistics.h"
#include "components/icons/text24.h"
#include "components/icons/ui_icons_generated.h"
#include "components/icons/wifi.h"
#include "fontIds.h"

// Internal constants
namespace {
constexpr int homeMenuMargin = 20;
constexpr int homeMarginTop = 30;
constexpr int subtitleY = 738;
constexpr int bookmarkStatusIconWidth = 16;
constexpr int bookmarkStatusIconHeight = 14;
constexpr int bookmarkStatusIconGap = 4;
constexpr int bookmarkStatusIconTopCrop = 2;
int homeCoverWidth = 0;

void drawBookmarkStatusIcon(const GfxRenderer& renderer, const int x, const int y) {
  constexpr int bytesPerRow = bookmarkStatusIconWidth / 8;
  for (int row = 0; row < bookmarkStatusIconHeight; ++row) {
    for (int col = 0; col < bookmarkStatusIconWidth; ++col) {
      const uint8_t byte = BookmarkStatusIcon[(row + bookmarkStatusIconTopCrop) * bytesPerRow + col / 8];
      const uint8_t mask = 1U << (7 - (col % 8));
      renderer.drawPixel(x + col, y + row, (byte & mask) != 0);
    }
  }
}

}  // namespace

const uint8_t* getUIIconBitmap(const UIIcon icon, const int size) {
  if (size == 20) {
    switch (icon) {
      case ContinueReading:
        return icon_continue_reading_20.bits;
      case Statistics:
        return icon_statistics_20.bits;
      case Library:
        return icon_menu_library_20.bits;
      case Recent:
        return icon_recent_20.bits;
      case Deferred:
        return icon_deferred_20.bits;
      case Transfer:
        return icon_transfer_20.bits;
      case Settings:
        return icon_settings_20.bits;
      case ChevronUp:
        return icon_chevron_up_20.bits;
      case ChevronDown:
        return icon_chevron_down_20.bits;
      case Check:
        return icon_check_20.bits;
      case CheckCheck:
        return CheckCheck20Icon;
      case House:
        return icon_house_20.bits;
      case Filters:
        return icon_filters_20.bits;
      case NavigateBack:
        return icon_navigate_back_20.bits;
      case SwitchTabs:
        return icon_switch_tabs_20.bits;
      case Plus:
        return icon_plus_20.bits;
      case Minus:
        return icon_minus_20.bits;
      case Trash:
        return icon_trash_20.bits;
      case Refresh:
        return icon_refresh_20.bits;
      case Cancel:
        return icon_cancel_20.bits;
      case Exit:
        return icon_exit_20.bits;
      case Search:
        return icon_search_20.bits;
      case ChevronLeft:
        return icon_chevron_left_20.bits;
      case ChevronRight:
        return icon_chevron_right_20.bits;
      case ReadBook:
        return icon_read_book_20.bits;
      case DeferredOff:
        return icon_deferred_off_20.bits;
      case MarkRead:
        return icon_mark_read_20.bits;
      case MarkUnread:
        return icon_mark_unread_20.bits;
      case BookCover:
        return icon_book_cover_20.bits;
      default:
        return nullptr;
    }
  }

  if (size == 24) {
    switch (icon) {
      case Folder:
        return Folder24Icon;
      case Text:
        return Text24Icon;
      case Image:
        return Image24Icon;
      case Book:
        return Book24Icon;
      case Statistics:
        return icon_statistics_24.bits;
      case File:
        return File24Icon;
      case ContinueReading:
        return icon_continue_reading_24.bits;
      case Library:
        return icon_menu_library_24.bits;
      case Recent:
        return icon_recent_24.bits;
      case Deferred:
        return icon_deferred_24.bits;
      case Transfer:
        return icon_transfer_24.bits;
      case Settings:
        return icon_settings_24.bits;
      case ChevronUp:
        return icon_chevron_up_24.bits;
      case ChevronDown:
        return icon_chevron_down_24.bits;
      case Check:
        return icon_check_24.bits;
      case House:
        return icon_house_24.bits;
      case Filters:
        return icon_filters_24.bits;
      case NavigateBack:
        return icon_navigate_back_24.bits;
      case SwitchTabs:
        return icon_switch_tabs_24.bits;
      case Plus:
        return icon_plus_24.bits;
      case Minus:
        return icon_minus_24.bits;
      case Trash:
        return icon_trash_24.bits;
      case Refresh:
        return icon_refresh_24.bits;
      case Cancel:
        return icon_cancel_24.bits;
      case Exit:
        return icon_exit_24.bits;
      case Search:
        return icon_search_24.bits;
      case ChevronLeft:
        return icon_chevron_left_24.bits;
      case ChevronRight:
        return icon_chevron_right_24.bits;
      case ReadBook:
        return icon_read_book_24.bits;
      case DeferredOff:
        return icon_deferred_off_24.bits;
      case MarkRead:
        return icon_mark_read_24.bits;
      case MarkUnread:
        return icon_mark_unread_24.bits;
      case BookCover:
        return icon_book_cover_24.bits;
      default:
        return nullptr;
    }
  }

  if (size == 32) {
    switch (icon) {
      case Folder:
        return FolderIcon;
      case Book:
        return BookIcon;
      case Statistics:
        return icon_statistics_32.bits;
      case Wifi:
        return WifiIcon;
      case Hotspot:
        return HotspotIcon;
      case Bookmark:
        return BookmarkIcon;
      case ContinueReading:
        return icon_continue_reading_32.bits;
      case Library:
        return icon_menu_library_32.bits;
      case Recent:
        return icon_recent_32.bits;
      case Deferred:
        return icon_deferred_32.bits;
      case Transfer:
        return icon_transfer_32.bits;
      case Settings:
        return icon_settings_32.bits;
      case ChevronUp:
        return icon_chevron_up_32.bits;
      case ChevronDown:
        return icon_chevron_down_32.bits;
      case Check:
        return icon_check_32.bits;
      case House:
        return icon_house_32.bits;
      case Filters:
        return icon_filters_32.bits;
      case NavigateBack:
        return icon_navigate_back_32.bits;
      case SwitchTabs:
        return icon_switch_tabs_32.bits;
      case Plus:
        return icon_plus_32.bits;
      case Minus:
        return icon_minus_32.bits;
      case Trash:
        return icon_trash_32.bits;
      case Refresh:
        return icon_refresh_32.bits;
      case Cancel:
        return icon_cancel_32.bits;
      case Exit:
        return icon_exit_32.bits;
      case Search:
        return icon_search_32.bits;
      case ChevronLeft:
        return icon_chevron_left_32.bits;
      case ChevronRight:
        return icon_chevron_right_32.bits;
      case ReadBook:
        return icon_read_book_32.bits;
      case DeferredOff:
        return icon_deferred_off_32.bits;
      case MarkRead:
        return icon_mark_read_32.bits;
      case MarkUnread:
        return icon_mark_unread_32.bits;
      case BookCover:
        return icon_book_cover_32.bits;
      default:
        return nullptr;
    }
  }

  return nullptr;
}

void drawUIIcon(const GfxRenderer& renderer, const UIIcon icon, const int x, const int y, const int size,
                const bool state) {
  const uint8_t* bitmap = getUIIconBitmap(icon, size);
  if (bitmap == nullptr) {
    return;
  }

  switch (icon) {
    case Recent:
    case Statistics:
    case Deferred:
    case Settings:
    case Transfer:
    case Library:
    case ContinueReading:
    case ChevronUp:
    case ChevronDown:
    case Check:
    case CheckCheck:
    case House:
    case Filters:
    case NavigateBack:
    case SwitchTabs:
    case Plus:
    case Minus:
    case Trash:
    case Refresh:
    case Cancel:
    case Exit:
    case Search:
    case ChevronLeft:
    case ChevronRight:
    case ReadBook:
    case DeferredOff:
    case MarkRead:
    case MarkUnread:
    case BookCover:
      renderer.drawNativeIcon(bitmap, x, y, size, state);
      break;
    default:
      renderer.drawIcon(bitmap, x, y, size, state);
      break;
  }
}

ButtonHint buttonHintFromLabel(const char* label, const uint8_t hardwareButton) {
  if (label == nullptr || label[0] == '\0') {
    return {};
  }
  // Specific commands override the generic confirm glyph even when they happen to use
  // the configured Confirm button on a particular screen.
  if (std::strcmp(label, "+") == 0) {
    return {.icon = Plus};
  }
  if (std::strcmp(label, "-") == 0) {
    return {.icon = Minus};
  }
  if (std::strcmp(label, tr(STR_FORGET_BUTTON)) == 0) {
    return {.icon = Trash};
  }
  if (std::strcmp(label, tr(STR_RETRY)) == 0) {
    return {.icon = Refresh};
  }
  if (std::strcmp(label, tr(STR_CANCEL)) == 0) {
    return {.icon = Cancel};
  }
  if (std::strcmp(label, tr(STR_EXIT)) == 0) {
    return {.icon = Exit};
  }
  if (std::strcmp(label, tr(STR_HOME)) == 0) {
    return {.icon = House};
  }
  if (std::strcmp(label, tr(STR_SEARCH)) == 0) {
    return {.icon = Search};
  }
  if (std::strcmp(label, tr(STR_DIR_LEFT)) == 0 || std::strcmp(label, "<") == 0) {
    return {.icon = ChevronLeft};
  }
  if (std::strcmp(label, tr(STR_DIR_RIGHT)) == 0 || std::strcmp(label, ">") == 0) {
    return {.icon = ChevronRight};
  }
  // The confirm label is contextual (Select, Open, Toggle, Retry, a settings category, ...),
  // so identify it by the configured physical button instead of translated label text.
  if (hardwareButton == SETTINGS.frontButtonConfirm) {
    return {.icon = Check};
  }
  if (std::strcmp(label, tr(STR_DIR_UP)) == 0) {
    return {.icon = ChevronUp};
  }
  if (std::strcmp(label, tr(STR_DIR_DOWN)) == 0) {
    return {.icon = ChevronDown};
  }
  if (std::strcmp(label, tr(STR_BACK)) == 0) {
    return {.icon = NavigateBack};
  }
  return {.label = label};
}

ButtonHint sideButtonHintFromLabel(const char* label) {
  if (label == nullptr || label[0] == '\0') {
    return {};
  }
  if (std::strcmp(label, ">") == 0) {
    return {.icon = ChevronRight};
  }
  if (std::strcmp(label, "<") == 0) {
    return {.icon = ChevronLeft};
  }
  return {.label = label};
}

void BaseTheme::drawBatteryOutline(const GfxRenderer& renderer, int x, int y, int battWidth, int rectHeight) {
  // Top line
  renderer.drawLine(x + 1, y, x + battWidth - 3, y);
  // Bottom line
  renderer.drawLine(x + 1, y + rectHeight - 1, x + battWidth - 3, y + rectHeight - 1);
  // Left line
  renderer.drawLine(x, y + 1, x, y + rectHeight - 2);
  // Battery end
  renderer.drawLine(x + battWidth - 2, y + 1, x + battWidth - 2, y + rectHeight - 2);
  renderer.drawPixel(x + battWidth - 1, y + 3);
  renderer.drawPixel(x + battWidth - 1, y + rectHeight - 4);
  renderer.drawLine(x + battWidth - 0, y + 4, x + battWidth - 0, y + rectHeight - 5);
}

void BaseTheme::drawBatteryLightningBolt(const GfxRenderer& renderer, int boltX, int boltY) {
  // Draw lightning bolt (white/inverted on black fill for visibility)
  renderer.drawLine(boltX + 4, boltY + 0, boltX + 5, boltY + 0, false);
  renderer.drawLine(boltX + 3, boltY + 1, boltX + 4, boltY + 1, false);
  renderer.drawLine(boltX + 2, boltY + 2, boltX + 5, boltY + 2, false);
  renderer.drawLine(boltX + 3, boltY + 3, boltX + 4, boltY + 3, false);
  renderer.drawLine(boltX + 2, boltY + 4, boltX + 3, boltY + 4, false);
  renderer.drawLine(boltX + 1, boltY + 5, boltX + 4, boltY + 5, false);
  renderer.drawLine(boltX + 2, boltY + 6, boltX + 3, boltY + 6, false);
  renderer.drawLine(boltX + 1, boltY + 7, boltX + 2, boltY + 7, false);
}

void BaseTheme::fillBatteryIcon(const GfxRenderer& renderer, Rect rect, uint16_t percentage) const {
  const bool charging = gpio.isUsbConnected();

  const int maxFillWidth = rect.width - 5;
  const int fillHeight = rect.height - 4;
  if (maxFillWidth <= 0 || fillHeight <= 0) {
    return;
  }
  // +1 to round up so we always fill at least one pixel
  int filledWidth = percentage * maxFillWidth / 100 + 1;
  if (filledWidth > maxFillWidth) {
    filledWidth = maxFillWidth;
  }

  // When charging, ensure minimum fill so lightning bolt is fully visible
  constexpr int minFillForBolt = 8;
  if (charging && filledWidth < minFillForBolt) {
    filledWidth = std::min(minFillForBolt, maxFillWidth);
  }

  renderer.fillRect(rect.x + 2, rect.y + 2, filledWidth, fillHeight);

  if (charging) {
    drawBatteryLightningBolt(renderer, rect.x + 4, rect.y + 2);
  }
}

void BaseTheme::drawBatteryLeft(const GfxRenderer& renderer, Rect rect, const bool showPercentage) const {
  // Left aligned: icon on left, percentage on right (reader mode)
  const uint16_t percentage = powerManager.getBatteryPercentage();
  const int y = rect.y + 6;

  if (showPercentage) {
    const auto percentageText = std::to_string(percentage) + "%";
    renderer.drawText(SMALL_FONT_ID, rect.x + batteryPercentSpacing + rect.width, rect.y, percentageText.c_str());
  }

  const Rect iconRect{rect.x, y, rect.width, rect.height};
  drawBatteryOutline(renderer, rect.x, y, rect.width, rect.height);
  fillBatteryIcon(renderer, iconRect, percentage);
}

void BaseTheme::drawProgressBar(const GfxRenderer& renderer, Rect rect, const size_t current,
                                const size_t total) const {
  if (total == 0) {
    return;
  }

  // Use 64-bit arithmetic to avoid overflow for large files
  const int percent = static_cast<int>((static_cast<uint64_t>(current) * 100) / total);

  LOG_DBG("UI", "Drawing progress bar: current=%u, total=%u, percent=%d", current, total, percent);
  // Draw outline
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height);

  // Draw filled portion
  const int fillWidth = (rect.width - 4) * percent / 100;
  if (fillWidth > 0) {
    renderer.fillRect(rect.x + 2, rect.y + 2, fillWidth, rect.height - 4);
  }

  // Draw percentage text centered below bar
  const std::string percentText = std::to_string(percent) + "%";
  renderer.drawCenteredText(UI_10_FONT_ID, rect.y + rect.height + 15, percentText.c_str());
}

// Centre a button-hint label inside its box. A label that fits is drawn on the
// single baseline it always was; one too wide used to overflow the button border
// and run into the neighbouring hint, and now wraps to at most two centred lines
// (wrappedText() ellipsises anything that still doesn't fit). Shared so every
// theme's drawButtonHints() gets the same behaviour.
void BaseTheme::drawHintLabel(GfxRenderer& renderer, const int fontId, const char* label, const int x,
                              const int boxWidth, const int boxTop, const int boxHeight, const int singleLineYOffset) {
  constexpr int textPadding = 4;  // keeps a wrapped label off the button's border
  const int maxTextWidth = boxWidth - (textPadding * 2);

  const int textWidth = renderer.getTextWidth(fontId, label);
  if (textWidth <= maxTextWidth) {
    renderer.drawText(fontId, x + (boxWidth - 1 - textWidth) / 2, boxTop + singleLineYOffset, label);
    return;
  }

  // Spaced by the glyph height, not getLineHeight() — that returns the font's
  // full advanceY (leading included), which stacks two lines taller than the
  // button and clips the second one.
  constexpr int lineGap = 2;
  const int step = renderer.getTextHeight(fontId) + lineGap;
  const auto lines = renderer.wrappedText(fontId, label, maxTextWidth, 2);
  const int block = static_cast<int>(lines.size()) * step - lineGap;
  int lineY = boxTop + std::max(1, (boxHeight - block) / 2);
  for (const auto& line : lines) {
    const int lineWidth = renderer.getTextWidth(fontId, line.c_str());
    renderer.drawText(fontId, x + (boxWidth - 1 - lineWidth) / 2, lineY, line.c_str());
    lineY += step;
  }
}

int BaseTheme::getButtonHintContentWidth(const GfxRenderer& renderer, const ButtonHint& hint, const int iconSize,
                                         const int fontId) {
  constexpr int contentGap = 5;
  int width = 0;

  if (hint.icon != None && getUIIconBitmap(hint.icon, iconSize) != nullptr) {
    width += iconSize;
  }
  if (hint.holdIcon != None && getUIIconBitmap(hint.holdIcon, iconSize) != nullptr) {
    const int separatorWidth = renderer.getTextWidth(fontId, "/");
    width += (width > 0 ? contentGap : 0) + separatorWidth + contentGap + iconSize;
  }
  if (hint.label != nullptr && hint.label[0] != '\0') {
    width += (width > 0 ? contentGap : 0) + renderer.getTextWidth(fontId, hint.label);
  }
  return width;
}

void BaseTheme::drawButtonHintContent(GfxRenderer& renderer, const ButtonHint& hint, const int centerX,
                                      const int centerY, const int iconSize, const int fontId, const bool state) {
  constexpr int contentGap = 5;
  int x = centerX - getButtonHintContentWidth(renderer, hint, iconSize, fontId) / 2;
  const int iconY = centerY - iconSize / 2;
  const int textY = centerY - renderer.getLineHeight(fontId) / 2;

  if (const uint8_t* icon = getUIIconBitmap(hint.icon, iconSize); icon != nullptr) {
    drawUIIcon(renderer, hint.icon, x, iconY, iconSize, state);
    x += iconSize;
  }

  if (const uint8_t* holdIcon = getUIIconBitmap(hint.holdIcon, iconSize); holdIcon != nullptr) {
    if (x != centerX - getButtonHintContentWidth(renderer, hint, iconSize, fontId) / 2) {
      x += contentGap;
    }
    renderer.drawText(fontId, x, textY, "/", state);
    x += renderer.getTextWidth(fontId, "/") + contentGap;
    drawUIIcon(renderer, hint.holdIcon, x, iconY, iconSize, state);
    x += iconSize;
  }

  if (hint.label != nullptr && hint.label[0] != '\0') {
    if (x != centerX - getButtonHintContentWidth(renderer, hint, iconSize, fontId) / 2) {
      x += contentGap;
    }
    renderer.drawText(fontId, x, textY, hint.label, state);
  }
}

void BaseTheme::drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                                const char* btn4) const {
  if (gpio.hasTouch()) {
    return;
  }

  const GfxRenderer::Orientation orig_orientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  const int pageHeight = renderer.getScreenHeight();
  constexpr int buttonWidth = 106;
  constexpr int buttonHeight = BaseMetrics::values.buttonHintsHeight;
  constexpr int buttonY = BaseMetrics::values.buttonHintsHeight;  // Distance from bottom
  constexpr int textYOffset = 7;                                  // Distance from top of button to text baseline
  constexpr int iconSize = 24;
  // Keyed to the portrait panel width: the 528-wide X3 gets more spacing than
  // the 480-wide boards (X4, X4 Pro, and the other 800x480 panels).
  constexpr int narrowButtonPositions[] = {25, 130, 245, 350};
  constexpr int wideButtonPositions[] = {38, 154, 268, 384};
  const int* buttonPositions = renderer.getScreenWidth() >= 528 ? wideButtonPositions : narrowButtonPositions;
  const char* labels[] = {btn1, btn2, btn3, btn4};
  constexpr uint8_t hardwareButtons[] = {HalGPIO::BTN_BACK, HalGPIO::BTN_CONFIRM, HalGPIO::BTN_LEFT,
                                         HalGPIO::BTN_RIGHT};

  for (int i = 0; i < 4; i++) {
    const ButtonHint hint = buttonHintFromLabel(labels[i], hardwareButtons[i]);
    if (getButtonHintContentWidth(renderer, hint, iconSize, UI_10_FONT_ID) > 0) {
      const int x = buttonPositions[i];
      renderer.fillRect(x, pageHeight - buttonY, buttonWidth, buttonHeight, false);
      renderer.drawRect(x, pageHeight - buttonY, buttonWidth, buttonHeight);
      if (hint.icon != None) {
        drawButtonHintContent(renderer, hint, x + buttonWidth / 2, pageHeight - buttonY + buttonHeight / 2, iconSize,
                              UI_10_FONT_ID);
      } else {
        drawHintLabel(renderer, UI_10_FONT_ID, labels[i], x, buttonWidth, pageHeight - buttonY, buttonHeight,
                      textYOffset);
      }
    }
  }

  renderer.setOrientation(orig_orientation);
}

void BaseTheme::drawIconButtonHints(GfxRenderer& renderer, const ButtonHint& btn1, const ButtonHint& btn2,
                                    const ButtonHint& btn3, const ButtonHint& btn4) const {
  if (gpio.hasTouch()) {
    return;
  }

  const GfxRenderer::Orientation originalOrientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  const int pageHeight = renderer.getScreenHeight();
  constexpr int buttonWidth = 106;
  constexpr int buttonHeight = BaseMetrics::values.buttonHintsHeight;
  constexpr int buttonY = BaseMetrics::values.buttonHintsHeight;
  constexpr int iconSize = 24;
  constexpr int narrowButtonPositions[] = {25, 130, 245, 350};
  constexpr int wideButtonPositions[] = {38, 154, 268, 384};
  const int* buttonPositions = renderer.getScreenWidth() >= 528 ? wideButtonPositions : narrowButtonPositions;
  const ButtonHint hints[] = {btn1, btn2, btn3, btn4};

  for (int i = 0; i < 4; i++) {
    if (getButtonHintContentWidth(renderer, hints[i], iconSize, UI_10_FONT_ID) == 0) {
      continue;
    }
    const int x = buttonPositions[i];
    const int y = pageHeight - buttonY;
    renderer.fillRect(x, y, buttonWidth, buttonHeight, false);
    renderer.drawRect(x, y, buttonWidth, buttonHeight);
    drawButtonHintContent(renderer, hints[i], x + buttonWidth / 2, y + buttonHeight / 2, iconSize, UI_10_FONT_ID);
  }

  renderer.setOrientation(originalOrientation);
}

void BaseTheme::drawSideButtonHints(const GfxRenderer& renderer, const char* topBtn, const char* bottomBtn) const {
  if (gpio.hasTouch()) {
    return;
  }

  const int screenWidth = renderer.getScreenWidth();
  constexpr int buttonWidth = BaseMetrics::values.sideButtonHintsWidth;  // Width on screen (height when rotated)
  constexpr int buttonHeight = 80;                                       // Height on screen (width when rotated)
  constexpr int buttonMargin = 4;
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
    const int textHeight = renderer.getTextHeight(SMALL_FONT_ID);
    const int textX = x + (buttonWidth - textHeight) / 2;
    const int textY = y + (buttonHeight + textWidth) / 2;
    renderer.drawTextRotated90CW(SMALL_FONT_ID, textX, textY, hint.label);
  };

  if (gpio.hasEdgeSideButtons()) {
    // Edge-button layout (X3, X4 Pro): Up on left side, Down on right side, positioned higher
    constexpr int x3ButtonY = 155;

    if (hasHint(hints[0])) {
      const int leftX = buttonMargin;
      renderer.drawRect(leftX, x3ButtonY, buttonWidth, buttonHeight);
      drawHint(hints[0], leftX, x3ButtonY);
    }

    if (hasHint(hints[1])) {
      const int rightX = screenWidth - buttonMargin - buttonWidth;
      renderer.drawRect(rightX, x3ButtonY, buttonWidth, buttonHeight);
      drawHint(hints[1], rightX, x3ButtonY);
    }
  } else {
    // X4 layout: Both buttons stacked on right side
    constexpr int topButtonY = 345;
    const int x = screenWidth - buttonMargin - buttonWidth;

    if (hasHint(hints[0])) {
      renderer.drawLine(x, topButtonY, x + buttonWidth - 1, topButtonY);
      renderer.drawLine(x, topButtonY, x, topButtonY + buttonHeight - 1);
      renderer.drawLine(x + buttonWidth - 1, topButtonY, x + buttonWidth - 1, topButtonY + buttonHeight - 1);
    }

    if (hasHint(hints[0]) || hasHint(hints[1])) {
      renderer.drawLine(x, topButtonY + buttonHeight, x + buttonWidth - 1, topButtonY + buttonHeight);
    }

    if (hasHint(hints[1])) {
      renderer.drawLine(x, topButtonY + buttonHeight, x, topButtonY + 2 * buttonHeight - 1);
      renderer.drawLine(x + buttonWidth - 1, topButtonY + buttonHeight, x + buttonWidth - 1,
                        topButtonY + 2 * buttonHeight - 1);
      renderer.drawLine(x, topButtonY + 2 * buttonHeight - 1, x + buttonWidth - 1, topButtonY + 2 * buttonHeight - 1);
    }

    for (int i = 0; i < 2; i++) {
      if (hasHint(hints[i])) {
        const int y = topButtonY + i * buttonHeight;
        drawHint(hints[i], x, y);
      }
    }
  }
}

int BaseTheme::getListRowStep(bool hasSubtitle) const {
  int rowHeight = (hasSubtitle) ? BaseMetrics::values.listWithSubtitleRowHeight : BaseMetrics::values.listRowHeight;
  return rowHeight;
}

int BaseTheme::getListPageItems(int contentHeight, bool hasSubtitle) const {
  const int rowStep = getListRowStep(hasSubtitle);
  if (rowStep <= 0) return 1;
  return std::max(1, contentHeight / rowStep);
}

void BaseTheme::drawList(const GfxRenderer& renderer, Rect rect, int itemCount, int selectedIndex,
                         const std::function<std::string(int index)>& rowTitle,
                         const std::function<std::string(int index)>& rowSubtitle,
                         const std::function<UIIcon(int index)>& rowIcon,
                         const std::function<std::string(int index)>& rowValue, bool highlightValue,
                         const std::function<bool(int index)>& rowDimmed) const {
  int rowHeight =
      (rowSubtitle != nullptr) ? BaseMetrics::values.listWithSubtitleRowHeight : BaseMetrics::values.listRowHeight;
  int pageItems = rowHeight > 0 ? std::max(1, rect.height / rowHeight) : 1;

  const int totalPages = (itemCount + pageItems - 1) / pageItems;
  if (totalPages > 1) {
    constexpr int indicatorWidth = 20;
    constexpr int arrowSize = 6;
    constexpr int margin = 15;  // Offset from right edge

    const int centerX = rect.x + rect.width - indicatorWidth / 2 - margin;
    const int indicatorTop = rect.y;  // Offset to avoid overlapping side button hints
    const int indicatorBottom = rect.y + rect.height - arrowSize;

    // Draw up arrow at top (^) - narrow point at top, wide base at bottom
    for (int i = 0; i < arrowSize; ++i) {
      const int lineWidth = 1 + i * 2;
      const int startX = centerX - i;
      renderer.drawLine(startX, indicatorTop + i, startX + lineWidth - 1, indicatorTop + i);
    }

    // Draw down arrow at bottom (v) - wide base at top, narrow point at bottom
    for (int i = 0; i < arrowSize; ++i) {
      const int lineWidth = 1 + (arrowSize - 1 - i) * 2;
      const int startX = centerX - (arrowSize - 1 - i);
      renderer.drawLine(startX, indicatorBottom - arrowSize + 1 + i, startX + lineWidth - 1,
                        indicatorBottom - arrowSize + 1 + i);
    }
  }

  // Draw selection
  int contentWidth = rect.width - 5;
  if (selectedIndex >= 0) {
    renderer.fillRect(rect.x, rect.y + selectedIndex % pageItems * rowHeight - 2, rect.width, rowHeight);
  }
  constexpr int minValueGap = 10;

  // Draw all items
  const auto pageStartIndex = selectedIndex / pageItems * pageItems;
  for (int i = pageStartIndex; i < itemCount && i < pageStartIndex + pageItems; i++) {
    const int itemY = rect.y + (i % pageItems) * rowHeight;

    int rowTextWidth = contentWidth - BaseMetrics::values.contentSidePadding * 2;
    std::string valueText;
    if (rowValue != nullptr) {
      valueText = rowValue(i);
      if (!valueText.empty()) {
        int maxValW = std::max(0, rowTextWidth - 40 - minValueGap);
        valueText = renderer.truncatedText(UI_10_FONT_ID, valueText.c_str(), maxValW);
        int valueWidth = renderer.getTextWidth(UI_10_FONT_ID, valueText.c_str()) + minValueGap;
        rowTextWidth -= valueWidth;
      }
    }

    auto itemName = rowTitle(i);
    auto font = UI_10_FONT_ID;
    auto item = renderer.truncatedText(font, itemName.c_str(), rowTextWidth);
    renderer.drawText(font, rect.x + BaseMetrics::values.contentSidePadding, itemY, item.c_str(), i != selectedIndex);

    // Apply checkerboard dither to create gray text effect for dimmed items
    if (rowDimmed && rowDimmed(i) && i != selectedIndex) {
      const int titleWidth = renderer.getTextWidth(font, item.c_str());
      const int lineH = renderer.getLineHeight(font);
      const int tx = rect.x + BaseMetrics::values.contentSidePadding;
      for (int py = itemY; py < itemY + lineH; py++)
        for (int px = tx; px < tx + titleWidth; px++)
          if ((px + py) % 2 == 0) renderer.drawPixel(px, py, false);
    }

    if (rowSubtitle != nullptr) {
      std::string subtitleText = rowSubtitle(i);
      if (!subtitleText.empty()) {
        auto subtitle = renderer.truncatedText(SMALL_FONT_ID, subtitleText.c_str(), rowTextWidth);
        renderer.drawText(SMALL_FONT_ID, rect.x + BaseMetrics::values.contentSidePadding, itemY + 22, subtitle.c_str(),
                          i != selectedIndex);
      }
    }

    if (!valueText.empty()) {
      const auto valueTextWidth = renderer.getTextWidth(UI_10_FONT_ID, valueText.c_str());
      int valueY = itemY;
      if (rowSubtitle != nullptr) {
        valueY = itemY + 10;
      }
      renderer.drawText(UI_10_FONT_ID, rect.x + contentWidth - BaseMetrics::values.contentSidePadding - valueTextWidth,
                        valueY, valueText.c_str(), i != selectedIndex);
    }
  }
}

void BaseTheme::drawHeader(const GfxRenderer& renderer, Rect rect, const char* title, const char* subtitle) const {
  // Every activity header renders through the FreeInkUI header + battery
  // indicator components, styled by the active theme's tokens (padding,
  // centering, underline). Non-interactive frame: no hit rects registered.
  namespace fui = freeink::ui;
  const auto spec = uiScaleSpec();
  fui::GfxRendererFrame<1> ui(renderer, spec.smallFontId, spec.bodyFontId, spec.titleFontId);
  // Refresh the app-wide shared tokens instead of copying ~1.5KB of
  // ThemeTokens onto this render-path stack frame; the values derived here
  // are identical to what every FreeInkApp screen derives. Goes through the
  // same publish-a-fresh-slot path applySharedUiTheme() uses (see
  // UiAppHelpers.h) rather than overwriting the previously-published
  // instance in place, since some other FreeInkApp could be mid-read of it.
  const fui::ThemeTokens& tokens = refreshSharedUiThemeTokens(ui.target);
  // Header status text (battery percent, right label) stays at the fixed
  // small font like the legacy headers; the uiScale small font is for list
  // subtitles.
  ui.target.setFont(fui::GfxRendererTarget::FONT_SMALL, SMALL_FONT_ID);
  const ThemeMetrics& metrics = UITheme::getInstance().getMetrics();
  const fui::Rect band{static_cast<int16_t>(rect.x), static_cast<int16_t>(rect.y), static_cast<int16_t>(rect.width),
                       static_cast<int16_t>(rect.height)};

  const bool showBatteryPercentage =
      SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS;
  const uint16_t percentage = powerManager.getBatteryPercentage();
  char percentText[8];
  snprintf(percentText, sizeof(percentText), "%u%%", static_cast<unsigned>(percentage));
  // The icon glyph extends 2px past glyphWidth (terminal nub); reserve it or
  // the percent label's rect comes up short and the text truncates.
  constexpr int16_t batteryNubWidth = 2;
  int16_t batteryReserve = static_cast<int16_t>(metrics.batteryWidth + batteryNubWidth);
  if (showBatteryPercentage) {
    batteryReserve = static_cast<int16_t>(
        batteryReserve + batteryPercentSpacing +
        ui.target.measureText(fui::GfxRendererTarget::FONT_SMALL, percentText, tokens.smallText).width);
  }

  fui::HeaderProps props;
  props.title = title;
  props.rightLabel = subtitle;  // firmware headers right-align the secondary text
  const bool batteryLeft = metrics.headerBatterySide == 1;
  const bool batteryDetached = metrics.headerBatteryDetached;
  // Shared-line headers with the battery on the right: the header component
  // places rightLabel inside the battery reserve, so it sits mid-band next to
  // the icon and shifts with the percent label's width. Draw it manually below
  // instead, pinned at the fixed side inset in the band's lower half — the
  // same corner the detached (Lyra) layout puts it — so the label holds one
  // position across themes and battery states.
  const bool manualRightLabel = subtitle != nullptr && !batteryDetached && !batteryLeft;
  if (manualRightLabel) {
    props.rightLabel = nullptr;
  }
  props.borderEdges = fui::EdgeBottom;
  props.titleText = tokens.titleText;
  props.titleText.align = tokens.headerTitleAlign;
  props.subtitleText = tokens.smallText;
  props.styles = tokens.popup;
  props.sidePadding = tokens.headerSidePadding;
  if (batteryDetached) {
    // Battery in its own corner strip; the title owns the full width of the
    // lower sub-band, so long book titles span the header (Lyra layout).
    // Anchor the title with explicit clearance above the band's bottom rule
    // instead of naive sub-band centering, which left the glyphs nearly
    // touching it.
    const int titleLineHeight = ui.target.lineHeight(fui::GfxRendererTarget::FONT_TITLE);
    const int titleTop = static_cast<int>(band.height) - tokens.headerUnderline - tokens.spaceMd - titleLineHeight;
    props.titleOffsetY = static_cast<int16_t>(titleTop - (static_cast<int>(band.height) - titleLineHeight) / 2);
  } else {
    const int16_t reserve = static_cast<int16_t>(batteryReserve + tokens.spaceMd);
    if (batteryLeft) {
      props.leftReserve = reserve;
    } else {
      props.rightReserve = reserve;
    }
  }
  // Underline only under a titled header: an untitled band (Lyra home screen)
  // historically drew no rule, and the old themes keyed the line on the title.
  if (title != nullptr && props.styles.normal.border.kind == fui::PaintKind::None && tokens.headerUnderline > 0) {
    props.styles.normal.border = fui::Paint::solid(fui::Color::Black);
    props.styles.normal.borderWidth = tokens.headerUnderline;
  }
  fui::header(ui.frame, band, props);

  fui::BatteryIndicatorProps battery;
  battery.percent = static_cast<uint8_t>(percentage > 100 ? 100 : percentage);
  battery.charging = gpio.isUsbConnected();
  battery.label = showBatteryPercentage ? percentText : nullptr;
  battery.text = tokens.smallText;
  battery.glyphWidth = static_cast<int16_t>(metrics.batteryWidth);
  battery.glyphHeight = static_cast<int16_t>(metrics.batteryHeight);
  battery.gap = batteryPercentSpacing;
  // Detached: hug the corner (12px, the legacy inset) within the battery
  // strip; shared line: sit on the content grid. Both anchor to the band's top
  // strip (batteryBarHeight) — the legacy shared-line headers drew the battery
  // at the top edge, and it keeps the lower-right corner free for the manual
  // right label below.
  const int16_t batteryEdgeInset = batteryDetached ? 12 : tokens.headerSidePadding;
  const int16_t batteryX = batteryLeft ? static_cast<int16_t>(band.x + batteryEdgeInset)
                                       : static_cast<int16_t>(band.right() - batteryEdgeInset - batteryReserve);
  const int16_t batteryH = static_cast<int16_t>(metrics.batteryBarHeight);
  fui::batteryIndicator(ui.frame, fui::Rect{batteryX, band.y, batteryReserve, batteryH}, battery);

  if (manualRightLabel) {
    const fui::Size labelSize = ui.target.measureText(fui::GfxRendererTarget::FONT_SMALL, subtitle, tokens.smallText);
    const int16_t labelH = ui.target.lineHeight(fui::GfxRendererTarget::FONT_SMALL);
    const fui::Rect labelRect{static_cast<int16_t>(band.right() - tokens.headerSidePadding - labelSize.width),
                              static_cast<int16_t>(band.bottom() - tokens.headerUnderline - tokens.spaceSm - labelH),
                              labelSize.width, labelH};
    ui.target.text(labelRect, subtitle, tokens.smallText);
  }
}

void BaseTheme::drawSubHeader(const GfxRenderer& renderer, Rect rect, const char* label, const char* rightLabel) const {
  constexpr int maxListValueWidth = 200;

  int currentX = rect.x + BaseMetrics::values.contentSidePadding;
  int rightSpace = BaseMetrics::values.contentSidePadding;
  if (rightLabel) {
    auto truncatedRightLabel =
        renderer.truncatedText(SMALL_FONT_ID, rightLabel, maxListValueWidth, EpdFontFamily::REGULAR);
    int rightLabelWidth = renderer.getTextWidth(SMALL_FONT_ID, truncatedRightLabel.c_str());
    renderer.drawText(SMALL_FONT_ID, rect.x + rect.width - BaseMetrics::values.contentSidePadding - rightLabelWidth,
                      rect.y + 7, truncatedRightLabel.c_str());
    rightSpace += rightLabelWidth + 10;
  }

  auto truncatedLabel = renderer.truncatedText(
      UI_12_FONT_ID, label, rect.width - BaseMetrics::values.contentSidePadding - rightSpace, EpdFontFamily::REGULAR);
  renderer.drawText(UI_12_FONT_ID, currentX, rect.y, truncatedLabel.c_str(), true, EpdFontFamily::REGULAR);
}

void BaseTheme::drawTabBar(const GfxRenderer& renderer, const Rect rect, const std::vector<TabInfo>& tabs,
                           bool selected) const {
  constexpr int underlineHeight = 2;  // Height of selection underline
  constexpr int underlineGap = 4;     // Gap between text and underline

  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);

  int currentX = rect.x + BaseMetrics::values.contentSidePadding;

  for (const auto& tab : tabs) {
    const int textWidth =
        renderer.getTextWidth(UI_12_FONT_ID, tab.label, tab.selected ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);

    // Draw underline for selected tab
    if (tab.selected) {
      if (selected) {
        renderer.fillRect(currentX - 3, rect.y, textWidth + 6, lineHeight + underlineGap);
      } else {
        renderer.fillRect(currentX, rect.y + lineHeight + underlineGap, textWidth, underlineHeight);
      }
    }

    // Draw tab label
    renderer.drawText(UI_12_FONT_ID, currentX, rect.y, tab.label, !(tab.selected && selected),
                      tab.selected ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);

    currentX += textWidth + BaseMetrics::values.tabSpacing;
  }
}

bool BaseTheme::tabIndexFromPoint(const GfxRenderer& renderer, const Rect rect, const std::vector<TabInfo>& tabs,
                                  const int x, const int y, int& index) const {
  if (tabs.empty() || y < rect.y || y >= rect.y + rect.height) {
    return false;
  }

  int currentX = rect.x + BaseMetrics::values.contentSidePadding;
  for (size_t i = 0; i < tabs.size(); i++) {
    const auto& tab = tabs[i];
    const int textWidth =
        renderer.getTextWidth(UI_12_FONT_ID, tab.label, tab.selected ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    const int left = (i == 0) ? rect.x : currentX - BaseMetrics::values.tabSpacing / 2;
    const int right = currentX + textWidth + BaseMetrics::values.tabSpacing / 2;
    if (x >= left && x < right) {
      index = static_cast<int>(i);
      return true;
    }
    currentX += textWidth + BaseMetrics::values.tabSpacing;
  }

  return false;
}

// Draw the "Recent Book" cover card on the home screen
// TODO: Refactor method to make it cleaner, split into smaller methods
void BaseTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                    const int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                    bool& bufferRestored, std::function<bool()> storeCoverBuffer) const {
  (void)selectorIndex;
  (void)bufferRestored;
  if (recentBooks.empty()) {
    const int y =
        rect.y + (rect.height - renderer.getLineHeight(UI_12_FONT_ID) - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
    renderer.drawCenteredText(UI_12_FONT_ID, y, tr(STR_NO_OPEN_BOOK));
    renderer.drawCenteredText(UI_10_FONT_ID, y + renderer.getLineHeight(UI_12_FONT_ID), tr(STR_START_READING));
    return;
  }

  const RecentBook& book = recentBooks.front();
  const int coverHeight = rect.height;
  int localCoverWidth = std::max(1, coverHeight * 2 / 3);
  bool hasCover = false;

  if (!coverRendered && !book.coverBmpPath.empty()) {
    const std::string coverPath = UITheme::getCoverThumbPath(book.coverBmpPath, coverHeight);
    HalFile file;
    if (Storage.openFileForRead("HOME", coverPath, file)) {
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getWidth() > 0 && bitmap.getHeight() > 0) {
        localCoverWidth =
            std::max(1, static_cast<int>(bitmap.getWidth() * static_cast<float>(coverHeight) / bitmap.getHeight()));
        localCoverWidth = std::min(localCoverWidth, rect.width * 2 / 3);
        renderer.drawBitmap(bitmap, rect.x, rect.y, localCoverWidth, coverHeight);
        hasCover = true;
      }
      file.close();
    }
  }

  if (!coverRendered) {
    if (!hasCover) {
      renderer.drawRect(rect.x, rect.y, localCoverWidth, coverHeight);
    }
    homeCoverWidth = localCoverWidth;
    coverBufferStored = storeCoverBuffer();
    coverRendered = coverBufferStored;
  }

  const int textX = rect.x + homeCoverWidth + 14;
  const int textRight = rect.x + rect.width - BaseMetrics::values.contentSidePadding;
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

int BaseTheme::getMenuRowHeight(const GfxRenderer&) const { return UITheme::getInstance().getMetrics().menuRowHeight; }

void BaseTheme::drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                               const std::function<std::string(int index)>& buttonLabel,
                               const std::function<UIIcon(int index)>& rowIcon) const {
  for (int i = 0; i < buttonCount; ++i) {
    const int tileY = BaseMetrics::values.verticalSpacing + rect.y +
                      static_cast<int>(i) * (BaseMetrics::values.menuRowHeight + BaseMetrics::values.menuSpacing);

    const bool selected = selectedIndex == i;

    if (selected) {
      renderer.fillRect(rect.x + BaseMetrics::values.contentSidePadding, tileY,
                        rect.width - BaseMetrics::values.contentSidePadding * 2, BaseMetrics::values.menuRowHeight);
    } else {
      renderer.drawRect(rect.x + BaseMetrics::values.contentSidePadding, tileY,
                        rect.width - BaseMetrics::values.contentSidePadding * 2, BaseMetrics::values.menuRowHeight);
    }

    std::string labelStr = buttonLabel(i);
    const char* label = labelStr.c_str();
    const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, label);
    constexpr int iconSize = 24;
    constexpr int iconGap = 10;
    const uint8_t* iconBitmap = rowIcon != nullptr ? getUIIconBitmap(rowIcon(i), iconSize) : nullptr;
    const int contentWidth = textWidth + (iconBitmap != nullptr ? iconSize + iconGap : 0);
    int textX = rect.x + (rect.width - contentWidth) / 2;
    const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
    const int textY =
        tileY + (BaseMetrics::values.menuRowHeight - lineHeight) / 2;  // vertically centered assuming y is top of text

    if (iconBitmap != nullptr) {
      drawUIIcon(renderer, rowIcon(i), textX, tileY + (BaseMetrics::values.menuRowHeight - iconSize) / 2, iconSize,
                 !selected);
      textX += iconSize + iconGap;
    }
    // Invert text when the tile is selected, to contrast with the filled background
    renderer.drawText(UI_10_FONT_ID, textX, textY, label, !selected);
  }
}

Rect BaseTheme::drawPopup(const GfxRenderer& renderer, const char* message) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int marginX = metrics.popupMarginX;
  const int marginY = metrics.popupMarginY;
  const int frameThickness = metrics.popupFrameThickness;
  const EpdFontFamily::Style popupFontFamily = metrics.popupTextBold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
  // Scale y position proportionally to screen height
  const int y = static_cast<int>(renderer.getScreenHeight() * metrics.popupTopOffsetRatio);
  const int textWidth = renderer.getTextWidth(UI_12_FONT_ID, message, popupFontFamily);
  const int textHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int w = textWidth + marginX * 2;
  const int h = textHeight + marginY * 2;
  const int x = (renderer.getScreenWidth() - w) / 2;

  const bool useRoundedPopup = metrics.popupCornerRadius > 0;
  if (useRoundedPopup) {
    renderer.fillRoundedRect(x - frameThickness, y - frameThickness, w + frameThickness * 2, h + frameThickness * 2,
                             metrics.popupCornerRadius + frameThickness, Color::White);
    renderer.fillRoundedRect(x, y, w, h, metrics.popupCornerRadius, Color::Black);
  } else {
    renderer.fillRect(x - frameThickness, y - frameThickness, w + frameThickness * 2, h + frameThickness * 2, true);
    renderer.fillRect(x, y, w, h, false);
  }

  const int textX = x + (w - textWidth) / 2;
  const int textY = y + marginY + metrics.popupTextBaselineOffsetY;
  renderer.drawText(UI_12_FONT_ID, textX, textY, message, metrics.popupTextInverted, popupFontFamily);
  renderer.displayBuffer();
  return Rect{x, y, w, h};
}

void BaseTheme::fillPopupProgress(const GfxRenderer& renderer, const Rect& layout, const int progress) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int barHeight = metrics.popupProgressBarHeight;
  const int barWidth =
      std::max(0, layout.width - metrics.popupMarginX * 2);  // twice the margin in drawPopup to match text width
  const int barX = layout.x + (layout.width - barWidth) / 2;
  const int barY = layout.y + layout.height - metrics.popupMarginY / 2 - barHeight / 2 - 1;
  if (barWidth <= 0 || barHeight <= 0) {
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    return;
  }

  const int scaledProgress = metrics.popupProgressClampPercent ? std::clamp(progress, 0, 100) : progress;
  const int fillWidth = barWidth * scaledProgress / 100;

  if (metrics.popupProgressDrawOutline) {
    renderer.drawRect(barX, barY, barWidth, barHeight, 1, metrics.popupProgressOutlineInverted);
  }
  if (fillWidth > 0) {
    renderer.fillRect(barX, barY, fillWidth, barHeight, metrics.popupProgressFillInverted);
  }

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void BaseTheme::drawStatusBar(GfxRenderer& renderer, const float bookProgress, const int currentPage,
                              const int pageCount, std::string title, const int paddingBottom, const int textYOffset,
                              const bool fillMargin, const bool isPageBookmarked, const bool pageCountEstimated) const {
  auto metrics = UITheme::getInstance().getMetrics();
  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  renderer.getOrientedViewableTRBL(&orientedMarginTop, &orientedMarginRight, &orientedMarginBottom,
                                   &orientedMarginLeft);
  const auto sb = SETTINGS.statusBarSpec();
  const bool showStatusBarTextLane = sb.textLaneVisible(halClock.isAvailable());

  // Draw Progress Text
  const auto screenHeight = renderer.getScreenHeight();
  auto textY = screenHeight - UITheme::getInstance().getStatusBarHeight() - orientedMarginBottom - paddingBottom - 4;

  const int leftClusterX = metrics.statusBarHorizontalMargin + orientedMarginLeft + 1;
  const int rightClusterX = renderer.getScreenWidth() - metrics.statusBarHorizontalMargin - orientedMarginRight;
  int leftClusterWidth = 0;
  int rightClusterWidth = 0;

  if (sb.showBookProgressPercent || sb.showChapterPageCount) {
    // Right aligned text for progress counter
    char progressStr[32];

    // Prefix the page count with "~" while a still-building spine only yields an estimated total.
    const char* estimatePrefix = pageCountEstimated ? "~" : "";

    if (sb.showBookProgressPercent && sb.showChapterPageCount) {
      snprintf(progressStr, sizeof(progressStr), "%s%d/%d  %.0f%%", estimatePrefix, currentPage, pageCount,
               bookProgress);
    } else if (sb.showBookProgressPercent) {
      snprintf(progressStr, sizeof(progressStr), "%.0f%%", bookProgress);
    } else {
      snprintf(progressStr, sizeof(progressStr), "%s%d/%d", estimatePrefix, currentPage, pageCount);
    }

    int progressTextWidth = renderer.getTextWidth(SMALL_FONT_ID, progressStr);
    renderer.drawText(SMALL_FONT_ID, rightClusterX - progressTextWidth, textY, progressStr);

    rightClusterWidth += progressTextWidth;
  }

  // Draw Progress Bar
  if (sb.showsProgressBar()) {
    const int barMarginLeft = fillMargin ? 0 : orientedMarginLeft;
    const int barMarginRight = fillMargin ? 0 : orientedMarginRight;
    const int progressBarMaxWidth = renderer.getScreenWidth() - barMarginLeft - barMarginRight;
    const int progressBarY = renderer.getScreenHeight() - orientedMarginBottom - sb.progressBarHeightPx -
                             paddingBottom + (fillMargin ? 1 : 0);
    size_t progress;
    if (sb.progressBarMode == CrossPointSettings::STATUS_BAR_PROGRESS_BAR::BOOK_PROGRESS) {
      progress = static_cast<size_t>(bookProgress);
    } else {
      // Chapter progress
      progress = (pageCount > 0) ? (static_cast<float>(currentPage) / pageCount) * 100 : 0;
    }
    const int barWidth = progressBarMaxWidth * progress / 100;
    const int barHeight = sb.progressBarHeightPx + (fillMargin ? orientedMarginBottom - 1 : 0);
    renderer.fillRect(barMarginLeft, progressBarY, barWidth, barHeight, true);
  }

  // Draw Battery
  const bool showBatteryPercentage = sb.showBatteryPercent;

  if (sb.showBattery) {
    GUI.drawBatteryLeft(renderer,
                        Rect{leftClusterX + leftClusterWidth, textY, metrics.batteryWidth, metrics.batteryHeight},
                        showBatteryPercentage);
    int batteryWidth = metrics.batteryWidth;

    if (showBatteryPercentage) {
      const uint16_t percentage = powerManager.getBatteryPercentage();
      // width of icon + spacing + text for layout purposes
      batteryWidth +=
          batteryPercentSpacing + renderer.getTextWidth(SMALL_FONT_ID, (std::to_string(percentage) + "%").c_str());
    }

    leftClusterWidth += batteryWidth;
  }

  // Draw Clock (X3 only — DS3231 RTC)
  if (sb.showsClock() && halClock.isAvailable()) {
    char timeBuf[9];
    if (halClock.formatTime(timeBuf, sizeof(timeBuf), sb.clockUtcOffsetQ, sb.clock12h)) {
      int clockTextWidth = renderer.getTextWidth(SMALL_FONT_ID, timeBuf);
      int clockX = 0;
      // Position to the left or right of the progress text (with a small gap)
      if (sb.clockMode == CrossPointSettings::STATUS_BAR_CLOCK_LEFT) {
        clockX = leftClusterX + leftClusterWidth + (leftClusterWidth > 0 ? 10 : 0);
        leftClusterWidth += clockTextWidth + 10;
      } else if (sb.clockMode == CrossPointSettings::STATUS_BAR_CLOCK_RIGHT) {
        clockX = rightClusterX - rightClusterWidth - (rightClusterWidth > 0 ? 10 : 0) - clockTextWidth;
        rightClusterWidth += clockTextWidth + 10;
      }
      renderer.drawText(SMALL_FONT_ID, clockX, textY, timeBuf);
    }
  }

  // Draw Bookmark
  if (showStatusBarTextLane && isPageBookmarked) {
    const int bookmarkGap = leftClusterWidth > 0 ? bookmarkStatusIconGap : 0;
    const int bookmarkX = leftClusterX + leftClusterWidth + bookmarkGap;
    const int bookmarkY = textY + 5;
    drawBookmarkStatusIcon(renderer, bookmarkX, bookmarkY);
    leftClusterWidth += bookmarkStatusIconWidth + bookmarkGap;
  }

  // Draw Title
  if (!title.empty()) {
    textY -= textYOffset;
    // Centered chapter title text
    // Page width minus existing content with 30px padding on each side
    const int rendererableScreenWidth =
        renderer.getScreenWidth() - (metrics.statusBarHorizontalMargin * 2) - orientedMarginLeft - orientedMarginRight;

    const int titleMarginLeft = leftClusterWidth + 30;
    const int titleMarginRight = rightClusterWidth + 30;

    // Attempt to center title on the screen, but if title is too wide then later we will center it within the
    // available space.
    int titleMarginLeftAdjusted = std::max(titleMarginLeft, titleMarginRight);
    int availableTitleSpace = rendererableScreenWidth - 2 * titleMarginLeftAdjusted;

    int titleWidth;
    titleWidth = renderer.getTextWidth(SMALL_FONT_ID, title.c_str());
    if (titleWidth > availableTitleSpace) {
      // Not enough space to center on the screen, center it within the remaining space instead
      availableTitleSpace = rendererableScreenWidth - titleMarginLeft - titleMarginRight;
      titleMarginLeftAdjusted = titleMarginLeft;
    }
    if (titleWidth > availableTitleSpace) {
      title = renderer.truncatedText(SMALL_FONT_ID, title.c_str(), availableTitleSpace);
      titleWidth = renderer.getTextWidth(SMALL_FONT_ID, title.c_str());
    }

    renderer.drawText(SMALL_FONT_ID,
                      titleMarginLeftAdjusted + metrics.statusBarHorizontalMargin + orientedMarginLeft +
                          (availableTitleSpace - titleWidth) / 2,
                      textY, title.c_str());
  }
}

void BaseTheme::drawHelpText(const GfxRenderer& renderer, Rect rect, const char* label) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  auto truncatedLabel =
      renderer.truncatedText(SMALL_FONT_ID, label, rect.width - metrics.contentSidePadding * 2, EpdFontFamily::REGULAR);
  renderer.drawCenteredText(SMALL_FONT_ID, rect.y, truncatedLabel.c_str());
}

void BaseTheme::drawTextField(const GfxRenderer& renderer, Rect rect, const int textWidth, bool cursorMode,
                              int contentStartX, int contentWidth) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int lineY = rect.y + rect.height + lineHeight + metrics.verticalSpacing;
  const int thickness = cursorMode ? metrics.textFieldCursorThickness : metrics.textFieldNormalThickness;
  if (contentWidth > 0) {
    renderer.drawLine(rect.x + contentStartX, lineY,
                      rect.x + contentStartX + contentWidth + metrics.textFieldLineEndOffset, lineY, thickness, true);
  } else {
    const int lineW = textWidth + metrics.textFieldHorizontalPadding * 2;
    const int lineStart = rect.x + (rect.width - lineW) / 2;
    renderer.drawLine(lineStart, lineY, lineStart + lineW + metrics.textFieldLineEndOffset, lineY, thickness, true);
  }
}

void BaseTheme::drawOptionPopup(const GfxRenderer& renderer, const char* title, const std::vector<std::string>& options,
                                int selectedIndex) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  const int optionFontId = metrics.optionPopupUseSmallFont ? UI_10_FONT_ID : UI_12_FONT_ID;
  const EpdFontFamily::Style optionStyle =
      metrics.optionPopupOptionFontBold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;

  const int itemSpacing = metrics.optionPopupItemSpacing;
  const int innerPadding = metrics.optionPopupInnerPadding;
  const int selectionHPadding = metrics.optionPopupSelectionHPadding;
  const int selectionVPadding = metrics.optionPopupSelectionVPadding;

  const int optionLineHeight = renderer.getLineHeight(optionFontId);
  const int titleLineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int rowHeight = optionLineHeight + selectionVPadding * 2;

  int maxTextWidth = renderer.getTextWidth(UI_12_FONT_ID, title, EpdFontFamily::BOLD);
  for (const auto& opt : options) {
    int w = renderer.getTextWidth(optionFontId, opt.c_str(), optionStyle);
    if (w > maxTextWidth) maxTextWidth = w;
  }

  const int optionCount = static_cast<int>(options.size());
  const int listHeight = rowHeight * optionCount + itemSpacing * (optionCount - 1);
  const int dialogW = std::min((maxTextWidth + innerPadding * 2 + selectionHPadding * 2) * 12 / 10,
                               pageWidth - metrics.optionPopupDialogSideMargin * 2);
  const int contentHeight = titleLineHeight + metrics.optionPopupTitleGap + listHeight;
  const int dialogH = contentHeight + innerPadding * 2;
  const int dialogX = (pageWidth - dialogW) / 2;
  const int dialogY = (pageHeight - dialogH) / 2;

  const int frameThickness = metrics.popupFrameThickness;
  const int frameRadius = metrics.popupCornerRadius;

  if (frameRadius > 0) {
    renderer.fillRoundedRect(dialogX - frameThickness, dialogY - frameThickness, dialogW + frameThickness * 2,
                             dialogH + frameThickness * 2, frameRadius + frameThickness, Color::White);
    renderer.fillRoundedRect(dialogX, dialogY, dialogW, dialogH, frameRadius, Color::Black);
    renderer.fillRoundedRect(dialogX + frameThickness, dialogY + frameThickness, dialogW - frameThickness * 2,
                             dialogH - frameThickness * 2,
                             frameRadius - frameThickness > 0 ? frameRadius - frameThickness : 0, Color::White);
  } else {
    renderer.fillRect(dialogX - frameThickness, dialogY - frameThickness, dialogW + frameThickness * 2,
                      dialogH + frameThickness * 2, true);
    renderer.fillRect(dialogX, dialogY, dialogW, dialogH, false);
  }

  int y = dialogY + innerPadding;

  renderer.drawCenteredText(UI_12_FONT_ID, y, title, true, EpdFontFamily::BOLD);
  y += titleLineHeight;

  if (metrics.optionPopupTitleSeparator) {
    const int sepY = y + metrics.optionPopupTitleGap / 2;
    renderer.drawLine(dialogX + innerPadding, sepY, dialogX + dialogW - innerPadding, sepY, true);
  }

  y += metrics.optionPopupTitleGap;

  const int itemRectX = dialogX + innerPadding;
  const int itemRectW = dialogW - innerPadding * 2;
  const int selectionRadius = metrics.optionPopupSelectionRadius;

  for (int i = 0; i < optionCount; i++) {
    const int itemY = y + i * (rowHeight + itemSpacing);
    const bool selected = (i == selectedIndex);
    const char* labelText = options[i].c_str();

    if (metrics.optionPopupDrawAllRows || selected) {
      Color rowColor;
      if (selected) {
        rowColor = metrics.optionPopupSelectionLight ? Color::LightGray : Color::Black;
      } else {
        rowColor = Color::White;
      }
      if (selectionRadius > 0) {
        renderer.fillRoundedRect(itemRectX, itemY, itemRectW, rowHeight, selectionRadius, rowColor);
      } else {
        renderer.fillRect(itemRectX, itemY, itemRectW, rowHeight, rowColor == Color::Black);
      }
    }

    const int textW = renderer.getTextWidth(optionFontId, labelText, optionStyle);
    const int textY = itemY + (rowHeight - optionLineHeight) / 2;
    const int textX = itemRectX + (itemRectW - textW) / 2;
    // Unselected items: text is dark (invert=true means draw on white bg).
    // Selected on dark bg: text must be white (invert=false).
    // Selected on light bg: text stays dark (invert=true).
    const bool invertText = selected ? metrics.optionPopupSelectionLight : true;
    renderer.drawText(optionFontId, textX, textY, labelText, invertText, optionStyle);
  }
}
