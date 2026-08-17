#include "LibraryBookStateStore.h"

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>

void LibraryBookStateStore::toJson(JsonDocument& doc) const {
  JsonArray paths = doc["deferred"].to<JsonArray>();
  for (const auto& path : deferredPaths) {
    paths.add(path);
  }
}

bool LibraryBookStateStore::fromJson(JsonVariantConst doc) {
  deferredPaths.clear();
  const JsonArrayConst paths = doc["deferred"].as<JsonArrayConst>();
  deferredPaths.reserve(std::min(paths.size(), MAX_DEFERRED_BOOKS));
  for (const JsonVariantConst value : paths) {
    if (deferredPaths.size() >= MAX_DEFERRED_BOOKS) break;
    const char* path = value.as<const char*>();
    if (path && path[0] == '/' &&
        std::find(deferredPaths.begin(), deferredPaths.end(), path) == deferredPaths.end()) {
      deferredPaths.emplace_back(path);
    }
  }
  LOG_DBG("LBSS", "Loaded %u deferred books", static_cast<unsigned>(deferredPaths.size()));
  return true;
}

bool LibraryBookStateStore::isDeferred(const std::string& path) const {
  return std::find(deferredPaths.begin(), deferredPaths.end(), path) != deferredPaths.end();
}

bool LibraryBookStateStore::setDeferred(const std::string& path, const bool deferred) {
  const auto found = std::find(deferredPaths.begin(), deferredPaths.end(), path);
  if (deferred) {
    if (found != deferredPaths.end()) return true;
    if (deferredPaths.size() >= MAX_DEFERRED_BOOKS) {
      LOG_ERR("LBSS", "Deferred book limit reached");
      return false;
    }
    deferredPaths.push_back(path);
  } else {
    if (found == deferredPaths.end()) return true;
    deferredPaths.erase(found);
  }

  if (!saveToFile()) {
    LOG_ERR("LBSS", "Failed to persist deferred state for %s", path.c_str());
    return false;
  }
  return true;
}

bool LibraryBookStateStore::pruneMissing() {
  const size_t oldSize = deferredPaths.size();
  deferredPaths.erase(std::remove_if(deferredPaths.begin(), deferredPaths.end(), [](const std::string& path) {
                        return !Storage.exists(path.c_str());
                      }),
                      deferredPaths.end());
  return oldSize != deferredPaths.size();
}

void LibraryBookStateStore::updatePath(const std::string& oldPath, const std::string& newPath) {
  const auto found = std::find(deferredPaths.begin(), deferredPaths.end(), oldPath);
  if (found == deferredPaths.end()) return;
  *found = newPath;
  if (!saveToFile()) {
    LOG_ERR("LBSS", "Failed to persist deferred path update");
  }
}
