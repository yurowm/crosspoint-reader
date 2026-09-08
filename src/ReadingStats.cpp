#include "ReadingStats.h"

#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {
constexpr char STATS_DIR[] = "/.crosspoint/reading-stats";
constexpr char GLOBAL_PATH[] = "/.crosspoint/reading-stats/global.bin";
constexpr uint32_t MAGIC = 0x53545243;  // CRTS
constexpr uint8_t VERSION = 3;
constexpr size_t V1_WIRE_SIZE = 30;
constexpr size_t V2_WIRE_SIZE = 52;
constexpr size_t TIME_BUCKETS_OFFSET = 48;
constexpr size_t WEEK_DAYS_OFFSET = 64;
constexpr size_t HISTORY_ANCHOR_OFFSET = 92;
constexpr size_t READING_DAYS_OFFSET = 96;
constexpr size_t DAILY_HISTORY_OFFSET = READING_DAYS_OFFSET + ReadingStatsData::HISTORY_BYTES;
constexpr size_t START_DAY_OFFSET = DAILY_HISTORY_OFFSET + ReadingStatsData::DAILY_DURATION_DAYS * 2;
constexpr size_t FINISHED_DAY_OFFSET = START_DAY_OFFSET + 4;
constexpr size_t WIRE_SIZE = FINISHED_DAY_OFFSET + 8;
constexpr uint32_t MIN_VALID_PAGE_SECONDS = 5;
constexpr uint32_t MAX_VALID_PAGE_SECONDS = 2 * 60;
std::array<uint8_t, WIRE_SIZE> wireBuffer;

uint64_t pathHash(const std::string& path) {
  uint64_t hash = 1469598103934665603ULL;
  for (const unsigned char ch : path) {
    hash ^= ch;
    hash *= 1099511628211ULL;
  }
  return hash;
}

void bookStatsPath(const std::string& path, char* output, const size_t size) {
  snprintf(output, size, "%s/%016llx.bin", STATS_DIR, static_cast<unsigned long long>(pathHash(path)));
}

uint32_t read32(const uint8_t* data) {
  return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
         (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
}

void write32(uint8_t* data, const uint32_t value) {
  data[0] = value & 0xff;
  data[1] = (value >> 8) & 0xff;
  data[2] = (value >> 16) & 0xff;
  data[3] = (value >> 24) & 0xff;
}

uint16_t read16(const uint8_t* data) { return static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8); }

void write16(uint8_t* data, const uint16_t value) {
  data[0] = value & 0xff;
  data[1] = value >> 8;
}

uint32_t checksum(const uint8_t* data, const size_t size) {
  uint32_t value = 2166136261UL;
  for (size_t i = 0; i < size; i++) {
    value ^= data[i];
    value *= 16777619UL;
  }
  return value;
}

bool loadFile(const char* path, ReadingStatsData& stats) {
  stats = {};
  HalFile file;
  if (!Storage.openFileForRead("RSTAT", path, file)) return false;
  auto& data = wireBuffer;
  data.fill(0);
  const int bytesRead = file.read(data.data(), data.size());
  if (bytesRead < 5 || read32(data.data()) != MAGIC) {
    LOG_ERR("RSTAT", "Invalid stats file: %s", path);
    return false;
  }

  const uint8_t version = data[4];
  const size_t wireSize = version == 1         ? V1_WIRE_SIZE
                          : version == 2       ? V2_WIRE_SIZE
                          : version == VERSION ? WIRE_SIZE
                                               : 0;
  if (wireSize == 0 || bytesRead != static_cast<int>(wireSize) ||
      read32(data.data() + wireSize - 4) != checksum(data.data(), wireSize - 4)) {
    LOG_ERR("RSTAT", "Invalid stats file: %s", path);
    return false;
  }

  stats.sessions = read32(data.data() + 5);
  stats.readingSeconds = read32(data.data() + 9);
  stats.forwardPages = read32(data.data() + 13);
  stats.lastSessionSeconds = read32(data.data() + 17);
  stats.completedBooks = read32(data.data() + 21);
  stats.finished = data[25] != 0;
  if (version >= 2) {
    stats.pageSampleCount = std::min<uint8_t>(data[26], ReadingStatsData::SPEED_SAMPLE_CAPACITY);
    stats.nextPageSample = data[27] % ReadingStatsData::SPEED_SAMPLE_CAPACITY;
    std::copy_n(data.data() + 28, ReadingStatsData::SPEED_SAMPLE_CAPACITY, stats.pageSeconds.begin());
  }
  if (version >= 3) {
    for (size_t i = 0; i < stats.timeOfDaySeconds.size(); i++)
      stats.timeOfDaySeconds[i] = read32(data.data() + TIME_BUCKETS_OFFSET + i * 4);
    for (size_t i = 0; i < stats.dayOfWeekSeconds.size(); i++)
      stats.dayOfWeekSeconds[i] = read32(data.data() + WEEK_DAYS_OFFSET + i * 4);
    stats.historyAnchorDay = read32(data.data() + HISTORY_ANCHOR_OFFSET);
    std::copy_n(data.data() + READING_DAYS_OFFSET, stats.readingDays.size(), stats.readingDays.begin());
    for (size_t i = 0; i < stats.dailyFiveSeconds.size(); i++)
      stats.dailyFiveSeconds[i] = read16(data.data() + DAILY_HISTORY_OFFSET + i * 2);
    stats.startDay = read32(data.data() + START_DAY_OFFSET);
    stats.finishedDay = read32(data.data() + FINISHED_DAY_OFFSET);
  }
  return true;
}

