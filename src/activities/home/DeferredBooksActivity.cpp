#include "DeferredBooksActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <algorithm>

#include "LibraryBookMenuActivity.h"
#include "LibraryBookStateStore.h"
#include "MappedInputManager.h"
#include "components/BookListItem.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr unsigned long CONFIRM_HOLD_MS = 1000;
constexpr unsigned long NAVIGATION_REPEAT_START_MS = 500;
constexpr unsigned long NAVIGATION_REPEAT_INTERVAL_MS = 500;

std::string filenameStem(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  const size_t start = slash == std::string::npos ? 0 : slash + 1;
  const size_t dot = path.find_last_of('.');
  return dot == std::string::npos || dot <= start ? path.substr(start) : path.substr(start, dot - start);
}

bool enrichBook(const LibraryBook& indexed, void* rawBooks) {
  auto& books = *static_cast<std::vector<LibraryBook>*>(rawBooks);
  const auto found =
      std::find_if(books.begin(), books.end(), [&indexed](const LibraryBook& book) { return book.path == indexed.path; });
  if (found == books.end()) return true;
  *found = indexed;
  found->deferred = true;
  return true;
}

ButtonHint deferredButtonHint(const MappedInputManager::NavigationAction action, const bool hasBooks) {
  switch (action) {
    case MappedInputManager::NavigationAction::Back:
      return {.icon = NavigateBack};
    case MappedInputManager::NavigationAction::Confirm:
      return hasBooks ? ButtonHint{.icon = Check} : ButtonHint{};
    case MappedInputManager::NavigationAction::Previous:
      return hasBooks ? ButtonHint{.icon = ChevronLeft} : ButtonHint{};
    case MappedInputManager::NavigationAction::Next:
      return hasBooks ? ButtonHint{.icon = ChevronRight} : ButtonHint{};
    default:
      return {};
  }
}
}  // namespace

void DeferredBooksActivity::loadBooks() {
  books.clear();
  const auto& paths = LIBRARY_BOOK_STATE.getDeferredPaths();
  books.reserve(paths.size());
  for (const auto& path : paths) {
    if (!Storage.exists(path.c_str())) continue;
    LibraryBook book;
    book.path = path;
    book.title = filenameStem(path);
    book.deferred = true;
    books.push_back(std::move(book));
  }
  LibraryIndex::visitBooks(&enrichBook, &books);
}

void DeferredBooksActivity::onEnter() {
  Activity::onEnter();
  selectorIndex = 0;
  lockConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  if (LIBRARY_BOOK_STATE.pruneMissing() && !LIBRARY_BOOK_STATE.saveToFile()) {
    LOG_ERR("DBA", "Failed to persist deferred-book cleanup");
  }
  loadBooks();
  requestUpdate();
}

void DeferredBooksActivity::onExit() {
  Activity::onExit();
  books.clear();
}

void DeferredBooksActivity::openBookMenu() {
  if (books.empty()) return;
  lockConfirmRelease = true;
  auto menu = makeUniqueNoThrow<LibraryBookMenuActivity>(renderer, mappedInput, books[selectorIndex]);
  if (!menu) {
    LOG_ERR("DBA", "OOM: LibraryBookMenuActivity");
    lockConfirmRelease = false;
    return;
  }
  startActivityForResult(std::move(menu), [this](const ActivityResult&) {
    const std::string selectedPath = selectorIndex < books.size() ? books[selectorIndex].path : std::string();
    loadBooks();
    const auto selected = std::find_if(books.begin(), books.end(), [&selectedPath](const LibraryBook& book) {
      return book.path == selectedPath;
    });
    selectorIndex = selected == books.end() ? std::min(selectorIndex, books.empty() ? size_t{0} : books.size() - 1)
                                            : static_cast<size_t>(selected - books.begin());
    lockConfirmRelease = false;
    requestUpdate();
  });
}

