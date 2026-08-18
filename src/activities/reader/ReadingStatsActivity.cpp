#include "ReadingStatsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
void drawRow(GfxRenderer& renderer, const int y, const char* label, const char* value) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, y, label);
  const int valueWidth = renderer.getTextWidth(UI_10_FONT_ID, value, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, renderer.getScreenWidth() - metrics.contentSidePadding - valueWidth, y, value,
                    true, EpdFontFamily::BOLD);
}
}  // namespace

ReadingStatsActivity::ReadingStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("GlobalReadingStats", renderer, mappedInput) {}

ReadingStatsActivity::ReadingStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                           std::string path, std::string bookTitle, const uint8_t progress)
    : Activity("BookReadingStats", renderer, mappedInput),
      bookPath(std::move(path)),
      title(std::move(bookTitle)),
      progressPercent(progress) {}

void ReadingStatsActivity::onEnter() {
  Activity::onEnter();
  if (bookPath.empty()) {
    ReadingStats::loadGlobal(stats);
  } else {
    ReadingStats::loadBook(bookPath, stats);
  }
  requestUpdate();
}

void ReadingStatsActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
      mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    finish();
  }
}

void ReadingStatsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight},
                 bookPath.empty() ? tr(STR_READING_STATS) : tr(STR_BOOK_STATS));

  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing * 2;
  if (!title.empty()) {
    const std::string visible = renderer.truncatedText(UI_10_FONT_ID, title.c_str(),
                                                       width - metrics.contentSidePadding * 2);
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, y, visible.c_str(), true, EpdFontFamily::BOLD);
    y += renderer.getLineHeight(UI_10_FONT_ID) + metrics.verticalSpacing * 2;
  }

  char value[32];
  ReadingStats::formatDuration(stats.readingSeconds, value, sizeof(value));
  drawRow(renderer, y, tr(STR_READING_TIME), value);
  y += metrics.listRowHeight;
  snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(stats.sessions));
  drawRow(renderer, y, tr(STR_READING_SESSIONS), value);
  y += metrics.listRowHeight;
  snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(stats.forwardPages));
  drawRow(renderer, y, tr(STR_PAGES_TURNED), value);
  y += metrics.listRowHeight;

  if (bookPath.empty()) {
    snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(stats.completedBooks));
    drawRow(renderer, y, tr(STR_BOOKS_COMPLETED), value);
    y += metrics.listRowHeight;
  }

  const uint32_t averageSession = stats.sessions > 0 ? stats.readingSeconds / stats.sessions : 0;
  ReadingStats::formatDuration(averageSession, value, sizeof(value));
  drawRow(renderer, y, tr(STR_AVERAGE_SESSION), value);

  if (!bookPath.empty()) {
    y += metrics.listRowHeight;
    const uint32_t averagePage = stats.forwardPages > 0 ? stats.readingSeconds / stats.forwardPages : 0;
    ReadingStats::formatDuration(averagePage, value, sizeof(value));
    drawRow(renderer, y, tr(STR_AVERAGE_PAGE), value);
    y += metrics.listRowHeight;
    const uint32_t remaining = progressPercent > 0 && progressPercent < 100
                                   ? static_cast<uint32_t>((static_cast<uint64_t>(stats.readingSeconds) *
                                                            (100 - progressPercent)) /
                                                           progressPercent)
                                   : 0;
    if (remaining > 0) {
      ReadingStats::formatDuration(remaining, value, sizeof(value));
      drawRow(renderer, y, tr(STR_TIME_REMAINING), value);
    }
  }

  GUI.drawButtonHints(renderer, tr(STR_BACK), "", "", "");
  renderer.displayBuffer();
}
