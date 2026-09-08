#include "ReadingStatsCalendar.h"

namespace {
bool isLeapYear(const uint16_t year) { return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0; }

uint8_t daysInMonth(const uint16_t year, const uint8_t month) {
  static constexpr uint8_t DAYS[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12) return 0;
  return month == 2 && isLeapYear(year) ? 29 : DAYS[month - 1];
}

void addDay(ReadingStatsDate& date, const int direction) {
  if (direction > 0) {
    if (date.day < daysInMonth(date.year, date.month)) {
      date.day++;
    } else {
      date.day = 1;
      if (++date.month > 12) {
        date.month = 1;
        date.year++;
      }
    }
  } else if (date.day > 1) {
    date.day--;
  } else {
    if (date.month == 1) {
      date.month = 12;
      date.year--;
    } else {
      date.month--;
    }
    date.day = daysInMonth(date.year, date.month);
  }
}
}  // namespace

bool ReadingStatsDate::isValid() const {
  const uint8_t monthDays = daysInMonth(year, month);
  return year >= 2000 && year <= 2099 && monthDays > 0 && day >= 1 && day <= monthDays;
}

uint32_t readingStatsDayIndex(const ReadingStatsDate& date) {
  uint32_t result = 0;
  for (uint16_t year = 2000; year < date.year; year++) result += isLeapYear(year) ? 366 : 365;
  for (uint8_t month = 1; month < date.month; month++) result += daysInMonth(date.year, month);
  return result + date.day - 1;
}

uint8_t readingStatsDayOfWeek(const ReadingStatsDate& date) {
  return static_cast<uint8_t>((readingStatsDayIndex(date) + 5) % 7);  // Monday = 0; 2000-01-01 was Saturday.
}

void addReadingStatsDays(ReadingStatsDate& value, int days) {
  if (!value.isValid()) return;
  while (days > 0) {
    addDay(value, 1);
    days--;
  }
  while (days < 0) {
    addDay(value, -1);
    days++;
  }
}

void addReadingStatsSeconds(ReadingStatsDateTime& value, const uint32_t seconds) {
  if (!value.isValid()) return;
  uint32_t secondOfDay =
      static_cast<uint32_t>(value.hour) * 3600 + static_cast<uint32_t>(value.minute) * 60 + value.second + seconds;
  uint32_t days = secondOfDay / 86400;
  secondOfDay %= 86400;
  while (days-- > 0) addDay(value.date, 1);
  value.hour = secondOfDay / 3600;
  value.minute = (secondOfDay / 60) % 60;
  value.second = secondOfDay % 60;
}