bool saveFile(const char* path, const ReadingStatsData& stats) {
  if (!Storage.exists(STATS_DIR) && !Storage.mkdir(STATS_DIR)) return false;
  if (Storage.exists(path)) {
    HalFile existing;
    uint8_t header[5];
    if (Storage.openFileForRead("RSTAT", path, existing) && existing.read(header, sizeof(header)) == sizeof(header) &&
        read32(header) == MAGIC && header[4] > VERSION) {
      LOG_ERR("RSTAT", "Refusing to overwrite newer stats version: %s", path);
      return false;
    }
  }
  auto& data = wireBuffer;
  data.fill(0);
  write32(data.data(), MAGIC);
  data[4] = VERSION;
  write32(data.data() + 5, stats.sessions);
  write32(data.data() + 9, stats.readingSeconds);
  write32(data.data() + 13, stats.forwardPages);
  write32(data.data() + 17, stats.lastSessionSeconds);
  write32(data.data() + 21, stats.completedBooks);
  data[25] = stats.finished ? 1 : 0;
  data[26] = std::min<uint8_t>(stats.pageSampleCount, ReadingStatsData::SPEED_SAMPLE_CAPACITY);
  data[27] = stats.nextPageSample % ReadingStatsData::SPEED_SAMPLE_CAPACITY;
  std::copy(stats.pageSeconds.begin(), stats.pageSeconds.end(), data.begin() + 28);
  for (size_t i = 0; i < stats.timeOfDaySeconds.size(); i++)
    write32(data.data() + TIME_BUCKETS_OFFSET + i * 4, stats.timeOfDaySeconds[i]);
  for (size_t i = 0; i < stats.dayOfWeekSeconds.size(); i++)
    write32(data.data() + WEEK_DAYS_OFFSET + i * 4, stats.dayOfWeekSeconds[i]);
  write32(data.data() + HISTORY_ANCHOR_OFFSET, stats.historyAnchorDay);
  std::copy(stats.readingDays.begin(), stats.readingDays.end(), data.begin() + READING_DAYS_OFFSET);
  for (size_t i = 0; i < stats.dailyFiveSeconds.size(); i++)
    write16(data.data() + DAILY_HISTORY_OFFSET + i * 2, stats.dailyFiveSeconds[i]);
  write32(data.data() + START_DAY_OFFSET, stats.startDay);
  write32(data.data() + FINISHED_DAY_OFFSET, stats.finishedDay);
  write32(data.data() + WIRE_SIZE - 4, checksum(data.data(), WIRE_SIZE - 4));

  char temp[96];
  snprintf(temp, sizeof(temp), "%s.tmp", path);
  HalFile file;
  if (!Storage.openFileForWrite("RSTAT", temp, file) ||
      file.write(data.data(), data.size()) != static_cast<int>(data.size())) {
    Storage.remove(temp);
    return false;
  }
  file.close();
  if (Storage.exists(path)) Storage.remove(path);
  return Storage.rename(temp, path);
}

uint32_t saturatedAdd(const uint32_t lhs, const uint32_t rhs) {
  return rhs > std::numeric_limits<uint32_t>::max() - lhs ? std::numeric_limits<uint32_t>::max() : lhs + rhs;
}

