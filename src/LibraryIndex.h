#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct LibraryBook {
  std::string path;
  std::string title;
  std::string author;
  std::vector<std::string> authors;
  std::string series;
  std::string seriesIndex;
  std::string year;
  std::vector<std::string> tags;
  std::string coverBmpPath;
  uint64_t fileSize = 0;
  uint16_t modifiedDate = 0;
  uint16_t modifiedTime = 0;
  uint8_t progressPercent = 0;
  bool started = false;

  // User state is persisted separately from the metadata index.
  bool deferred = false;

  // Runtime-only guard for lazy cover generation. Failed or unavailable covers
  // are attempted at most once per LibraryActivity session.
  bool coverAttempted = false;
};

struct LibraryFileInfo {
  std::string path;
  uint64_t fileSize = 0;
  uint16_t modifiedDate = 0;
  uint16_t modifiedTime = 0;
};

class LibraryIndex {
 public:
  using BookVisitor = bool (*)(const LibraryBook& book, void* context);

  static constexpr const char* FILE_PATH = "/.crosspoint/library.idx";

  static bool load(std::vector<LibraryBook>& books);
  static bool save(const std::vector<LibraryBook>& books);
  // Reads one entry at a time and stops when visitor returns false. This keeps
  // callers that need only a few books from retaining the complete index.
  static bool visitBooks(BookVisitor visitor, void* context);

  // Best-effort helpers for mutations that happen outside LibraryActivity.
  // Missing index files and paths are successful no-ops.
  static bool updateProgress(const std::string& path, uint8_t progressPercent);
  static bool setProgressState(const std::string& path, uint8_t progressPercent, bool started);
  static bool invalidate(const std::string& path);

  static bool sourceMatches(const LibraryBook& book, const LibraryFileInfo& file);
};
