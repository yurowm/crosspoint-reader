#pragma once

#include <cstdint>

struct ReadingStatsDate {
  uint16_t year = 0;
  uint8_t month = 0;
  uint8_t day = 0;

  bool isValid() const;
};

struct ReadingStatsDateTime {
  ReadingStatsDate date;
  uint8_t hour = 0;
  uint8_t minute = 0;
  uint8_t second = 0;

  bool isValid() const { return date.isValid(); }
};

bool getCurrentLocalReadingDateTime(ReadingStatsDateTime& value);
uint32_t readingStatsDayIndex(const ReadingStatsDate& date);
uint8_t readingStatsDayOfWeek(const ReadingStatsDate& date);
void addReadingStatsDays(ReadingStatsDate& value, int days);
void addReadingStatsSeconds(ReadingStatsDateTime& value, uint32_t seconds);
