#include "CalibreSyncActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <OpdsStream.h>
#include <Epub.h>
#include <WiFi.h>

#include <algorithm>
#include <cstdio>

#include "CrossPointSettings.h"
#include "LibraryIndex.h"
#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"
#include "util/BookCacheUtils.h"
#include "util/OpdsFilename.h"
#include "util/UrlUtils.h"

namespace {
constexpr size_t MAX_SYNC_BOOKS = 2000;
constexpr size_t MAX_CATALOG_PAGES = 100;

uint32_t fnv1a(const std::string& value) {
  uint32_t hash = 2166136261u;
  for (const unsigned char c : value) {
    hash ^= c;
    hash *= 16777619u;
  }
  return hash;
}

bool containsBookId(const std::vector<OpdsEntry>& books, const std::string& id) {
  return std::any_of(books.begin(), books.end(), [&id](const OpdsEntry& book) { return book.id == id; });
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

std::string CalibreSyncActivity::catalogUrl() const {
  std::string base = serverKey();
  std::string query;
  const size_t queryAt = base.find('?');
  if (queryAt != std::string::npos) {
    query = base.substr(queryAt);
    base.erase(queryAt);
  }
  while (!base.empty() && base.back() == '/') base.pop_back();
  if (base.size() < 5 || base.compare(base.size() - 5, 5, "/opds") != 0) base += "/opds";
  return base + "/navcatalog/4f6e6577657374" + query;  // Calibre "Onewest": every book, newest first.
}

bool CalibreSyncActivity::fetchCatalog(std::vector<OpdsEntry>& books) {
  books.clear();
  std::string pageUrl = catalogUrl();
  for (size_t page = 0; page < MAX_CATALOG_PAGES && !pageUrl.empty(); ++page) {
    updateProgress(tr(STR_CALIBRE_SYNC_CATALOG), page + 1, 0);
    OpdsParser parser;
    {
      OpdsParserStream stream(parser);
      if (!HttpDownloader::fetchUrl(pageUrl, stream, server.username, server.password)) return false;
    }
    if (!parser || parser.truncated()) return false;

    const std::string next = parser.getNextPageUrl();
    auto entries = std::move(parser).getEntries();
    for (auto& entry : entries) {
      if (entry.type != OpdsEntryType::BOOK || entry.id.empty() || entry.href.empty()) continue;
      if (!containsBookId(books, entry.id)) books.push_back(std::move(entry));
      if (books.size() > MAX_SYNC_BOOKS) return false;
    }
    if (next.empty()) return true;
    const std::string resolved = UrlUtils::buildUrl(pageUrl, next);
    if (resolved == pageUrl) return false;
    pageUrl = resolved;
  }
  return pageUrl.empty();
}

std::string CalibreSyncActivity::destinationFor(const OpdsEntry& book,
                                                const std::vector<CalibreSyncRecord>& records) const {
  std::string folder = SETTINGS.opdsDownloadFolder;
  while (!folder.empty() && folder.back() == '/') folder.pop_back();
  std::string path = folder + "/" +
                     opdsBookFilename(book.author, book.title,
                                      static_cast<OpdsFilenameFormat>(SETTINGS.opdsFilenameFormat));
  const bool occupiedBySync = std::any_of(records.begin(), records.end(), [&path](const CalibreSyncRecord& record) {
    return record.path == path;
  });
  if (!Storage.exists(path.c_str()) && !occupiedBySync) return path;

  char suffix[20];
  snprintf(suffix, sizeof(suffix), " [%08lx].epub", static_cast<unsigned long>(fnv1a(book.id)));
  if (path.size() >= 5 && path.compare(path.size() - 5, 5, ".epub") == 0) path.erase(path.size() - 5);
  path += suffix;
  return path;
}

void CalibreSyncActivity::updateProgress(const std::string& text, const size_t item, const size_t total) {
  statusText = text;
  currentItem = item;
  totalItems = total;
  requestUpdateAndWait();
}

void CalibreSyncActivity::runSync() {
  std::vector<OpdsEntry> books;
  if (!fetchCatalog(books)) {
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

  bool libraryChanged = false;
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
    const bool needsDownload = isNew || old->updated != book.updated || !Storage.exists(old->path.c_str());
    if (!needsDownload) continue;

    updateProgress(book.title, i + 1, books.size());
    const std::string destination = isNew ? destinationFor(book, records) : old->path;
    const std::string temporary = destination + ".part";
    const std::string downloadUrl = UrlUtils::buildUrl(catalogUrl(), book.href);
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
    if (isNew) {
      records.push_back(CalibreSyncRecord{key, book.id, book.updated, destination});
      ++added;
    } else {
      old->updated = book.updated;
      old->path = destination;
      ++updated;
    }
    libraryChanged = true;
  }

  if (cancelRequested) {
    CalibreSyncStore::save(records);
    if (libraryChanged) Storage.remove(LibraryIndex::FILE_PATH);
    state = CANCELLED;
    requestUpdate();
    return;
  }

  for (auto it = records.begin(); it != records.end();) {
    if (it->serverUrl == key && !containsBookId(books, it->bookId)) {
      clearBookCache(it->path);
      if (!Storage.exists(it->path.c_str()) || Storage.remove(it->path.c_str())) {
        it = records.erase(it);
        ++removed;
        libraryChanged = true;
        continue;
      }
      ++errors;
    }
    ++it;
  }

  if (!CalibreSyncStore::save(records)) {
    state = FAILED;
  } else {
    if (libraryChanged) Storage.remove(LibraryIndex::FILE_PATH);
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
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) || mappedInput.wasPressed(MappedInputManager::Button::Confirm) ||
        mappedInput.wasScreenTapped(x, y)) {
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
    renderer.drawCenteredText(UI_12_FONT_ID, midY - 25, tr(STR_CALIBRE_SYNC_COMPLETE), true,
                              EpdFontFamily::BOLD);
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
