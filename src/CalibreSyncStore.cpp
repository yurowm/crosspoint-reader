#include "CalibreSyncStore.h"

#include <ArduinoJson.h>
#include <PersistableStore.h>

bool CalibreSyncStore::load(std::vector<CalibreSyncRecord>& records) {
  records.clear();
  JsonDocument doc;
  if (!PersistableStoreBase::readDocFromFile(FILE_PATH, doc)) return true;

  const JsonArrayConst items = doc["books"].as<JsonArrayConst>();
  records.reserve(items.size());
  for (const JsonObjectConst item : items) {
    CalibreSyncRecord record;
    record.serverUrl = item["server"] | "";
    record.bookId = item["id"] | "";
    record.updated = item["updated"] | "";
    record.path = item["path"] | "";
    if (!record.serverUrl.empty() && !record.bookId.empty() && !record.path.empty()) {
      records.push_back(std::move(record));
    }
  }
  return true;
}

bool CalibreSyncStore::save(const std::vector<CalibreSyncRecord>& records) {
  JsonDocument doc;
  doc["version"] = 1;
  JsonArray items = doc["books"].to<JsonArray>();
  for (const auto& record : records) {
    JsonObject item = items.add<JsonObject>();
    item["server"] = record.serverUrl;
    item["id"] = record.bookId;
    item["updated"] = record.updated;
    item["path"] = record.path;
  }
  return PersistableStoreBase::writeDocToFile(FILE_PATH, doc);
}
