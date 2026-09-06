#include "ReadingStatsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>

#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
void drawRow(GfxRenderer& renderer, const int y, const char* label, const char* value) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, y, label);
  const int valueWidth = renderer.getTextWidth(UI_10_FONT_ID, value, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, renderer.getScreenWidth() - metrics.contentSidePadding - valueWidth, y, value, true,
                    EpdFontFamily::BOLD);
}

void formatReadingSpeed(const ReadingStatsData& stats, char* value, const size_t size) {
  const uint32_t secondsPerPage = ReadingStats::readingSpeedSeconds(stats);
  if (secondsPerPage == 0) {
    value[0] = '\0';
    return;
  }
  const uint32_t pagesPerMinuteHundredths = (6000 + secondsPerPage / 2) / secondsPerPage;
  snprintf(value, size, tr(STR_READING_SPEED_VALUE), static_cast<unsigned long>(pagesPerMinuteHundredths / 100),
           static_cast<unsigned long>(pagesPerMinuteHundredths % 100));
}

std::string filenameStem(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  const size_t start = slash == std::string::npos ? 0 : slash + 1;
  const size_t dot = path.find_last_of('.');
  return dot == std::string::npos || dot <= start ? path.substr(start) : path.substr(start, dot - start);
}
}  // namespace

ReadingStatsActivity::ReadingStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("GlobalReadingStats", renderer, mappedInput) {}

ReadingStatsActivity::ReadingStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string path,
                                           std::string bookTitle, const uint16_t progressBasisPoints)
    : Activity("BookReadingStats", renderer, mappedInput),
      bookPath(std::move(path)),
      title(std::move(bookTitle)),
      progressBasisPoints(progressBasisPoints) {}

void ReadingStatsActivity::onEnter() {
  Activity::onEnter();
  if (bookPath.empty()) {
    ReadingStats::loadGlobal(stats);
    const std::string& currentPath = APP_STATE.openEpubPath;
    if (!currentPath.empty()) {
      currentBookTitle = filenameStem(currentPath);
      const auto& recentBooks = RECENT_BOOKS.getBooks();
      const auto recent = std::find_if(recentBooks.begin(), recentBooks.end(),
                                       [&currentPath](const RecentBook& book) { return book.path == currentPath; });
      if (recent != recentBooks.end() && !recent->title.empty()) currentBookTitle = recent->title;
      ReadingStats::loadBook(currentPath, currentBookStats);
      hasCurrentBook = true;
    }
  } else {
    ReadingStats::loadBook(bookPath, stats);
  }
  requestUpdate();
}

void ReadingStatsActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (!bookPath.empty() && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) confirmReset();

  if (mappedInput.hasTouch()) {
    int x = 0;
    int y = 0;
    if (!mappedInput.wasScreenTapped(x, y)) return;
    const auto& metrics = UITheme::getInstance().getMetrics();
    const int buttonY = renderer.getScreenHeight() - metrics.listRowHeight - metrics.verticalSpacing;
    if (y < buttonY) return;
    if (x < renderer.getScreenWidth() / 2) {
      finish();
    } else if (!bookPath.empty()) {
      confirmReset();
    }
  }
}

void ReadingStatsActivity::confirmReset() {
  auto confirmation = makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_CLEAR_BOOK_STATS),
                                                              tr(STR_CLEAR_BOOK_STATS_CONFIRM));
  if (!confirmation) return;
  startActivityForResult(std::move(confirmation), [this](const ActivityResult& result) {
    if (result.isCancelled) return;
    ReadingStats::resetBook(bookPath);
    stats = {};
  });
}

void ReadingStatsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight},
                 bookPath.empty() ? tr(STR_READING_STATS) : tr(STR_BOOK_STATS));

  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing * 2;
  if (!title.empty()) {
    const std::string visible =
        renderer.truncatedText(UI_10_FONT_ID, title.c_str(), width - metrics.contentSidePadding * 2);
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, y, visible.c_str(), true, EpdFontFamily::BOLD);
    y += renderer.getLineHeight(UI_10_FONT_ID) + metrics.verticalSpacing * 2;
  }

  char value[32];
  ReadingStats::formatDuration(stats.readingSeconds, value, sizeof(value));
  drawRow(renderer, y, tr(STR_READING_TIME), value);
  y += metrics.listRowHeight;
  if (bookPath.empty()) {
    snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(stats.completedBooks));
    drawRow(renderer, y, tr(STR_BOOKS_COMPLETED), value);
    y += metrics.listRowHeight;
    const uint32_t averageSession = stats.sessions > 0 ? stats.readingSeconds / stats.sessions : 0;
    ReadingStats::formatDuration(averageSession, value, sizeof(value));
    drawRow(renderer, y, tr(STR_AVERAGE_SESSION), value);
    y += metrics.listRowHeight;

    if (hasCurrentBook) {
      renderer.drawLine(metrics.contentSidePadding, y, width - metrics.contentSidePadding, y);
      y += metrics.verticalSpacing * 2;
      const std::string visible =
          renderer.truncatedText(UI_10_FONT_ID, currentBookTitle.c_str(), width - metrics.contentSidePadding * 2);
      renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, y, visible.c_str(), true, EpdFontFamily::BOLD);
      y += renderer.getLineHeight(UI_10_FONT_ID) + metrics.verticalSpacing * 2;

      ReadingStats::formatDuration(currentBookStats.readingSeconds, value, sizeof(value));
      drawRow(renderer, y, tr(STR_READING_TIME), value);
      y += metrics.listRowHeight;
      formatReadingSpeed(currentBookStats, value, sizeof(value));
      if (value[0] != '\0') {
        drawRow(renderer, y, tr(STR_READING_SPEED), value);
        y += metrics.listRowHeight;
      }
      const uint32_t bookAverageSession =
          currentBookStats.sessions > 0 ? currentBookStats.readingSeconds / currentBookStats.sessions : 0;
      ReadingStats::formatDuration(bookAverageSession, value, sizeof(value));
      drawRow(renderer, y, tr(STR_AVERAGE_SESSION), value);
    }
  } else {
    formatReadingSpeed(stats, value, sizeof(value));
    if (value[0] != '\0') {
      drawRow(renderer, y, tr(STR_READING_SPEED), value);
      y += metrics.listRowHeight;
    }
    const uint32_t averageSession = stats.sessions > 0 ? stats.readingSeconds / stats.sessions : 0;
    ReadingStats::formatDuration(averageSession, value, sizeof(value));
    drawRow(renderer, y, tr(STR_AVERAGE_SESSION), value);
    y += metrics.listRowHeight;
    const uint32_t remaining = ReadingStats::remainingSeconds(stats, progressBasisPoints);
    if (remaining > 0) {
      ReadingStats::formatDuration(remaining, value, sizeof(value));
      drawRow(renderer, y, tr(STR_TIME_REMAINING), value);
    }
  }

  if (mappedInput.hasTouch()) {
    const int gap = metrics.verticalSpacing;
    const int buttonY = renderer.getScreenHeight() - metrics.listRowHeight - gap;
    const int buttonWidth = (width - metrics.contentSidePadding * 2 - gap) / 2;
    const auto drawTouchButton = [&](const int x, const int buttonWidth, const char* label) {
      renderer.drawRect(x, buttonY, buttonWidth, metrics.listRowHeight, true);
      const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, label, EpdFontFamily::BOLD);
      const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
      renderer.drawText(UI_10_FONT_ID, x + (buttonWidth - textWidth) / 2,
                        buttonY + (metrics.listRowHeight - lineHeight) / 2, label, true, EpdFontFamily::BOLD);
    };
    drawTouchButton(metrics.contentSidePadding, buttonWidth, tr(STR_BACK));
    if (!bookPath.empty()) {
      drawTouchButton(metrics.contentSidePadding + buttonWidth + gap, buttonWidth, tr(STR_CLEAR_BOOK_STATS));
    }
  } else {
    GUI.drawIconButtonHints(renderer, {.icon = NavigateBack},
                            bookPath.empty() ? ButtonHint{} : ButtonHint{.icon = ListX}, {}, {});
  }
  renderer.displayBuffer();
}
