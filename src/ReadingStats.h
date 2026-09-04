#pragma once

#include <Arduino.h>

#include <array>
#include <cstdint>
#include <string>

struct ReadingStatsData {
  static constexpr size_t SPEED_SAMPLE_CAPACITY = 20;
  static constexpr size_t MIN_SPEED_SAMPLES = 10;

  uint32_t sessions = 0;
  uint32_t readingSeconds = 0;
  uint32_t forwardPages = 0;
  uint32_t lastSessionSeconds = 0;
  uint32_t completedBooks = 0;
  std::array<uint8_t, SPEED_SAMPLE_CAPACITY> pageSeconds{};
  uint8_t pageSampleCount = 0;
  uint8_t nextPageSample = 0;
  bool finished = false;
};

class ReadingStats {
 public:
  static constexpr uint16_t PROGRESS_COMPLETE = 10000;

  static ReadingStats& instance();

  static bool loadBook(const std::string& path, ReadingStatsData& stats);
  static bool loadGlobal(ReadingStatsData& stats);
  static bool setBookFinished(const std::string& path, bool finished);
  static bool resetBook(const std::string& path);
  static void formatDuration(uint32_t seconds, char* buffer, size_t size);
  static uint32_t readingSpeedSeconds(const ReadingStatsData& stats);
  static uint32_t remainingSeconds(const ReadingStatsData& stats, uint16_t progressBasisPoints);

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
  uint32_t pendingSleepSummarySeconds_ = 0;
  uint16_t sessionPageTurns_ = 0;
  bool active_ = false;
  bool dirty_ = false;

  ReadingStats() = default;
  uint32_t collectInterval();
  void addPageSample(uint32_t seconds);
  void persist();
};

#define READING_STATS ReadingStats::instance()
