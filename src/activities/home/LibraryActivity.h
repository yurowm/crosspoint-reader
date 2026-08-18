#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "LibraryIndex.h"
#include "LibraryViewState.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class LibraryActivity final : public Activity {
  std::vector<LibraryBook> books;
  size_t selectorIndex = 0;
  size_t scannedBookCount = 0;
  size_t totalBookCount = 0;
  uint32_t lastNavigationRepeatTime = 0;
  uint32_t estimatedCharactersPerPage = 0;
  LibraryViewState viewState;
  bool scanning = false;
  bool lockLongPressBack = false;
  bool lockNextConfirmRelease = false;
  bool indexDirty = false;
  bool navigationRepeated = false;

  bool scanLibrary(bool indexLoaded);
  bool scanDirectory(const std::string& path, std::vector<LibraryFileInfo>& bookFiles);
  LibraryBook loadBook(const LibraryFileInfo& file);
  void openFilters();
  void openBookMenu(LibraryBook& book);
  std::vector<size_t> filteredBookIndices() const;
  bool matchesFilters(const LibraryBook& book) const;
  void sortBooks();
  void restoreSelector();
  void rememberSelectedBook();
  void moveSelection(int bookCount, bool next, bool byPage);

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
