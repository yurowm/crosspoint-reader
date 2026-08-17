#pragma once

#include <cstddef>
#include <string>

#include "LibraryIndex.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class LibraryBookMenuActivity final : public Activity {
  LibraryBook& book;
  ButtonNavigator buttonNavigator;
  std::string seriesText;
  std::string tagsText;
  size_t selectorIndex = 0;
  bool lockConfirmRelease = false;

  void activateSelection();
  void buildDisplayMetadata();

 public:
  LibraryBookMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, LibraryBook& book)
      : Activity("LibraryBookMenu", renderer, mappedInput), book(book) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
