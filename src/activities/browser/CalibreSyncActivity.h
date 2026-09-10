#pragma once

#include <OpdsParser.h>

#include <string>
#include <vector>

#include "CalibreSyncStore.h"
#include "OpdsServerStore.h"
#include "activities/Activity.h"

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
  bool fetchCatalog(std::vector<OpdsEntry>& books);
  bool discoverCatalogUrl(std::string& url);
  std::string opdsRootUrl() const;
  std::string serverKey() const;
  std::string destinationFor(const OpdsEntry& book, const std::vector<CalibreSyncRecord>& records) const;
  void updateProgress(const std::string& text, size_t item, size_t total);
};
