#pragma once

#include <cstddef>
#include <vector>

#include "LibraryIndex.h"
#include "activities/Activity.h"

class DeferredBooksActivity final : public Activity {
  std::vector<LibraryBook> books;
  size_t selectorIndex = 0;
  uint32_t lastNavigationRepeatTime = 0;
  bool navigationRepeated = false;
  bool lockConfirmRelease = false;

  void loadBooks();
  void openBookMenu();
  void moveSelection(bool next, bool byPage);

 public:
  DeferredBooksActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("DeferredBooks", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
