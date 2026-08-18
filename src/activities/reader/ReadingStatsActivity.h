#pragma once

#include <string>

#include "ReadingStats.h"
#include "activities/Activity.h"

class ReadingStatsActivity final : public Activity {
  std::string bookPath;
  std::string title;
  uint8_t progressPercent = 0;
  ReadingStatsData stats;

 public:
  ReadingStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  ReadingStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath,
                       std::string title, uint8_t progressPercent);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
