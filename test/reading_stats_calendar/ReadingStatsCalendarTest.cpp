#include <gtest/gtest.h>

#include "ReadingStatsCalendar.h"

TEST(ReadingStatsCalendar, HandlesLeapDaysAndWeekdays) {
  ReadingStatsDateTime value{{2024, 2, 28}, 23, 59, 55};
  addReadingStatsSeconds(value, 10);

  EXPECT_EQ(value.date.year, 2024);
  EXPECT_EQ(value.date.month, 2);
  EXPECT_EQ(value.date.day, 29);
  EXPECT_EQ(value.hour, 0);
  EXPECT_EQ(value.minute, 0);
  EXPECT_EQ(value.second, 5);
  EXPECT_EQ(readingStatsDayOfWeek({2000, 1, 3}), 0);
}

TEST(ReadingStatsCalendar, DayIndexIsContinuousAcrossYears) {
  EXPECT_EQ(readingStatsDayIndex({2000, 1, 1}), 0u);
  EXPECT_EQ(readingStatsDayIndex({2000, 12, 31}), 365u);
  EXPECT_EQ(readingStatsDayIndex({2001, 1, 1}), 366u);
}

TEST(ReadingStatsCalendar, RejectsInvalidDates) {
  EXPECT_FALSE(ReadingStatsDate({2026, 2, 29}).isValid());
  EXPECT_TRUE(ReadingStatsDate({2024, 2, 29}).isValid());
  EXPECT_FALSE(ReadingStatsDate({1999, 12, 31}).isValid());
}
