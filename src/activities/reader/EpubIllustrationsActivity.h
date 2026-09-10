#pragma once

#include <Epub.h>

#include <memory>
#include <string>
#include <vector>

#include "activities/Activity.h"

class EpubIllustrationsActivity final : public Activity {
 public:
  EpubIllustrationsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::shared_ptr<Epub> epub);
  EpubIllustrationsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& bookPath);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  std::shared_ptr<Epub> sharedEpub;
  std::unique_ptr<Epub> ownedEpub;
  Epub* epub = nullptr;
  std::string bookPath;
  std::vector<std::string> illustrations;
  std::string extractedPath;
  std::string convertedPath;
  int currentIndex = 0;
  int pagesUntilFullRefresh = 1;

  bool extractCurrent();
  bool convertCurrent();
  bool drawCurrent();
  void turn(int direction);
  void clearExtracted();
};
