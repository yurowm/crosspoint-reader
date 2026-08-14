#include "LibraryFiltersActivity.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace {
bool matchesSelection(const std::vector<std::string>& values, const std::set<std::string>& selected) {
  if (selected.empty()) {
    return true;
  }
  return std::any_of(values.begin(), values.end(),
                     [&selected](const std::string& value) { return selected.contains(value); });
}

bool matchesAuthors(const LibraryBook& book, const std::set<std::string>& selected) {
  if (!book.authors.empty()) return matchesSelection(book.authors, selected);
  return selected.empty() || (!book.author.empty() && selected.contains(book.author));
}
}  // namespace

void LibraryFiltersActivity::onEnter() {
  Activity::onEnter();
  lockBackRelease = mappedInput.isPressed(MappedInputManager::Button::Back);
  requestUpdate();
}

std::set<std::string>& LibraryFiltersActivity::selectedValues() {
  switch (activeField) {
    case FilterField::Authors:
      return viewState.authors;
    case FilterField::Series:
      return viewState.series;
    case FilterField::Tags:
      return viewState.tags;
  }
  return viewState.authors;
}

const std::set<std::string>& LibraryFiltersActivity::selectedValues() const {
  switch (activeField) {
    case FilterField::Authors:
      return viewState.authors;
    case FilterField::Series:
      return viewState.series;
    case FilterField::Tags:
      return viewState.tags;
  }
  return viewState.authors;
}

const char* LibraryFiltersActivity::fieldName(const FilterField field) const {
  switch (field) {
    case FilterField::Authors:
      return tr(STR_LIBRARY_FILTER_AUTHOR);
    case FilterField::Series:
      return tr(STR_LIBRARY_FILTER_SERIES);
    case FilterField::Tags:
      return tr(STR_LIBRARY_FILTER_TAGS);
  }
  return "";
}

std::string LibraryFiltersActivity::fieldSummary(const FilterField field) const {
  const std::set<std::string>* selected = nullptr;
  const char* allLabel = "";
  switch (field) {
    case FilterField::Authors:
      selected = &viewState.authors;
      allLabel = tr(STR_LIBRARY_ALL_AUTHORS);
      break;
    case FilterField::Series:
      selected = &viewState.series;
      allLabel = tr(STR_LIBRARY_ALL_SERIES);
      break;
    case FilterField::Tags:
      selected = &viewState.tags;
      allLabel = tr(STR_LIBRARY_ALL_TAGS);
      break;
  }
  if (selected->empty()) return allLabel;
  if (selected->size() == 1) return *selected->begin();
  return std::to_string(selected->size()) + " " + tr(STR_LIBRARY_FILTER_SELECTED);
}

const char* LibraryFiltersActivity::sortModeName(const LibrarySortMode mode) const {
  switch (mode) {
    case LibrarySortMode::Author:
      return tr(STR_LIBRARY_SORT_AUTHOR);
    case LibrarySortMode::Series:
      return tr(STR_LIBRARY_SORT_SERIES);
    case LibrarySortMode::Title:
    case LibrarySortMode::Count:
      return tr(STR_LIBRARY_SORT_TITLE);
  }
  return tr(STR_LIBRARY_SORT_TITLE);
}

bool LibraryFiltersActivity::matchesOtherFilters(const LibraryBook& book) const {
  if (activeField != FilterField::Authors && !matchesAuthors(book, viewState.authors)) {
    return false;
  }
  if (activeField != FilterField::Series && !viewState.series.empty() && !viewState.series.contains(book.series)) {
    return false;
  }
  if (activeField != FilterField::Tags && !matchesSelection(book.tags, viewState.tags)) {
    return false;
  }
  return true;
}

void LibraryFiltersActivity::rebuildValues() {
  std::set<std::string> uniqueValues = selectedValues();
  for (const auto& book : books) {
    if (!matchesOtherFilters(book)) {
      continue;
    }

    switch (activeField) {
      case FilterField::Authors:
        uniqueValues.insert(book.authors.begin(), book.authors.end());
        if (book.authors.empty() && !book.author.empty()) {
          uniqueValues.insert(book.author);
        }
        break;
      case FilterField::Series:
        if (!book.series.empty()) {
          uniqueValues.insert(book.series);
        }
        break;
      case FilterField::Tags:
        uniqueValues.insert(book.tags.begin(), book.tags.end());
        break;
    }
  }

  values.assign(uniqueValues.begin(), uniqueValues.end());
  std::sort(values.begin(), values.end(), FsHelpers::naturalLess);
}

void LibraryFiltersActivity::openValues(const FilterField field) {
  activeField = field;
  rebuildValues();
  selectorIndex = 0;
  screen = Screen::FilterValues;
  requestUpdate();
}

