#include "LibraryBookMenuActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdio>

#include "BookCoverActivity.h"
#include "LibraryBookStateStore.h"
#include "MappedInputManager.h"
#include "ReadingStats.h"
#include "activities/reader/ReadingStatsActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int ACTION_COUNT = 5;

std::string displaySeriesIndex(std::string index) {
  const size_t decimalPoint = index.find('.');
  if (decimalPoint != std::string::npos && decimalPoint + 1 < index.size() &&
      index.find_first_not_of('0', decimalPoint + 1) == std::string::npos) {
    index.erase(decimalPoint);
  }
  return index;
}

ButtonHint bookMenuButtonHint(const MappedInputManager::NavigationAction action) {
  switch (action) {
    case MappedInputManager::NavigationAction::Back:
      return {.icon = NavigateBack};
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

void LibraryBookMenuActivity::buildDisplayMetadata() {
  seriesText = book.series;
  if (!book.seriesIndex.empty()) {
    if (!seriesText.empty()) seriesText += " · ";
    seriesText += displaySeriesIndex(book.seriesIndex);
  }

  tagsText.clear();
  for (const auto& tag : book.tags) {
    if (!tagsText.empty()) tagsText += ", ";
    tagsText += tag;
  }

  pageCountText.clear();
  if (estimatedPageCount > 0) {
    char buffer[32] = {};
    snprintf(buffer, sizeof(buffer), tr(STR_BOOK_PAGE_COUNT_FORMAT), static_cast<unsigned>(estimatedPageCount));
    pageCountText = buffer;
  }
}

void LibraryBookMenuActivity::onEnter() {
  Activity::onEnter();
  lockConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  buildDisplayMetadata();
  requestUpdate();
}

void LibraryBookMenuActivity::activateSelection() {
  if (selectorIndex == 0) {
    onSelectBook(book.path);
    return;
  }

  if (selectorIndex == 1) {
    LIBRARY_BOOK_STATE.setDeferred(book.path, !book.deferred);
    book.deferred = LIBRARY_BOOK_STATE.isDeferred(book.path);
    requestUpdate();
    return;
  }

  if (selectorIndex == 2) {
    const bool markAsRead = !book.started || book.progressPercent < 100;
    book.started = markAsRead;
    book.progressPercent = markAsRead ? 100 : 0;
    if (!LibraryIndex::setProgressState(book.path, book.progressPercent, book.started)) {
      LOG_ERR("LBM", "Failed to update progress state for %s", book.path.c_str());
    }
    ReadingStats::setBookFinished(book.path, markAsRead);
    requestUpdate();
    return;
  }

  if (selectorIndex == 3) {
    auto stats = makeUniqueNoThrow<ReadingStatsActivity>(renderer, mappedInput, book.path, book.title,
                                                         book.progressPercent, estimatedPageCount);
    if (!stats) {
      LOG_ERR("LBM", "OOM: ReadingStatsActivity");
      return;
    }
    startActivityForResult(std::move(stats), nullptr);
    return;
  }

  auto cover = makeUniqueNoThrow<BookCoverActivity>(renderer, mappedInput, book.path);
  if (!cover) {
    LOG_ERR("LBM", "OOM: BookCoverActivity");
    return;
  }
  startActivityForResult(std::move(cover), nullptr);
}

void LibraryBookMenuActivity::loop() {
  if (lockConfirmRelease) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
      lockConfirmRelease = false;
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateSelection();
    return;
  }

  buttonNavigator.onNextRelease([this] {
    selectorIndex = ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), ACTION_COUNT);
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this] {
    selectorIndex = ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), ACTION_COUNT);
    requestUpdate();
  });
}

void LibraryBookMenuActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 book.title.empty() ? tr(STR_BOOK_MENU) : book.title.c_str());

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentBottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
  constexpr int detailGap = 4;
  const int detailLineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int detailWidth = pageWidth - metrics.contentSidePadding * 2;
  const std::string* details[] = {&book.author, &seriesText, &book.year, &tagsText, &pageCountText};
  int detailY = contentTop;
  int visibleDetails = 0;
  for (const std::string* detail : details) {
    if (detail->empty()) continue;
    const std::string text = renderer.truncatedText(UI_10_FONT_ID, detail->c_str(), detailWidth);
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, detailY, text.c_str());
    detailY += detailLineHeight + detailGap;
    visibleDetails++;
  }

  const int actionsTop = detailY + (visibleDetails > 0 ? metrics.verticalSpacing : 0);
  GUI.drawButtonMenu(
      renderer, Rect{0, actionsTop, pageWidth, contentBottom - actionsTop}, ACTION_COUNT,
      static_cast<int>(selectorIndex),
      [this](const int index) {
        if (index == 0) {
          return std::string(tr(STR_READ_BOOK));
        }
        if (index == 1) {
          return std::string(book.deferred ? tr(STR_REMOVE_FROM_DEFERRED) : tr(STR_DEFER_BOOK));
        }
        if (index == 2) {
          return std::string(book.started && book.progressPercent == 100 ? tr(STR_MARK_AS_UNREAD)
                                                                         : tr(STR_MARK_AS_READ));
        }
        if (index == 3) return std::string(tr(STR_BOOK_STATS));
        return std::string(tr(STR_VIEW_BOOK_COVER));
      },
      [this](const int index) {
        if (index == 0) return ReadBook;
        if (index == 1) return book.deferred ? DeferredOff : Deferred;
        if (index == 2) return book.started && book.progressPercent == 100 ? MarkUnread : MarkRead;
        if (index == 3) return Statistics;
        return BookCover;
      });

  const auto actions = mappedInput.mapNavigationActions();
  GUI.drawIconButtonHints(renderer, bookMenuButtonHint(actions.btn1), bookMenuButtonHint(actions.btn2),
                          bookMenuButtonHint(actions.btn3), bookMenuButtonHint(actions.btn4));
  renderer.displayBuffer();
}
