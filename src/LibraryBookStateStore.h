#pragma once

#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <cstddef>
#include <string>
#include <vector>

class LibraryBookStateStore : public PersistableStore<LibraryBookStateStore> {
  std::vector<std::string> deferredPaths;
  static constexpr size_t MAX_DEFERRED_BOOKS = 4096;

  LibraryBookStateStore() = default;
  ~LibraryBookStateStore() = default;

  friend class PersistableStore<LibraryBookStateStore>;

 public:
  static const char* getFilePath() { return "/.crosspoint/library-state.json"; }

  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  bool isDeferred(const std::string& path) const;
  bool setDeferred(const std::string& path, bool deferred);
  bool pruneMissing();
  void updatePath(const std::string& oldPath, const std::string& newPath);

  const std::vector<std::string>& getDeferredPaths() const { return deferredPaths; }
};

#define LIBRARY_BOOK_STATE LibraryBookStateStore::getInstance()
