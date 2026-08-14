#pragma once

#include <cstdint>
#include <set>
#include <string>

enum class LibrarySortMode : uint8_t { Title = 0, Author = 1, Series = 2, Count };

struct LibraryViewState {
  std::set<std::string> authors;
  std::set<std::string> series;
  std::set<std::string> tags;
  std::string selectedBookPath;
  LibrarySortMode sortMode = LibrarySortMode::Title;
  bool dirty = false;

  bool filtersActive() const { return !authors.empty() || !series.empty() || !tags.empty(); }
  void clearFilters() {
    if (!filtersActive()) return;
    authors.clear();
    series.clear();
    tags.clear();
    dirty = true;
  }
};

class LibraryViewStateFile {
 public:
  static constexpr const char* FILE_PATH = "/.crosspoint/library-view.json";

  static bool load(LibraryViewState& state);
  static bool save(const LibraryViewState& state);
};
