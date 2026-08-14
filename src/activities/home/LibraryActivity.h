#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "LibraryIndex.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

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
  enum class CoverAttemptResult { None, Attempted, Updated };

  ButtonNavigator buttonNavigator;
  std::vector<LibraryBook> books;
  size_t selectorIndex = 0;
  size_t scannedBookCount = 0;
  size_t totalBookCount = 0;
  LibraryFilterState filters;
  bool scanning = false;
  bool lockLongPressBack = false;
  bool lockNextConfirmRelease = false;
  bool indexDirty = false;

  bool scanLibrary(bool indexLoaded);
  bool scanDirectory(const std::string& path, std::vector<LibraryFileInfo>& bookFiles);
  LibraryBook loadBook(const LibraryFileInfo& file);
  CoverAttemptResult ensureNextVisibleCover();
  void openFilters();
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