uint32_t secondsUntilBoundary(const ReadingStatsDateTime& value) {
  const uint32_t secondOfDay = static_cast<uint32_t>(value.hour) * 3600 + value.minute * 60 + value.second;
  uint32_t boundary = 86400;
  if (value.hour < 5)
    boundary = 5 * 3600;
  else if (value.hour < 12)
    boundary = 12 * 3600;
  else if (value.hour < 17)
    boundary = 17 * 3600;
  else if (value.hour < 21)
    boundary = 21 * 3600;
  return std::max<uint32_t>(1, boundary - secondOfDay);
}

uint8_t timeBucket(const uint8_t hour) {
  if (hour >= 5 && hour < 12) return 0;
  if (hour >= 12 && hour < 17) return 1;
  if (hour >= 17 && hour < 21) return 2;
  return 3;
}

void advanceHistory(ReadingStatsData& stats, const uint32_t encodedDay) {
  if (stats.historyAnchorDay == 0) {
    stats.historyAnchorDay = encodedDay;
    return;
  }
  if (encodedDay <= stats.historyAnchorDay) return;
  const uint32_t shift = encodedDay - stats.historyAnchorDay;
  if (shift >= stats.dailyFiveSeconds.size()) {
    stats.dailyFiveSeconds.fill(0);
  } else {
    for (size_t i = stats.dailyFiveSeconds.size() - shift; i-- > 0;)
      stats.dailyFiveSeconds[i + shift] = stats.dailyFiveSeconds[i];
    std::fill_n(stats.dailyFiveSeconds.begin(), shift, 0);
  }
  if (shift >= ReadingStatsData::HISTORY_DAYS) {
    stats.readingDays.fill(0);
  } else {
    const auto wasSet = [&stats](const size_t bit) {
      return (stats.readingDays[bit / 8] & static_cast<uint8_t>(1U << (bit % 8))) != 0;
    };
    const auto set = [&stats](const size_t bit, const bool value) {
      const uint8_t mask = static_cast<uint8_t>(1U << (bit % 8));
      if (value)
        stats.readingDays[bit / 8] |= mask;
      else
        stats.readingDays[bit / 8] &= static_cast<uint8_t>(~mask);
    };
    for (size_t bit = ReadingStatsData::HISTORY_DAYS - shift; bit-- > 0;) set(bit + shift, wasSet(bit));
    for (size_t bit = 0; bit < shift; bit++) set(bit, false);
  }
  stats.historyAnchorDay = encodedDay;
}

bool historyDaySet(const ReadingStatsData& stats, const size_t offset) {
  return offset < ReadingStatsData::HISTORY_DAYS &&
         (stats.readingDays[offset / 8] & static_cast<uint8_t>(1U << (offset % 8))) != 0;
}

void markHistoryDay(ReadingStatsData& stats, const size_t offset) {
  if (offset < ReadingStatsData::HISTORY_DAYS)
    stats.readingDays[offset / 8] |= static_cast<uint8_t>(1U << (offset % 8));
}

uint32_t currentEncodedDay() {
  ReadingStatsDateTime now;
  return getCurrentLocalReadingDateTime(now) ? readingStatsDayIndex(now.date) + 1 : 0;
}
}  // namespace

ReadingStats& ReadingStats::instance() {
  static ReadingStats stats;
  return stats;
}

bool ReadingStats::loadBook(const std::string& path, ReadingStatsData& stats) {
  ReadingStats& live = instance();
  if (live.active_ && live.path_ == path) {
    stats = live.book_;
    const uint32_t activeSeconds = live.currentSessionSeconds();
    stats.readingSeconds = saturatedAdd(stats.readingSeconds, activeSeconds);
    if (live.hasSessionStartDateTime_) recordDatedSpan(stats, live.sessionStartDateTime_, activeSeconds);
    return true;
  }
  char filePath[72];
  bookStatsPath(path, filePath, sizeof(filePath));
  return loadFile(filePath, stats);
}

bool ReadingStats::loadGlobal(ReadingStatsData& stats) {
  ReadingStats& live = instance();
  if (live.active_) {
    stats = live.global_;
    const uint32_t activeSeconds = live.currentSessionSeconds();
    stats.readingSeconds = saturatedAdd(stats.readingSeconds, activeSeconds);
    if (live.hasSessionStartDateTime_) recordDatedSpan(stats, live.sessionStartDateTime_, activeSeconds);
    return true;
  }
  return loadFile(GLOBAL_PATH, stats);
}

