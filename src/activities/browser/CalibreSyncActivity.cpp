#include "CalibreSyncActivity.h"

#include <ArduinoJson.h>
#include <Epub.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>
#include <mbedtls/sha256.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>

#include "CrossPointSettings.h"
#include "LibraryIndex.h"
#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"
#include "util/BookCacheUtils.h"
#include "util/StringUtils.h"
#include "util/UrlUtils.h"

namespace {
constexpr size_t MAX_SYNC_BOOKS = 2000;
constexpr size_t HASH_CHUNK = 4096;

uint32_t fnv1a(const std::string& value) {
  uint32_t hash = 2166136261u;
  for (const unsigned char c : value) {
    hash ^= c;
    hash *= 16777619u;
  }
  return hash;
}

bool containsBookId(const std::vector<CalibreManifestBook>& books, const std::string& id) {
  return std::any_of(books.begin(), books.end(), [&id](const CalibreManifestBook& book) { return book.id == id; });
}

bool validSha256(const std::string& value) {
  return value.size() == 64 && std::all_of(value.begin(), value.end(), [](const unsigned char c) {
           return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
         });
}

std::string basename(const std::string& path) {
  const size_t slash = path.find_last_of("/\\");
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

void invalidateBookCachePreservingProgress(const std::string& path) {
  Epub epub(path, "/.crosspoint");
  const std::string progressPath = epub.getCachePath() + "/progress.bin";
  uint8_t savedProgress[32];
  size_t savedSize = 0;
  HalFile input;
  if (Storage.openFileForRead("CSYNC", progressPath, input)) {
    const int bytesRead = input.read(savedProgress, sizeof(savedProgress));
    if (bytesRead > 0) savedSize = static_cast<size_t>(bytesRead);
    input.close();
  }

  epub.clearCache();
  if (savedSize == 0) return;
  epub.setupCacheDir();
  HalFile output;
  if (!Storage.openFileForWrite("CSYNC", progressPath, output) || output.write(savedProgress, savedSize) != savedSize) {
    LOG_ERR("CSYNC", "Failed to preserve progress for %s", path.c_str());
  }
  output.close();
}

void invalidateLibraryEntry(const std::string& path) {
  // Keep all unaffected metadata cached. If the targeted index rewrite fails,
  // removing the complete index is safer than retaining stale metadata.
  if (!LibraryIndex::invalidate(path)) Storage.remove(LibraryIndex::FILE_PATH);
}
}  // namespace

CalibreSyncActivity::CalibreSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, OpdsServer server)
    : Activity("CalibreSync", renderer, mappedInput), server(std::move(server)) {}

