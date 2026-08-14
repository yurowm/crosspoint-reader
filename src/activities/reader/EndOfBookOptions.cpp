#include "EndOfBookOptions.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Xtc.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "ReaderUtils.h"
#include "activities/Activity.h"
#include "components/BookListItem.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/ButtonNavigator.h"
#include "util/NextBookFinder.h"

namespace {
constexpr int RECOMMENDATION_COVER_HEIGHT = BookListItem::DEFAULT_COVER_CACHE_HEIGHT;

struct EndOfBookLayout {
  Rect card;
  Rect home;
  int subtitleY = 0;
};

bool contains(const Rect& rect, const int x, const int y) {
  return x >= rect.x && x < rect.x + rect.width && y >= rect.y && y < rect.y + rect.height;
}

EndOfBookLayout calculateLayout(const GfxRenderer& renderer) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentBottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int subtitleY = contentTop;
  const int cardTop = subtitleY + renderer.getLineHeight(UI_10_FONT_ID) + metrics.verticalSpacing;
  const int homeHeight = std::max(metrics.listRowHeight, renderer.getLineHeight(UI_10_FONT_ID) + 16);
  const int availableCardHeight = contentBottom - cardTop - metrics.verticalSpacing - homeHeight;
  const int cardHeight = std::max(1, std::min(RECOMMENDATION_COVER_HEIGHT, availableCardHeight));
  const int sidePadding = metrics.contentSidePadding;
  const Rect card{sidePadding, cardTop, pageWidth - sidePadding * 2, cardHeight};
  const Rect home{sidePadding, card.y + card.height + metrics.verticalSpacing, pageWidth - sidePadding * 2, homeHeight};
  return {card, home, subtitleY};
}

ButtonHint endButtonHint(const MappedInputManager::NavigationAction action) {
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

void EndOfBookOptions::loadOnce(const std::string& currentBookPath) {
  if (isLoaded.load(std::memory_order_acquire)) return;

  hasRecommendation = NextBookFinder::findRecommendedBook(currentBookPath, recommendation);
  selector = 0;
  if (hasRecommendation) ensureRecommendationCover();
  isLoaded.store(true, std::memory_order_release);
}

void EndOfBookOptions::ensureRecommendationCover() {
  recommendation.coverAttempted = true;

  if (!recommendation.coverBmpPath.empty()) {
    recommendation.coverBmpPath = UITheme::getCoverThumbPath(recommendation.coverBmpPath,
                                                              RECOMMENDATION_COVER_HEIGHT);
    if (Storage.exists(recommendation.coverBmpPath.c_str())) return;
    recommendation.coverBmpPath.clear();
  }

  std::string preferredPath;
  if (FsHelpers::hasEpubExtension(recommendation.path)) {
    Epub epub(recommendation.path, "/.crosspoint");
    if (epub.loadMetadata()) {
      preferredPath = epub.getThumbBmpPath(RECOMMENDATION_COVER_HEIGHT);
      if (Storage.exists(preferredPath.c_str()) || epub.generateThumbBmp(RECOMMENDATION_COVER_HEIGHT)) {
        recommendation.coverBmpPath = preferredPath;
        return;
      }
    }
  } else if (FsHelpers::hasXtcExtension(recommendation.path)) {
    Xtc xtc(recommendation.path, "/.crosspoint");
    if (xtc.load()) {
      preferredPath = xtc.getThumbBmpPath(RECOMMENDATION_COVER_HEIGHT);
      if (Storage.exists(preferredPath.c_str()) || xtc.generateThumbBmp(RECOMMENDATION_COVER_HEIGHT)) {
        recommendation.coverBmpPath = preferredPath;
        return;
      }
    }
  }
}

bool EndOfBookOptions::menuActive() const {
  return isLoaded.load(std::memory_order_acquire) && hasRecommendation;
}

EndOfBookOptions::Action EndOfBookOptions::handleMenuInput(const MappedInputManager& input, std::string* openPath) {
  if (!menuActive()) return Action::None;

  int touchX = 0;
  int touchY = 0;
  if (input.wasScreenTapped(touchX, touchY)) {
    const EndOfBookLayout layout = calculateLayout(renderer);
    if (contains(layout.card, touchX, touchY)) {
      selector = 0;
      if (openPath) *openPath = recommendation.path;
      return Action::OpenBook;
    }
    if (contains(layout.home, touchX, touchY)) {
      selector = 1;
      return Action::GoHome;
    }
  }

  if (input.wasReleased(MappedInputManager::Button::Confirm)) {
    if (selector == 0) {
      if (openPath) *openPath = recommendation.path;
      return Action::OpenBook;
    }
    return Action::GoHome;
  }

  if (input.wasReleased(MappedInputManager::Button::Back) && input.getHeldTime() < ReaderUtils::GO_HOME_MS) {
    return Action::LastPage;
  }

  const bool usePress = SETTINGS.longPressButtonBehavior == CrossPointSettings::OFF;
  const auto triggered = [&](const MappedInputManager::Button button) {
    return usePress ? input.wasPressed(button) : input.wasReleased(button);
  };
  if (triggered(MappedInputManager::Button::NavPrevious)) {
    selector = ButtonNavigator::previousIndex(selector, 2);
    return Action::Redraw;
  }
  if (triggered(MappedInputManager::Button::NavNext)) {
    selector = ButtonNavigator::nextIndex(selector, 2);
    return Action::Redraw;
  }
  return Action::None;
}

void EndOfBookOptions::render(GfxRenderer& renderer, const MappedInputManager& input) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();

  if (!menuActive()) {
    const Rect content{0, 0, pageWidth, renderer.getScreenHeight()};
    UITheme::drawCenteredText(renderer, content, UI_12_FONT_ID, content.y + content.height * 3 / 8,
                              tr(STR_END_OF_BOOK), true, EpdFontFamily::BOLD);
    return;
  }

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_END_OF_BOOK));
  const EndOfBookLayout layout = calculateLayout(renderer);
  UITheme::drawCenteredText(renderer, Rect{0, layout.subtitleY, pageWidth, renderer.getLineHeight(UI_10_FONT_ID)},
                            UI_10_FONT_ID, layout.subtitleY, tr(STR_EOB_CONTINUE_WITH));
  BookListItem::draw(renderer, recommendation, layout.card.x, layout.card.y, layout.card.width, layout.card.height,
                     selector == 0);

  if (selector == 1) {
    renderer.fillRoundedRect(layout.home.x, layout.home.y, layout.home.width, layout.home.height, 5, Color::LightGray);
  }
  renderer.drawRoundedRect(layout.home.x, layout.home.y, layout.home.width, layout.home.height, 1, 5, true);
  const int homeIconSize = 32;
  const int homeIconX = layout.home.x + 12;
  const int homeIconY = layout.home.y + (layout.home.height - homeIconSize) / 2;
  drawUIIcon(renderer, House, homeIconX, homeIconY, homeIconSize);
  renderer.drawText(UI_10_FONT_ID, homeIconX + homeIconSize + 12,
                    layout.home.y + (layout.home.height - renderer.getLineHeight(UI_10_FONT_ID)) / 2, tr(STR_EOB_HOME),
                    true, EpdFontFamily::BOLD);

  const auto actions = input.mapNavigationActions();
  GUI.drawIconButtonHints(renderer, endButtonHint(actions.btn1), endButtonHint(actions.btn2),
                          endButtonHint(actions.btn3), endButtonHint(actions.btn4));
}