bool ReadingStats::setBookFinished(const std::string& path, const bool finished) {
  ReadingStats& live = instance();
  ReadingStatsData stats;
  if (live.active_ && live.path_ == path) {
    stats = live.book_;
  } else {
    loadBook(path, stats);
  }
  if (stats.finished == finished) return true;
  stats.finished = finished;
  ReadingStatsDateTime now;
  stats.finishedDay = finished && getCurrentLocalReadingDateTime(now) ? readingStatsDayIndex(now.date) + 1 : 0;
  char filePath[72];
  bookStatsPath(path, filePath, sizeof(filePath));
  if (!saveFile(filePath, stats)) return false;

  if (live.active_ && live.path_ == path) {
    live.book_.finished = finished;
    live.book_.finishedDay = stats.finishedDay;
  }
  stats = {};
  if (live.active_) {
    stats = live.global_;
  } else {
    loadGlobal(stats);
  }
  if (finished) {
    stats.completedBooks = saturatedAdd(stats.completedBooks, 1);
  } else if (stats.completedBooks > 0) {
    stats.completedBooks--;
  }
  if (live.active_) live.global_.completedBooks = stats.completedBooks;
  return saveFile(GLOBAL_PATH, stats);
}

bool ReadingStats::resetBook(const std::string& path) {
  ReadingStatsData cleared;
  loadBook(path, cleared);
  const bool wasFinished = cleared.finished;
  cleared = {};
  cleared.finished = wasFinished;

  ReadingStats& live = instance();
  if (live.path_ == path) {
    live.book_ = cleared;
    live.sessionSeconds_ = 0;
    live.sessionRemainderMs_ = 0;
    live.pendingSleepSummarySeconds_ = 0;
    live.sessionPageTurns_ = 0;
    live.lastInteractionMs_ = millis();
    live.dirty_ = false;
  }
  char filePath[72];
  bookStatsPath(path, filePath, sizeof(filePath));
  if (cleared.finished) return saveFile(filePath, cleared);
  return !Storage.exists(filePath) || Storage.remove(filePath);
}

void ReadingStats::formatDuration(const uint32_t seconds, char* buffer, const size_t size) {
  const uint32_t days = seconds / 86400;
  const uint32_t hours = (seconds / 3600) % 24;
  const uint32_t minutes = (seconds / 60) % 60;
  const uint32_t remainingSeconds = seconds % 60;
  if (days > 0) {
    snprintf(buffer, size, hours > 0 ? tr(STR_DURATION_DAYS_HOURS) : tr(STR_DURATION_DAYS),
             static_cast<unsigned long>(days), static_cast<unsigned long>(hours));
  } else if (hours > 0) {
    snprintf(buffer, size, minutes > 0 ? tr(STR_DURATION_HOURS_MINUTES) : tr(STR_DURATION_HOURS),
             static_cast<unsigned long>(hours), static_cast<unsigned long>(minutes));
  } else if (minutes > 0) {
    snprintf(buffer, size, remainingSeconds > 0 ? tr(STR_DURATION_MINUTES_SECONDS) : tr(STR_DURATION_MINUTES),
             static_cast<unsigned long>(minutes), static_cast<unsigned long>(remainingSeconds));
  } else {
    snprintf(buffer, size, tr(STR_DURATION_SECONDS), static_cast<unsigned long>(remainingSeconds));
  }
}

uint32_t ReadingStats::readingSpeedSeconds(const ReadingStatsData& stats) {
  if (stats.pageSampleCount < ReadingStatsData::MIN_SPEED_SAMPLES) return 0;
  uint32_t total = 0;
  for (size_t i = 0; i < stats.pageSampleCount; i++) total += stats.pageSeconds[i];
  return (total + stats.pageSampleCount / 2) / stats.pageSampleCount;
}

uint32_t ReadingStats::remainingSeconds(const ReadingStatsData& stats, const uint16_t progressBasisPoints) {
  if (stats.readingSeconds == 0 || progressBasisPoints == 0 || progressBasisPoints >= PROGRESS_COMPLETE) return 0;

  const uint64_t estimate = (static_cast<uint64_t>(stats.readingSeconds) * (PROGRESS_COMPLETE - progressBasisPoints) +
                             progressBasisPoints / 2) /
                            progressBasisPoints;
  return static_cast<uint32_t>(std::min<uint64_t>(estimate, std::numeric_limits<uint32_t>::max()));
}