void DeferredBooksActivity::moveSelection(const bool next, const bool byPage) {
  const int count = static_cast<int>(books.size());
  const int current = static_cast<int>(selectorIndex);
  selectorIndex = byPage
                      ? (next ? ButtonNavigator::nextPageIndex(current, count, BookListItem::ITEMS_PER_PAGE)
                              : ButtonNavigator::previousPageIndex(current, count, BookListItem::ITEMS_PER_PAGE))
                      : (next ? ButtonNavigator::nextIndex(current, count)
                              : ButtonNavigator::previousIndex(current, count));
  requestUpdate();
}

void DeferredBooksActivity::loop() {
  if (lockConfirmRelease) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Confirm)) lockConfirmRelease = false;
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome();
    return;
  }
  if (books.empty()) return;

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (mappedInput.getHeldTime() < CONFIRM_HOLD_MS) {
      openBookMenu();
    }
    return;
  }

  const auto handleRelease = [this](const MappedInputManager::Button button, const bool next, const bool byPage) {
    if (!mappedInput.wasReleased(button)) return false;
    if (!navigationRepeated) moveSelection(next, byPage);
    navigationRepeated = false;
    lastNavigationRepeatTime = 0;
    return true;
  };
  if (handleRelease(MappedInputManager::Button::Down, true, false) ||
      handleRelease(MappedInputManager::Button::Up, false, false) ||
      handleRelease(MappedInputManager::Button::Right, true, true) ||
      handleRelease(MappedInputManager::Button::Left, false, true)) {
    return;
  }

  const unsigned long now = millis();
  if (mappedInput.getHeldTime() > NAVIGATION_REPEAT_START_MS &&
      now - lastNavigationRepeatTime > NAVIGATION_REPEAT_INTERVAL_MS) {
    if (mappedInput.isPressed(MappedInputManager::Button::Down) ||
        mappedInput.isPressed(MappedInputManager::Button::Right)) {
      navigationRepeated = true;
      lastNavigationRepeatTime = now;
      moveSelection(true, true);
      return;
    }
    if (mappedInput.isPressed(MappedInputManager::Button::Up) ||
        mappedInput.isPressed(MappedInputManager::Button::Left)) {
      navigationRepeated = true;
      lastNavigationRepeatTime = now;
      moveSelection(false, true);
      return;
    }
  }

}

void DeferredBooksActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  char pageCounter[24] = {};
  const char* subtitle = nullptr;
  if (!books.empty()) {
    const size_t pageCount = (books.size() + BookListItem::ITEMS_PER_PAGE - 1) / BookListItem::ITEMS_PER_PAGE;
    snprintf(pageCounter, sizeof(pageCounter), "%u / %u",
             static_cast<unsigned>(selectorIndex / BookListItem::ITEMS_PER_PAGE + 1),
             static_cast<unsigned>(pageCount));
    subtitle = pageCounter;
  }
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_DEFERRED_BOOKS),
                 subtitle);

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  if (books.empty()) {
    UITheme::drawCenteredText(renderer, Rect{0, contentTop, pageWidth, contentHeight}, UI_12_FONT_ID,
                              contentTop + contentHeight / 2, tr(STR_NO_DEFERRED_BOOKS));
  } else {
    const int rowHeight = BookListItem::rowHeight(contentHeight);
    const int pageStart =
        static_cast<int>(selectorIndex / BookListItem::ITEMS_PER_PAGE) * BookListItem::ITEMS_PER_PAGE;
    const int sidePadding = metrics.contentSidePadding;
    const int rowWidth = pageWidth - sidePadding * 2;
    for (int index = pageStart; index < static_cast<int>(books.size()) &&
                                index < pageStart + BookListItem::ITEMS_PER_PAGE;
         ++index) {
      const int rowY = contentTop + (index - pageStart) * (rowHeight + BookListItem::ROW_GAP);
      BookListItem::draw(renderer, books[index], sidePadding, rowY, rowWidth, rowHeight,
                         index == static_cast<int>(selectorIndex), false);
    }
  }

  const auto actions = mappedInput.mapNavigationActions();
  const bool hasBooks = !books.empty();
  GUI.drawIconButtonHints(renderer, deferredButtonHint(actions.btn1, hasBooks),
                          deferredButtonHint(actions.btn2, hasBooks), deferredButtonHint(actions.btn3, hasBooks),
                          deferredButtonHint(actions.btn4, hasBooks));
  renderer.displayBuffer();
}
