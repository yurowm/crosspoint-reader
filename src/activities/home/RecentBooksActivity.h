#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "LibraryIndex.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class RecentBooksActivity final : public Activity {
  enum class CoverAttemptResult { None, Attempted, Updated };

  ButtonNavigator buttonNavigator;
  std::vector<LibraryBook> recentBooks;
  size_t selectorIndex = 0;
  bool longPressFired = false;

  void loadRecentBooks();
  CoverAttemptResult ensureNextVisibleCover();
  void promptRemoveBook(const std::string& path, const std::string& title);

 public:
  explicit RecentBooksActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("RecentBooks", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
