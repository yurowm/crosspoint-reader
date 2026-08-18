#pragma once

#include <Arduino.h>

#include <cstdint>
#include <string>

struct ReadingStatsData {
  uint32_t sessions = 0;
  uint32_t readingSeconds = 0;
  uint32_t forwardPages = 0;
  uint32_t lastSessionSeconds = 0;
  uint32_t completedBooks = 0;
  bool finished = false;
};

class ReadingStats {
 public:
  static ReadingStats& instance();

  static bool loadBook(const std::string& path, ReadingStatsData& stats);
  static bool loadGlobal(ReadingStatsData& stats);
  static bool setBookFinished(const std::string& path, bool finished);
  static bool resetBook(const std::string& path);
  static void formatDuration(uint32_t seconds, char* buffer, size_t size);

  void startSession(const std::string& path);
  void recordPageTurn(bool forward);
  void finishSession();
  uint32_t finishSessionForSleep();
  uint32_t currentSessionSeconds() const;
  bool active() const { return active_; }

 private:
  static constexpr uint32_t MAX_ACTIVE_INTERVAL_MS = 5UL * 60UL * 1000UL;

  std::string path_;
  ReadingStatsData book_;
  ReadingStatsData global_;
  unsigned long lastInteractionMs_ = 0;
  uint32_t sessionSeconds_ = 0;
  bool active_ = false;
  bool dirty_ = false;

  ReadingStats() = default;
  void collectInterval();
  void persist();
};

#define READING_STATS ReadingStats::instance()
