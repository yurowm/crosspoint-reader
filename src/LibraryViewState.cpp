#include "LibraryViewState.h"

#include <ArduinoJson.h>
#include <Logging.h>
#include <PersistableStore.h>

#include <cstring>

namespace {
constexpr uint8_t FORMAT_VERSION = 1;
constexpr size_t MAX_FILTER_VALUES = 128;
constexpr size_t MAX_FILTER_VALUE_BYTES = 512;
constexpr size_t MAX_BOOK_PATH_BYTES = 2048;

void loadValues(const JsonVariantConst value, std::set<std::string>& output) {
  output.clear();
  const JsonArrayConst values = value.as<JsonArrayConst>();
  for (JsonVariantConst item : values) {
    if (output.size() >= MAX_FILTER_VALUES) break;
    if (!item.is<const char*>()) continue;
    const char* text = item.as<const char*>();
    const size_t length = strlen(text);
    if (length > 0 && length <= MAX_FILTER_VALUE_BYTES) output.emplace(text, length);
  }
}

void saveValues(JsonDocument& doc, const char* key, const std::set<std::string>& values) {
  JsonArray array = doc[key].to<JsonArray>();
  for (const auto& value : values) array.add(value);
}
}  // namespace

bool LibraryViewStateFile::load(LibraryViewState& state) {
  state = {};
  JsonDocument doc;
  if (!PersistableStoreBase::readDocFromFile(FILE_PATH, doc)) return false;
  if ((doc["version"] | static_cast<uint8_t>(0)) != FORMAT_VERSION) {
    LOG_ERR("LVIEW", "Unsupported library view state version");
    return false;
  }

  loadValues(doc["authors"], state.authors);
  loadValues(doc["series"], state.series);
  loadValues(doc["tags"], state.tags);

  const char* selectedPath = doc["selected"] | "";
  const size_t selectedPathLength = strlen(selectedPath);
  if (selectedPathLength > 0 && selectedPathLength <= MAX_BOOK_PATH_BYTES && selectedPath[0] == '/') {
    state.selectedBookPath.assign(selectedPath, selectedPathLength);
  }

  const uint8_t sortMode = doc["sort"] | static_cast<uint8_t>(LibrarySortMode::Title);
  if (sortMode < static_cast<uint8_t>(LibrarySortMode::Count)) {
    state.sortMode = static_cast<LibrarySortMode>(sortMode);
  }
  state.dirty = false;
  return true;
}

bool LibraryViewStateFile::save(const LibraryViewState& state) {
  JsonDocument doc;
  doc["version"] = FORMAT_VERSION;
  doc["selected"] = state.selectedBookPath;
  doc["sort"] = static_cast<uint8_t>(state.sortMode);
  saveValues(doc, "authors", state.authors);
  saveValues(doc, "series", state.series);
  saveValues(doc, "tags", state.tags);
  return PersistableStoreBase::writeDocToFile(FILE_PATH, doc);
}