void LibraryFiltersActivity::loop() {
  if (lockBackRelease) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Back)) {
      lockBackRelease = false;
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (screen != Screen::Menu) {
      const Screen previousScreen = screen;
      screen = Screen::Menu;
      selectorIndex = previousScreen == Screen::SortValues ? 3 : static_cast<size_t>(activeField);
      requestUpdate();
    } else {
      finish();
    }
    return;
  }

  const int itemCount = screen == Screen::Menu
                          ? 5
                          : (screen == Screen::SortValues ? static_cast<int>(LibrarySortMode::Count)
                                                          : static_cast<int>(values.size()) + 1);
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (screen == Screen::Menu) {
      if (selectorIndex == 0) {
        openValues(FilterField::Authors);
      } else if (selectorIndex == 1) {
        openValues(FilterField::Series);
      } else if (selectorIndex == 2) {
        openValues(FilterField::Tags);
      } else if (selectorIndex == 3) {
        selectorIndex = static_cast<size_t>(viewState.sortMode);
        screen = Screen::SortValues;
        requestUpdate();
      } else {
        viewState.clearFilters();
        requestUpdate();
      }
    } else if (screen == Screen::SortValues) {
      const auto selectedMode = static_cast<LibrarySortMode>(selectorIndex);
      if (viewState.sortMode != selectedMode) {
        viewState.sortMode = selectedMode;
        viewState.dirty = true;
      }
      requestUpdate();
    } else if (selectorIndex == 0) {
      auto& selected = selectedValues();
      if (!selected.empty()) {
        selected.clear();
        viewState.dirty = true;
      }
      requestUpdate();
    } else {
      const auto& value = values[selectorIndex - 1];
      auto& selected = selectedValues();
      if (selected.contains(value)) {
        selected.erase(value);
      } else {
        selected.insert(value);
      }
      viewState.dirty = true;
      requestUpdate();
    }
    return;
  }

  buttonNavigator.onNextRelease([this, itemCount] {
    selectorIndex = ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), itemCount);
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this, itemCount] {
    selectorIndex = ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), itemCount);
    requestUpdate();
  });
  buttonNavigator.onNextContinuous([this, itemCount] {
    selectorIndex = ButtonNavigator::nextPageIndex(static_cast<int>(selectorIndex), itemCount, 6);
    requestUpdate();
  });
  buttonNavigator.onPreviousContinuous([this, itemCount] {
    selectorIndex = ButtonNavigator::previousPageIndex(static_cast<int>(selectorIndex), itemCount, 6);
    requestUpdate();
  });
}

void LibraryFiltersActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const Rect contentRect{0, contentTop, pageWidth, contentHeight};

  if (screen == Screen::Menu) {
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_LIBRARY_OPTIONS));
    GUI.drawList(
        renderer, contentRect, 5, selectorIndex,
        [this](const int index) {
          switch (index) {
            case 0:
              return std::string(tr(STR_LIBRARY_FILTER_AUTHOR));
            case 1:
              return std::string(tr(STR_LIBRARY_FILTER_SERIES));
            case 2:
              return std::string(tr(STR_LIBRARY_FILTER_TAGS));
            case 3:
              return std::string(tr(STR_LIBRARY_SORT));
            default:
              return std::string(tr(STR_LIBRARY_RESET_FILTERS));
          }
        },
        nullptr, nullptr,
        [this](const int index) {
          if (index == 0) return fieldSummary(FilterField::Authors);
          if (index == 1) return fieldSummary(FilterField::Series);
          if (index == 2) return fieldSummary(FilterField::Tags);
          if (index == 3) return std::string(sortModeName(viewState.sortMode));
          return std::string();
        });
  } else if (screen == Screen::FilterValues) {
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, fieldName(activeField));
    GUI.drawList(
        renderer, contentRect, static_cast<int>(values.size()) + 1, selectorIndex,
        [this](const int index) {
          if (index == 0) return std::string(tr(STR_CLEAR_BUTTON));
          return values[index - 1];
        },
        nullptr,
        [this](const int index) {
          return index > 0 && selectedValues().contains(values[index - 1]) ? Check : None;
        });
  } else {
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_LIBRARY_SORT));
    GUI.drawList(
        renderer, contentRect, static_cast<int>(LibrarySortMode::Count), selectorIndex,
        [this](const int index) { return std::string(sortModeName(static_cast<LibrarySortMode>(index))); }, nullptr,
        [this](const int index) {
          return static_cast<LibrarySortMode>(index) == viewState.sortMode ? Check : None;
        });
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
