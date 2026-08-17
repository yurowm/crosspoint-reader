#pragma once

#include <string>
#include <utility>

#include "activities/Activity.h"

class BookCoverActivity final : public Activity {
  std::string bookPath;
  std::string coverPath;
  bool coverReady = false;

  bool prepareCover();

 public:
  BookCoverActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath)
      : Activity("BookCover", renderer, mappedInput), bookPath(std::move(bookPath)) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
