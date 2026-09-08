#include <HalClock.h>

#include "CrossPointSettings.h"
#include "ReadingStatsCalendar.h"

bool getCurrentLocalReadingDateTime(ReadingStatsDateTime& value) {
  value = {};
  if (!halClock.getDateTime(value.date.year, value.date.month, value.date.day, value.hour, value.minute,
                            value.second)) {
    return false;
  }
  if (!value.isValid()) {
    value = {};
    return false;
  }

  const uint8_t encodedOffset = SETTINGS.clockUtcOffsetQ > 104 ? 104 : SETTINGS.clockUtcOffsetQ;
  int minutes = static_cast<int>(value.hour) * 60 + value.minute + (static_cast<int>(encodedOffset) - 48) * 15;
  while (minutes < 0) {
    addReadingStatsDays(value.date, -1);
    minutes += 24 * 60;
  }
  while (minutes >= 24 * 60) {
    addReadingStatsDays(value.date, 1);
    minutes -= 24 * 60;
  }
  value.hour = static_cast<uint8_t>(minutes / 60);
  value.minute = static_cast<uint8_t>(minutes % 60);
  return value.isValid();
}