void CalibreSyncActivity::onEnter() {
  Activity::onEnter();
  if (WiFi.status() == WL_CONNECTED) {
    state = SYNCING;
    requestUpdate();
    return;
  }
  tearDownWifi = true;
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void CalibreSyncActivity::onExit() {
  Activity::onExit();
  if (tearDownWifi && WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void CalibreSyncActivity::onWifiSelectionComplete(const bool connected) {
  if (!connected) {
    finish();
    return;
  }
  state = SYNCING;
  requestUpdate();
}

std::string CalibreSyncActivity::serverKey() const {
  std::string key = server.url;
  while (!key.empty() && key.back() == '/') key.pop_back();
  return key;
}

std::string CalibreSyncActivity::syncRootUrl() const {
  std::string base = serverKey();
  const size_t queryAt = base.find('?');
  if (queryAt != std::string::npos) base.erase(queryAt);
  while (!base.empty() && base.back() == '/') base.pop_back();
  if (base.size() >= 5 && base.compare(base.size() - 5, 5, "/opds") == 0) base.erase(base.size() - 5);
  return base + "/crosspoint-sync/v1/";
}

bool CalibreSyncActivity::fetchManifest(std::vector<CalibreManifestBook>& books) {
  books.clear();
  updateProgress(tr(STR_CALIBRE_SYNC_CATALOG), 0, 0);
  std::string payload;
  if (!HttpDownloader::fetchUrl(syncRootUrl() + "manifest.json", payload, server.username, server.password)) {
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, payload) || (doc["version"] | 0) != 1) return false;
  if (!doc["books"].is<JsonArrayConst>()) return false;
  const JsonArrayConst items = doc["books"].as<JsonArrayConst>();
  if (items.size() > MAX_SYNC_BOOKS) return false;
  books.reserve(items.size());
  for (const JsonObjectConst item : items) {
    CalibreManifestBook book;
    book.id = item["path"] | "";
    book.title = basename(book.id);
    book.href = item["url"] | "";
    book.sha256 = item["sha256"] | "";
    book.size = item["size"] | 0;
    const std::string expectedHref = "books/" + book.sha256 + ".epub";
    if (book.id.empty() || book.id.front() == '/' || book.id.find("../") != std::string::npos ||
        book.href != expectedHref || !validSha256(book.sha256) || book.size == 0 || containsBookId(books, book.id)) {
      return false;
    }
    books.push_back(std::move(book));
  }
  return true;
}

std::string CalibreSyncActivity::destinationFor(const CalibreManifestBook& book,
                                                const std::vector<CalibreSyncRecord>& records) const {
  std::string folder = SETTINGS.opdsDownloadFolder;
  while (!folder.empty() && folder.back() == '/') folder.pop_back();
  std::string filename = basename(book.id);
  if (filename.size() >= 5 && filename.compare(filename.size() - 5, 5, ".epub") == 0)
    filename.resize(filename.size() - 5);
  std::string path = folder + "/" + StringUtils::sanitizeFilename(filename) + ".epub";
  const bool occupiedBySync = std::any_of(records.begin(), records.end(),
                                          [&path](const CalibreSyncRecord& record) { return record.path == path; });
  if (!Storage.exists(path.c_str()) && !occupiedBySync) return path;

  char suffix[20];
  snprintf(suffix, sizeof(suffix), " [%08lx].epub", static_cast<unsigned long>(fnv1a(book.id)));
  if (path.size() >= 5 && path.compare(path.size() - 5, 5, ".epub") == 0) path.erase(path.size() - 5);
  path += suffix;
  return path;
}

bool CalibreSyncActivity::verifyDownload(const std::string& path, const CalibreManifestBook& book) const {
  HalFile file;
  if (!Storage.openFileForRead("CSYNC", path, file) || file.size() != book.size) return false;
  auto buffer = std::unique_ptr<uint8_t[]>(new (std::nothrow) uint8_t[HASH_CHUNK]);
  if (!buffer) {
    file.close();
    return false;
  }
  mbedtls_sha256_context context;
  mbedtls_sha256_init(&context);
  mbedtls_sha256_starts(&context, 0);
  while (file.available()) {
    const int count = file.read(buffer.get(), HASH_CHUNK);
    if (count <= 0) {
      mbedtls_sha256_free(&context);
      file.close();
      return false;
    }
    mbedtls_sha256_update(&context, buffer.get(), count);
  }
  uint8_t digest[32];
  mbedtls_sha256_finish(&context, digest);
  mbedtls_sha256_free(&context);
  file.close();
  char hexadecimal[65];
  for (size_t i = 0; i < sizeof(digest); ++i) snprintf(hexadecimal + i * 2, 3, "%02x", digest[i]);
  hexadecimal[64] = '\0';
  return book.sha256 == hexadecimal;
}

void CalibreSyncActivity::updateProgress(const std::string& text, const size_t item, const size_t total) {
  statusText = text;
  currentItem = item;
  totalItems = total;
  requestUpdateAndWait();
}

void CalibreSyncActivity::runSync() {
  std::vector<CalibreManifestBook> books;
  if (!fetchManifest(books)) {
    state = FAILED;
    requestUpdate();
    return;
  }

  std::vector<CalibreSyncRecord> records;
  if (!CalibreSyncStore::load(records)) {
    state = FAILED;
    requestUpdate();
    return;
  }
  const std::string key = serverKey();
  const char* folder = SETTINGS.opdsDownloadFolder;
  if (folder[0] && !Storage.exists(folder) && !Storage.mkdir(folder)) {
    state = FAILED;
    requestUpdate();
    return;
  }

  for (size_t i = 0; i < books.size(); ++i) {
    if (cancelRequested) break;
    const auto& book = books[i];
    auto old = std::find_if(records.begin(), records.end(), [&](const CalibreSyncRecord& record) {
      return record.serverUrl == key && record.bookId == book.id;
    });
    const bool isNew = old == records.end();
    if (!isNew) {
      const std::string backup = old->path + ".bak";
      if (!Storage.exists(old->path.c_str()) && Storage.exists(backup.c_str())) {
        Storage.rename(backup.c_str(), old->path.c_str());
      } else if (Storage.exists(old->path.c_str()) && Storage.exists(backup.c_str())) {
        Storage.remove(backup.c_str());
      }
    }
    const bool needsDownload = isNew || old->updated != book.sha256 || !Storage.exists(old->path.c_str());
    if (!needsDownload) continue;

    updateProgress(book.title, i + 1, books.size());
    const std::string destination = isNew ? destinationFor(book, records) : old->path;
    const std::string temporary = destination + ".part";
    const std::string downloadUrl = UrlUtils::buildUrl(syncRootUrl(), book.href);
    const auto result = HttpDownloader::downloadToFile(
        downloadUrl, temporary,
        [this](const size_t, const size_t) {
          mappedInput.update();
          if (mappedInput.wasPressed(MappedInputManager::Button::Back) || mappedInput.wasHomeGesture()) {
            cancelRequested = true;
          }
        },
        &cancelRequested, server.username, server.password);
    if (result != HttpDownloader::OK) {
      if (result != HttpDownloader::ABORTED) ++errors;
      continue;
    }
    if (!verifyDownload(temporary, book)) {
      Storage.remove(temporary.c_str());
      ++errors;
      continue;
    }
    const std::string backup = destination + ".bak";
    const bool hadOldFile = Storage.exists(destination.c_str());
    if (hadOldFile) {
      Storage.remove(backup.c_str());
      if (!Storage.rename(destination.c_str(), backup.c_str())) {
        Storage.remove(temporary.c_str());
        ++errors;
        continue;
      }
    }
    if (!Storage.rename(temporary.c_str(), destination.c_str())) {
      Storage.remove(temporary.c_str());
      if (hadOldFile) Storage.rename(backup.c_str(), destination.c_str());
      ++errors;
      continue;
    }
    if (hadOldFile) Storage.remove(backup.c_str());
    invalidateBookCachePreservingProgress(destination);
    invalidateLibraryEntry(destination);
    if (isNew) {
      records.push_back(CalibreSyncRecord{key, book.id, book.sha256, destination});
      ++added;
    } else {
      old->updated = book.sha256;
      old->path = destination;
      ++updated;
    }
  }

  if (cancelRequested) {
    CalibreSyncStore::save(records);
    state = CANCELLED;
    requestUpdate();
    return;
  }

  // Remote deletion is the final commit step. If any download failed, keep
  // every existing synchronized book so a transient network or SD error can
  // never turn a partial refresh into data loss.
  if (errors == 0) {
    for (auto it = records.begin(); it != records.end();) {
      if (it->serverUrl == key && !containsBookId(books, it->bookId)) {
        clearBookCache(it->path);
        invalidateLibraryEntry(it->path);
        if (!Storage.exists(it->path.c_str()) || Storage.remove(it->path.c_str())) {
          it = records.erase(it);
          ++removed;
          continue;
        }
        ++errors;
      }
      ++it;
    }
  }

  if (!CalibreSyncStore::save(records)) {
    state = FAILED;
  } else {
    state = COMPLETE;
  }
  requestUpdate();
}

void CalibreSyncActivity::loop() {
  if (state == SYNCING && !started) {
    started = true;
    requestUpdateAndWait();
    runSync();
    return;
  }
  if (state == COMPLETE || state == FAILED || state == CANCELLED) {
    int x = 0;
    int y = 0;
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::Confirm) || mappedInput.wasScreenTapped(x, y)) {
      finish();
    }
  }
}

void CalibreSyncActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight}, tr(STR_CALIBRE_SYNC));

  const int midY = height / 2;
  if (state == SYNCING || state == CONNECTING) {
    renderer.drawCenteredText(UI_10_FONT_ID, midY - 15,
                              statusText.empty() ? tr(STR_CALIBRE_SYNC_PREPARING) : statusText.c_str(), true);
    if (totalItems > 0) {
      char progress[32];
      snprintf(progress, sizeof(progress), "%u / %u", static_cast<unsigned>(currentItem),
               static_cast<unsigned>(totalItems));
      renderer.drawCenteredText(UI_10_FONT_ID, midY + 15, progress);
    }
    const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state == COMPLETE) {
    renderer.drawCenteredText(UI_12_FONT_ID, midY - 25, tr(STR_CALIBRE_SYNC_COMPLETE), true, EpdFontFamily::BOLD);
    char summary[96];
    snprintf(summary, sizeof(summary), "%s: %d   %s: %d", tr(STR_CALIBRE_SYNC_ADDED), added,
             tr(STR_CALIBRE_SYNC_UPDATED), updated);
    renderer.drawCenteredText(UI_10_FONT_ID, midY + 5, summary);
    snprintf(summary, sizeof(summary), "%s: %d   %s: %d", tr(STR_CALIBRE_SYNC_REMOVED), removed,
             tr(STR_CALIBRE_SYNC_ERRORS), errors);
    renderer.drawCenteredText(UI_10_FONT_ID, midY + 30, summary);
  } else {
    renderer.drawCenteredText(UI_12_FONT_ID, midY - 10,
                              state == CANCELLED ? tr(STR_CALIBRE_SYNC_CANCELLED) : tr(STR_CALIBRE_SYNC_FAILED), true,
                              EpdFontFamily::BOLD);
  }
  if (state == COMPLETE || state == FAILED || state == CANCELLED) {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
  renderer.displayBuffer();
}
