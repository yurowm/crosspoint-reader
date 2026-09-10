#pragma once

#include <string>
#include <vector>

#include "CalibreSyncStore.h"
#include "OpdsServerStore.h"
#include "activities/Activity.h"

struct CalibreManifestBook {
  std::string id;
  std::string title;
  std::string href;
  std::string sha256;
  size_t size = 0;
};

class CalibreSyncActivity final : public Activity {
 public:
  CalibreSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, OpdsServer server);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool skipLoopDelay() override { return true; }
  bool preventAutoSleep() override { return true; }

 private:
  enum State { CONNECTING, SYNCING, COMPLETE, FAILED, CANCELLED };

  OpdsServer server;
  State state = CONNECTING;
  bool started = false;
  bool cancelRequested = false;
  bool tearDownWifi = false;
  std::string statusText;
  size_t currentItem = 0;
  size_t totalItems = 0;
  int added = 0;
  int updated = 0;
  int removed = 0;
  int errors = 0;

  void onWifiSelectionComplete(bool connected);
  void runSync();
  bool fetchManifest(std::vector<CalibreManifestBook>& books);
  std::string syncRootUrl() const;
  std::string serverKey() const;
  std::string destinationFor(const CalibreManifestBook& book, const std::vector<CalibreSyncRecord>& records) const;
  bool verifyDownload(const std::string& path, const CalibreManifestBook& book) const;
  void updateProgress(const std::string& text, size_t item, size_t total);
};
