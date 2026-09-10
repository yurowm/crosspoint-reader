#pragma once

#include <string>
#include <vector>

struct CalibreSyncRecord {
  std::string serverUrl;
  std::string bookId;
  std::string updated;
  std::string path;
};

// Persistent ownership index for full OPDS synchronization. Only paths present
// here may be removed by the synchronizer, so manually copied books are never
// treated as remote deletions.
class CalibreSyncStore {
 public:
  static constexpr const char* FILE_PATH = "/.crosspoint/calibre-sync.json";

  static bool load(std::vector<CalibreSyncRecord>& records);
  static bool save(const std::vector<CalibreSyncRecord>& records);
};
