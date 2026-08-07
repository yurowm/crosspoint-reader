#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

struct LibraryBook {
  std::string path;
  std::string title;
  std::string author;
  std::string series;
  std::string seriesIndex;
  std::string coverBmpPath;
  uint8_t progressPercent = 0;
  bool started = false;
};

class LibraryActivity final : public Activity {
  static constexpr int BOOKS_PER_PAGE = 4;

  ButtonNavigator buttonNavigator;
  std::vector<LibraryBook> books;
  size_t selectorIndex = 0;
  size_t scannedBookCount = 0;
  size_t totalBookCount = 0;
  bool scanning = false;
  bool lockNextConfirmRelease = false;

  void scanLibrary();
  void scanDirectory(const std::string& path, std::vector<std::string>& bookPaths);
  void addBook(const std::string& path);
  void drawBookCover(const LibraryBook& book, int x, int y, int width, int height) const;

  static std::string filenameStem(const std::string& path);
  static bool readEpubProgress(const class Epub& epub, uint8_t& progressPercent);
  static bool readXtcProgress(const class Xtc& xtc, uint8_t& progressPercent);

 public:
  explicit LibraryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Library", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
