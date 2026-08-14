#include "RecentBooksActivity.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>
#include <Xtc.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/BookListItem.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr unsigned long LONG_PRESS_MS = 1000;

ButtonHint recentButtonHint(const MappedInputManager::NavigationAction action, const bool hasBooks) {
  switch (action) {
    case MappedInputManager::NavigationAction::Back:
      return {.icon = House};
    case MappedInputManager::NavigationAction::Confirm:
      return hasBooks ? ButtonHint{.icon = Check, .holdIcon = Trash} : ButtonHint{};
    case MappedInputManager::NavigationAction::Previous:
      return hasBooks ? ButtonHint{.icon = ChevronUp} : ButtonHint{};
    case MappedInputManager::NavigationAction::Next:
      return hasBooks ? ButtonHint{.icon = ChevronDown} : ButtonHint{};
    default:
      return {};
  }
}

bool enrichRecentBook(const LibraryBook& indexed, void* rawBooks) {
  auto& books = *static_cast<std::vector<LibraryBook>*>(rawBooks);
  const auto recent =
      std::find_if(books.begin(), books.end(), [&indexed](const LibraryBook& book) { return book.path == indexed.path; });
  if (recent == books.end()) return true;

  if (!indexed.title.empty()) recent->title = indexed.title;
  if (!indexed.author.empty()) recent->author = indexed.author;
  recent->series = indexed.series;
  recent->seriesIndex = indexed.seriesIndex;
  if (!indexed.coverBmpPath.empty()) recent->coverBmpPath = indexed.coverBmpPath;
  recent->progressPercent = indexed.progressPercent;
  recent->started = indexed.started;
  return true;
}
}  // namespace

void RecentBooksActivity::loadRecentBooks() {
  recentBooks.clear();
  const auto& storedBooks = RECENT_BOOKS.getBooks();
  recentBooks.reserve(storedBooks.size());
  for (const RecentBook& stored : storedBooks) {
    recentBooks.emplace_back();
    LibraryBook& book = recentBooks.back();
    book.path = stored.path;
    book.title = stored.title;
    book.author = stored.author;
    book.coverBmpPath = stored.coverBmpPath;
  }
  LibraryIndex::visitBooks(&enrichRecentBook, &recentBooks);
}

void RecentBooksActivity::onEnter() {
  Activity::onEnter();
  selectorIndex = 0;
  longPressFired = false;

  if (RECENT_BOOKS.pruneMissing()) {
    RECENT_BOOKS.saveToFile();
  }
  loadRecentBooks();
  requestUpdate();
}

void RecentBooksActivity::onExit() {
  Activity::onExit();
  recentBooks.clear();
}

RecentBooksActivity::CoverAttemptResult RecentBooksActivity::ensureNextVisibleCover() {
  if (recentBooks.empty()) return CoverAttemptResult::None;

  const size_t pageStart = selectorIndex / BookListItem::ITEMS_PER_PAGE * BookListItem::ITEMS_PER_PAGE;
  const size_t pageEnd = std::min(recentBooks.size(), pageStart + BookListItem::ITEMS_PER_PAGE);
  bool attemptedAny = false;
  for (size_t index = pageStart; index < pageEnd; index++) {
    LibraryBook& book = recentBooks[index];
    if (book.coverAttempted) continue;
    book.coverAttempted = true;
    attemptedAny = true;

    if (!book.coverBmpPath.empty()) {
      book.coverBmpPath = UITheme::getCoverThumbPath(book.coverBmpPath, BookListItem::DEFAULT_COVER_CACHE_HEIGHT);
      if (Storage.exists(book.coverBmpPath.c_str())) continue;
      book.coverBmpPath.clear();
    }

    if (FsHelpers::hasEpubExtension(book.path)) {
      Epub epub(book.path, "/.crosspoint");
      if (epub.loadMetadata()) {
        const std::string thumb = epub.getThumbBmpPath(BookListItem::DEFAULT_COVER_CACHE_HEIGHT);
        if (Storage.exists(thumb.c_str()) || epub.generateThumbBmp(BookListItem::DEFAULT_COVER_CACHE_HEIGHT)) {
          book.coverBmpPath = thumb;
          return CoverAttemptResult::Updated;
        }
      }
    } else if (FsHelpers::hasXtcExtension(book.path)) {
      Xtc xtc(book.path, "/.crosspoint");
      if (xtc.load()) {
        const std::string thumb = xtc.getThumbBmpPath(BookListItem::DEFAULT_COVER_CACHE_HEIGHT);
        if (Storage.exists(thumb.c_str()) || xtc.generateThumbBmp(BookListItem::DEFAULT_COVER_CACHE_HEIGHT)) {
          book.coverBmpPath = thumb;
          return CoverAttemptResult::Updated;
        }
      }
    }
    return CoverAttemptResult::Attempted;
  }
  return attemptedAny ? CoverAttemptResult::Attempted : CoverAttemptResult::None;
}

void RecentBooksActivity::promptRemoveBook(const std::string& path, const std::string& title) {
  auto confirmation = makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_REMOVE_FROM_RECENTS), title);
  if (!confirmation) {
    LOG_ERR("RBA", "OOM: ConfirmationActivity");
    return;
  }

  startActivityForResult(std::move(confirmation), [this, path](const ActivityResult& result) {
    if (result.isCancelled || !RECENT_BOOKS.removeByPath(path)) return;
    loadRecentBooks();
    if (recentBooks.empty()) {
      selectorIndex = 0;
    } else if (selectorIndex >= recentBooks.size()) {
      selectorIndex = recentBooks.size() - 1;
    }
  });
}

