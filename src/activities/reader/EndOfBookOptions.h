#pragma once

#include <atomic>
#include <string>

#include "LibraryIndex.h"

class GfxRenderer;
class MappedInputManager;

class EndOfBookOptions {
 public:
  enum class Action { None, Redraw, OpenBook, GoHome, LastPage };

  explicit EndOfBookOptions(GfxRenderer& renderer) : renderer(renderer) {}

  // Loads one recommendation on the render task, then publishes the immutable
  // result to the input task through isLoaded.
  void loadOnce(const std::string& currentBookPath);
  bool menuActive() const;
  Action handleMenuInput(const MappedInputManager& input, std::string* openPath);

  void render(GfxRenderer& renderer, const MappedInputManager& input);

 private:
  GfxRenderer& renderer;
  LibraryBook recommendation;
  int selector = 0;
  bool hasRecommendation = false;
  std::atomic<bool> isLoaded{false};

  void ensureRecommendationCover();
};
