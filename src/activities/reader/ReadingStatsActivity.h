#pragma once

#include <string>

#include "ReadingStats.h"
#include "activities/Activity.h"

class ReadingStatsActivity final : public Activity {
  std::string bookPath;
  std::string title;
  uint16_t progressBasisPoints = 0;
  uint32_t estimatedPageCount = 0;
  ReadingStatsData stats;

 public:
  ReadingStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  ReadingStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath, std::string title,
                       uint16_t progressBasisPoints, uint32_t estimatedPageCount = 0);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  void confirmReset();
};