uint32_t ReadingStats::todaySeconds(const ReadingStatsData& stats) {
  const uint32_t today = currentEncodedDay();
  if (today == 0 || stats.historyAnchorDay == 0 || today > stats.historyAnchorDay) return 0;
  const uint32_t offset = stats.historyAnchorDay - today;
  return offset < stats.dailyFiveSeconds.size() ? static_cast<uint32_t>(stats.dailyFiveSeconds[offset]) * 5 : 0;
}

uint32_t ReadingStats::recentDaysSeconds(const ReadingStatsData& stats, const uint16_t days) {
  const uint32_t today = currentEncodedDay();
  if (today == 0 || stats.historyAnchorDay == 0 || days == 0 || today > stats.historyAnchorDay + 1) return 0;
  const uint32_t firstOffset = stats.historyAnchorDay > today ? stats.historyAnchorDay - today : 0;
  uint32_t total = 0;
  const size_t end = std::min<size_t>(stats.dailyFiveSeconds.size(), firstOffset + days);
  for (size_t i = firstOffset; i < end; i++) total = saturatedAdd(total, stats.dailyFiveSeconds[i] * 5UL);
  return total;
}

uint16_t ReadingStats::currentStreak(const ReadingStatsData& stats) {
  const uint32_t today = currentEncodedDay();
  if (today == 0 || stats.historyAnchorDay == 0 || today > stats.historyAnchorDay + 1) return 0;
  size_t offset = stats.historyAnchorDay > today ? stats.historyAnchorDay - today : 0;
  if (offset == 0 && stats.historyAnchorDay == today && !historyDaySet(stats, 0)) offset = 1;
  uint16_t result = 0;
  for (; offset < ReadingStatsData::HISTORY_DAYS && historyDaySet(stats, offset); offset++) result++;
  return result;
}

uint16_t ReadingStats::longestStreak(const ReadingStatsData& stats) {
  uint16_t longest = 0;
  uint16_t current = 0;
  for (size_t offset = 0; offset < ReadingStatsData::HISTORY_DAYS; offset++) {
    if (!historyDaySet(stats, offset)) {
      current = 0;
    } else {
      longest = std::max<uint16_t>(longest, ++current);
    }
  }
  return longest;
}

void ReadingStats::recordDatedSpan(ReadingStatsData& stats, const ReadingStatsDateTime& start, const uint32_t seconds) {
  if (!start.isValid() || seconds == 0) return;
  ReadingStatsDateTime cursor = start;
  uint32_t remaining = seconds;
  if (stats.startDay == 0) stats.startDay = readingStatsDayIndex(start.date) + 1;
  while (remaining > 0) {
    const uint32_t segment = std::min(remaining, secondsUntilBoundary(cursor));
    const uint32_t encodedDay = readingStatsDayIndex(cursor.date) + 1;
    advanceHistory(stats, encodedDay);
    if (encodedDay <= stats.historyAnchorDay) {
      const uint32_t offset = stats.historyAnchorDay - encodedDay;
      markHistoryDay(stats, offset);
      if (offset < stats.dailyFiveSeconds.size()) {
        const uint32_t units = std::min<uint32_t>((segment + 4) / 5, std::numeric_limits<uint16_t>::max());
        stats.dailyFiveSeconds[offset] = static_cast<uint16_t>(
            std::min<uint32_t>(std::numeric_limits<uint16_t>::max(), stats.dailyFiveSeconds[offset] + units));
      }
    }
    const uint8_t bucket = timeBucket(cursor.hour);
    const uint8_t weekday = readingStatsDayOfWeek(cursor.date);
    stats.timeOfDaySeconds[bucket] = saturatedAdd(stats.timeOfDaySeconds[bucket], segment);
    stats.dayOfWeekSeconds[weekday] = saturatedAdd(stats.dayOfWeekSeconds[weekday], segment);
    remaining -= segment;
    addReadingStatsSeconds(cursor, segment);
  }
}

