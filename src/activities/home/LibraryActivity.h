#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

struct LibraryBook {
  std::string path;
  std::string title;
  std::string author;
  std::vector<std::string> authors;
  std::string series;
  std::string seriesIndex;
  std::vector<std::string> tags;
  std::string coverBmpPath;
  uint8_t progressPercent = 0;
  bool started = false;
};

struct LibraryFilterState {
  std::set<std::string> authors;
  std::set<std::string> series;
  std::set<std::string> tags;

  bool isActive() const { return !authors.empty() || !series.empty() || !tags.empty(); }
  void clear() {
    authors.clear();
    series.clear();
    tags.clear();
  }
};

class LibraryActivity final : public Activity {
  static constexpr int BOOKS_PER_PAGE = 4;

  ButtonNavigator buttonNavigator;
  std::vector<LibraryBook> books;
  size_t selectorIndex = 0;
  size_t scannedBookCount = 0;
  size_t totalBookCount = 0;
  LibraryFilterState filters;
  bool scanning = false;
  bool lockLongPressBack = false;
  bool lockNextConfirmRelease = false;

  void scanLibrary();
  void scanDirectory(const std::string& path, std::vector<std::string>& bookPaths);
  void addBook(const std::string& path);
  void openFilters();
  void drawBookCover(const LibraryBook& book, int x, int y, int width, int height) const;
  std::vector<size_t> filteredBookIndices() const;
  bool matchesFilters(const LibraryBook& book) const;

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
