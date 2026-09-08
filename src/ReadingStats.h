#pragma once

#include <Arduino.h>

#include <array>
#include <cstdint>
#include <string>

#include "ReadingStatsCalendar.h"

struct ReadingStatsData {
  static constexpr size_t SPEED_SAMPLE_CAPACITY = 20;
  static constexpr size_t MIN_SPEED_SAMPLES = 10;
  static constexpr size_t HISTORY_DAYS = 366;
  static constexpr size_t HISTORY_BYTES = (HISTORY_DAYS + 7) / 8;
  static constexpr size_t DAILY_DURATION_DAYS = 31;
  static constexpr size_t TIME_BUCKETS = 4;
  static constexpr size_t WEEK_DAYS = 7;

  uint32_t sessions = 0;
  uint32_t readingSeconds = 0;
  uint32_t forwardPages = 0;
  uint32_t lastSessionSeconds = 0;
  uint32_t completedBooks = 0;
  std::array<uint8_t, SPEED_SAMPLE_CAPACITY> pageSeconds{};
  std::array<uint32_t, TIME_BUCKETS> timeOfDaySeconds{};
  std::array<uint32_t, WEEK_DAYS> dayOfWeekSeconds{};
  std::array<uint8_t, HISTORY_BYTES> readingDays{};
  // Exact recent totals use five-second units; the bit history retains a full year for streaks.
  std::array<uint16_t, DAILY_DURATION_DAYS> dailyFiveSeconds{};
  uint32_t historyAnchorDay = 0;  // Encoded as day index + 1; zero means no dated history.
  uint32_t startDay = 0;
  uint32_t finishedDay = 0;
  uint8_t pageSampleCount = 0;
  uint8_t nextPageSample = 0;
  bool finished = false;
};

static_assert(sizeof(ReadingStatsData) <= 256, "ReadingStatsData must remain safe for small task stacks");

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
  static uint32_t todaySeconds(const ReadingStatsData& stats);
  static uint32_t recentDaysSeconds(const ReadingStatsData& stats, uint16_t days);
  static uint16_t currentStreak(const ReadingStatsData& stats);
  static uint16_t longestStreak(const ReadingStatsData& stats);

  void startSession(const std::string& path);
  void recordPageTurn(bool forward);
  void pauseTiming();
  void resumeTiming();
  void finishSession();
  uint32_t finishSessionForSleep();
  uint32_t currentSessionSeconds() const;
  bool active() const { return active_; }

 private:
  std::string path_;
  ReadingStatsData book_;
  ReadingStatsData global_;
  ReadingStatsDateTime sessionStartDateTime_;
  unsigned long lastInteractionMs_ = 0;
  uint32_t sessionSeconds_ = 0;
  uint16_t sessionRemainderMs_ = 0;
  uint32_t pendingSleepSummarySeconds_ = 0;
  uint16_t sessionPageTurns_ = 0;
  bool active_ = false;
  bool dirty_ = false;
  bool paused_ = false;
  bool hasSessionStartDateTime_ = false;

  ReadingStats() = default;
  uint32_t collectInterval();
  void addPageSample(uint32_t seconds);
  static void recordDatedSpan(ReadingStatsData& stats, const ReadingStatsDateTime& start, uint32_t seconds);
  void persist();
};

#define READING_STATS ReadingStats::instance()