void ReadingStats::startSession(const std::string& path) {
  if (active_) finishSession();
  path_ = path;
  loadBook(path_, book_);
  loadGlobal(global_);
  lastInteractionMs_ = millis();
  sessionSeconds_ = 0;
  sessionRemainderMs_ = 0;
  sessionPageTurns_ = 0;
  hasSessionStartDateTime_ = getCurrentLocalReadingDateTime(sessionStartDateTime_);
  active_ = true;
  paused_ = false;
  dirty_ = false;
}

uint32_t ReadingStats::collectInterval() {
  if (!active_ || paused_) return 0;
  const unsigned long now = millis();
  const unsigned long elapsed = now - lastInteractionMs_;
  lastInteractionMs_ = now;
  const uint64_t totalMs = static_cast<uint64_t>(elapsed) + sessionRemainderMs_;
  const uint32_t seconds = totalMs / 1000UL;
  sessionSeconds_ = saturatedAdd(sessionSeconds_, seconds);
  sessionRemainderMs_ = totalMs % 1000UL;
  return seconds;
}

void ReadingStats::pauseTiming() {
  if (!active_ || paused_) return;
  collectInterval();
  paused_ = true;
}

void ReadingStats::resumeTiming() {
  if (!active_ || !paused_) return;
  lastInteractionMs_ = millis();
  paused_ = false;
}

void ReadingStats::addPageSample(const uint32_t seconds) {
  if (seconds < MIN_VALID_PAGE_SECONDS || seconds > MAX_VALID_PAGE_SECONDS) return;
  book_.pageSeconds[book_.nextPageSample] = static_cast<uint8_t>(seconds);
  book_.nextPageSample = (book_.nextPageSample + 1) % ReadingStatsData::SPEED_SAMPLE_CAPACITY;
  if (book_.pageSampleCount < ReadingStatsData::SPEED_SAMPLE_CAPACITY) book_.pageSampleCount++;
}

void ReadingStats::recordPageTurn(const bool forward) {
  if (!active_ || paused_) return;
  const uint32_t pageSeconds = collectInterval();
  sessionPageTurns_++;
  if (forward) {
    book_.forwardPages = saturatedAdd(book_.forwardPages, 1);
    global_.forwardPages = saturatedAdd(global_.forwardPages, 1);
    addPageSample(pageSeconds);
  }
  dirty_ = true;
}

uint32_t ReadingStats::currentSessionSeconds() const {
  if (!active_ || paused_) return sessionSeconds_;
  const unsigned long elapsed = millis() - lastInteractionMs_;
  return saturatedAdd(sessionSeconds_,
                      static_cast<uint32_t>((static_cast<uint64_t>(elapsed) + sessionRemainderMs_) / 1000UL));
}

void ReadingStats::persist() {
  if (!dirty_ || path_.empty()) return;
  char filePath[72];
  bookStatsPath(path_, filePath, sizeof(filePath));
  if (!saveFile(filePath, book_)) LOG_ERR("RSTAT", "Failed to save book stats");
  if (!saveFile(GLOBAL_PATH, global_)) LOG_ERR("RSTAT", "Failed to save global stats");
  dirty_ = false;
}

void ReadingStats::finishSession() {
  if (!active_) return;
  collectInterval();
  active_ = false;
  paused_ = false;
  if (sessionSeconds_ < 60 && sessionPageTurns_ == 0) {
    dirty_ = false;
    return;
  }
  book_.sessions = saturatedAdd(book_.sessions, 1);
  global_.sessions = saturatedAdd(global_.sessions, 1);
  book_.readingSeconds = saturatedAdd(book_.readingSeconds, sessionSeconds_);
  global_.readingSeconds = saturatedAdd(global_.readingSeconds, sessionSeconds_);
  if (hasSessionStartDateTime_) {
    recordDatedSpan(book_, sessionStartDateTime_, sessionSeconds_);
    recordDatedSpan(global_, sessionStartDateTime_, sessionSeconds_);
  }
  book_.lastSessionSeconds = sessionSeconds_;
  global_.lastSessionSeconds = sessionSeconds_;
  pendingSleepSummarySeconds_ = saturatedAdd(pendingSleepSummarySeconds_, sessionSeconds_);
  dirty_ = true;
  persist();
}

uint32_t ReadingStats::finishSessionForSleep() {
  if (active_) finishSession();
  const uint32_t summarySeconds = pendingSleepSummarySeconds_;
  pendingSleepSummarySeconds_ = 0;
  return summarySeconds;
}
