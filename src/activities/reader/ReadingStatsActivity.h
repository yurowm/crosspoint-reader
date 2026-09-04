#pragma once

#include <string>

#include "ReadingStats.h"
#include "activities/Activity.h"

class ReadingStatsActivity final : public Activity {
  std::string bookPath;
  std::string title;
  uint16_t progressBasisPoints = 0;
  ReadingStatsData stats;
  std::string currentBookTitle;
  ReadingStatsData currentBookStats;
  bool hasCurrentBook = false;

 public:
  ReadingStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  ReadingStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath, std::string title,
                       uint16_t progressBasisPoints);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  void confirmReset();
};
