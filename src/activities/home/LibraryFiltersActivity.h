#pragma once

#include <set>
#include <string>
#include <vector>

#include "LibraryActivity.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class LibraryFiltersActivity final : public Activity {
  enum class Screen { Menu, Values };
  enum class FilterField { Authors, Series, Tags };

  const std::vector<LibraryBook>& books;
  LibraryFilterState& filters;
  ButtonNavigator buttonNavigator;
  Screen screen = Screen::Menu;
  FilterField activeField = FilterField::Authors;
  std::vector<std::string> values;
  size_t selectorIndex = 0;
  bool lockBackRelease = false;

  void openValues(FilterField field);
  void rebuildValues();
  bool matchesOtherFilters(const LibraryBook& book) const;
  std::set<std::string>& selectedValues();
  const std::set<std::string>& selectedValues() const;
  const char* fieldName(FilterField field) const;
  std::string fieldSummary(FilterField field) const;

 public:
  LibraryFiltersActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::vector<LibraryBook>& books,
                         LibraryFilterState& filters)
      : Activity("LibraryFilters", renderer, mappedInput), books(books), filters(filters) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
