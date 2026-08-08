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
  return std::any_of(values.begin(), values.end(), [&selected](const std::string& value) {
    return selected.contains(value);
  });
}

std::vector<std::string> authorValues(const LibraryBook& book) {
  if (!book.authors.empty()) {
    return book.authors;
  }
  return book.author.empty() ? std::vector<std::string>{} : std::vector<std::string>{book.author};
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
      return filters.authors;
    case FilterField::Series:
      return filters.series;
    case FilterField::Tags:
      return filters.tags;
  }
  return filters.authors;
}

const std::set<std::string>& LibraryFiltersActivity::selectedValues() const {
  switch (activeField) {
    case FilterField::Authors:
      return filters.authors;
    case FilterField::Series:
      return filters.series;
    case FilterField::Tags:
      return filters.tags;
  }
  return filters.authors;
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
      selected = &filters.authors;
      allLabel = tr(STR_LIBRARY_ALL_AUTHORS);
      break;
    case FilterField::Series:
      selected = &filters.series;
      allLabel = tr(STR_LIBRARY_ALL_SERIES);
      break;
    case FilterField::Tags:
      selected = &filters.tags;
      allLabel = tr(STR_LIBRARY_ALL_TAGS);
      break;
  }
  return selected->empty() ? allLabel : std::to_string(selected->size()) + " " + tr(STR_LIBRARY_FILTER_SELECTED);
}

bool LibraryFiltersActivity::matchesOtherFilters(const LibraryBook& book) const {
  if (activeField != FilterField::Authors && !matchesSelection(authorValues(book), filters.authors)) {
    return false;
  }
  if (activeField != FilterField::Series && !filters.series.empty() && !filters.series.contains(book.series)) {
    return false;
  }
  if (activeField != FilterField::Tags && !matchesSelection(book.tags, filters.tags)) {
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
  screen = Screen::Values;
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
    if (screen == Screen::Values) {
      screen = Screen::Menu;
      selectorIndex = static_cast<size_t>(activeField);
      requestUpdate();
    } else {
      finish();
    }
    return;
  }

  const int itemCount = screen == Screen::Menu ? 4 : static_cast<int>(values.size()) + 2;
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (screen == Screen::Menu) {
      if (selectorIndex == 0) {
        openValues(FilterField::Authors);
      } else if (selectorIndex == 1) {
        openValues(FilterField::Series);
      } else if (selectorIndex == 2) {
        openValues(FilterField::Tags);
      } else {
        filters.clear();
        requestUpdate();
      }
    } else if (selectorIndex == 0) {
      screen = Screen::Menu;
      selectorIndex = static_cast<size_t>(activeField);
      requestUpdate();
    } else if (selectorIndex == 1) {
      selectedValues().clear();
      requestUpdate();
    } else {
      const auto& value = values[selectorIndex - 2];
      auto& selected = selectedValues();
      if (selected.contains(value)) {
        selected.erase(value);
      } else {
        selected.insert(value);
      }
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
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_LIBRARY_FILTERS));
    GUI.drawList(
        renderer, contentRect, 4, selectorIndex,
        [this](const int index) {
          switch (index) {
            case 0:
              return std::string(tr(STR_LIBRARY_FILTER_AUTHOR));
            case 1:
              return std::string(tr(STR_LIBRARY_FILTER_SERIES));
            case 2:
              return std::string(tr(STR_LIBRARY_FILTER_TAGS));
            default:
              return std::string(tr(STR_LIBRARY_RESET_FILTERS));
          }
        },
        nullptr, nullptr,
        [this](const int index) {
          if (index == 0) return fieldSummary(FilterField::Authors);
          if (index == 1) return fieldSummary(FilterField::Series);
          if (index == 2) return fieldSummary(FilterField::Tags);
          return std::string();
        });
  } else {
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, fieldName(activeField));
    GUI.drawList(
        renderer, contentRect, static_cast<int>(values.size()) + 2, selectorIndex,
        [this](const int index) {
          if (index == 0) return std::string(tr(STR_DONE));
          if (index == 1) return std::string(tr(STR_CLEAR_BUTTON));
          return values[index - 2];
        },
        nullptr, nullptr,
        [this](const int index) {
          return index >= 2 && selectedValues().contains(values[index - 2]) ? std::string("[x]") : std::string();
        });
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