void RecentBooksActivity::loop() {
  if (longPressFired) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Confirm)) longPressFired = false;
    return;
  }

  if (!recentBooks.empty() && mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      mappedInput.getHeldTime() >= LONG_PRESS_MS) {
    longPressFired = true;
    promptRemoveBook(recentBooks[selectorIndex].path, recentBooks[selectorIndex].title);
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome();
    return;
  }
  if (recentBooks.empty()) return;

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int itemHeight = BookListItem::rowHeight(contentHeight);
  const int pageStart =
      static_cast<int>(selectorIndex / BookListItem::ITEMS_PER_PAGE) * BookListItem::ITEMS_PER_PAGE;
  const int visibleRows = std::min(BookListItem::ITEMS_PER_PAGE, static_cast<int>(recentBooks.size()) - pageStart);

  int touchX = 0;
  int touchY = 0;
  if (mappedInput.wasScreenLongPress(touchX, touchY) && touchY >= contentTop) {
    const int row = (touchY - contentTop) / (itemHeight + BookListItem::ROW_GAP);
    const int rowOffset = (touchY - contentTop) % (itemHeight + BookListItem::ROW_GAP);
    if (touchX >= 0 && touchX < renderer.getScreenWidth() && row >= 0 && row < visibleRows &&
        rowOffset < itemHeight) {
      selectorIndex = static_cast<size_t>(pageStart + row);
      promptRemoveBook(recentBooks[selectorIndex].path, recentBooks[selectorIndex].title);
      return;
    }
  }

  int row = -1;
  const auto touch = mappedInput.rowTouch(row, contentTop, itemHeight + BookListItem::ROW_GAP, visibleRows, 0,
                                          renderer.getScreenWidth(), itemHeight);
  if (touch != MappedInputManager::RowTouch::None) {
    selectorIndex = static_cast<size_t>(pageStart + row);
    if (touch == MappedInputManager::RowTouch::Tap) {
      onSelectBook(recentBooks[selectorIndex].path);
    } else {
      requestUpdate();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    onSelectBook(recentBooks[selectorIndex].path);
    return;
  }

  const int bookCount = static_cast<int>(recentBooks.size());
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    selectorIndex =
        ButtonNavigator::nextPageIndex(static_cast<int>(selectorIndex), bookCount, BookListItem::ITEMS_PER_PAGE);
    requestUpdate();
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    selectorIndex =
        ButtonNavigator::previousPageIndex(static_cast<int>(selectorIndex), bookCount, BookListItem::ITEMS_PER_PAGE);
    requestUpdate();
    return;
  }

  bool navigationHandled = false;
  buttonNavigator.onNextRelease([this, bookCount, &navigationHandled] {
    selectorIndex = ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), bookCount);
    navigationHandled = true;
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this, bookCount, &navigationHandled] {
    selectorIndex = ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), bookCount);
    navigationHandled = true;
    requestUpdate();
  });
  buttonNavigator.onNextContinuous([this, bookCount, &navigationHandled] {
    selectorIndex =
        ButtonNavigator::nextPageIndex(static_cast<int>(selectorIndex), bookCount, BookListItem::ITEMS_PER_PAGE);
    navigationHandled = true;
    requestUpdate();
  });
  buttonNavigator.onPreviousContinuous([this, bookCount, &navigationHandled] {
    selectorIndex =
        ButtonNavigator::previousPageIndex(static_cast<int>(selectorIndex), bookCount, BookListItem::ITEMS_PER_PAGE);
    navigationHandled = true;
    requestUpdate();
  });
  if (navigationHandled) return;

  if (ensureNextVisibleCover() != CoverAttemptResult::None) requestUpdate();
}

void RecentBooksActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_MENU_RECENT_BOOKS));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  if (recentBooks.empty()) {
    UITheme::drawCenteredText(renderer, Rect{0, contentTop, pageWidth, contentHeight}, UI_12_FONT_ID,
                              contentTop + contentHeight / 2, tr(STR_NO_RECENT_BOOKS));
  } else {
    const int itemHeight = BookListItem::rowHeight(contentHeight);
    const int pageStart =
        static_cast<int>(selectorIndex / BookListItem::ITEMS_PER_PAGE) * BookListItem::ITEMS_PER_PAGE;
    const int sidePadding = metrics.contentSidePadding;
    const int itemWidth = pageWidth - sidePadding * 2;

    for (int index = pageStart; index < static_cast<int>(recentBooks.size()) &&
                                index < pageStart + BookListItem::ITEMS_PER_PAGE;
         index++) {
      const int itemY = contentTop + (index - pageStart) * (itemHeight + BookListItem::ROW_GAP);
      const LibraryBook& book = recentBooks[index];
      BookListItem::draw(renderer, book, sidePadding, itemY, itemWidth, itemHeight,
                         index == static_cast<int>(selectorIndex));
    }
  }

  const auto actions = mappedInput.mapNavigationActions();
  const bool hasBooks = !recentBooks.empty();
  GUI.drawIconButtonHints(renderer, recentButtonHint(actions.btn1, hasBooks), recentButtonHint(actions.btn2, hasBooks),
                          recentButtonHint(actions.btn3, hasBooks), recentButtonHint(actions.btn4, hasBooks));
  renderer.displayBuffer();
}
