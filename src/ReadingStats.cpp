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
constexpr uint8_t VERSION = 1;
constexpr size_t WIRE_SIZE = 30;

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
  std::array<uint8_t, WIRE_SIZE> data{};
  if (file.read(data.data(), data.size()) != static_cast<int>(data.size()) || read32(data.data()) != MAGIC ||
      data[4] != VERSION || read32(data.data() + WIRE_SIZE - 4) != checksum(data.data(), WIRE_SIZE - 4)) {
    LOG_ERR("RSTAT", "Invalid stats file: %s", path);
    return false;
  }
  stats.sessions = read32(data.data() + 5);
  stats.readingSeconds = read32(data.data() + 9);
  stats.forwardPages = read32(data.data() + 13);
  stats.lastSessionSeconds = read32(data.data() + 17);
  stats.completedBooks = read32(data.data() + 21);
  stats.finished = data[25] != 0;
  return true;
}

bool saveFile(const char* path, const ReadingStatsData& stats) {
  if (!Storage.exists(STATS_DIR) && !Storage.mkdir(STATS_DIR)) return false;
  std::array<uint8_t, WIRE_SIZE> data{};
  write32(data.data(), MAGIC);
  data[4] = VERSION;
  write32(data.data() + 5, stats.sessions);
  write32(data.data() + 9, stats.readingSeconds);
  write32(data.data() + 13, stats.forwardPages);
  write32(data.data() + 17, stats.lastSessionSeconds);
  write32(data.data() + 21, stats.completedBooks);
  data[25] = stats.finished ? 1 : 0;
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
}  // namespace

ReadingStats& ReadingStats::instance() {
  static ReadingStats stats;
  return stats;
}

bool ReadingStats::loadBook(const std::string& path, ReadingStatsData& stats) {
  ReadingStats& live = instance();
  if (live.active_ && live.path_ == path) {
    stats = live.book_;
    stats.readingSeconds = saturatedAdd(stats.readingSeconds, live.currentSessionSeconds());
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
    stats.readingSeconds = saturatedAdd(stats.readingSeconds, live.currentSessionSeconds());
    return true;
  }
  return loadFile(GLOBAL_PATH, stats);
}

bool ReadingStats::setBookFinished(const std::string& path, const bool finished) {
  ReadingStatsData stats;
  loadBook(path, stats);
  if (stats.finished == finished) return true;
  stats.finished = finished;
  ReadingStatsData global;
  loadGlobal(global);
  if (finished) {
    global.completedBooks = saturatedAdd(global.completedBooks, 1);
  } else if (global.completedBooks > 0) {
    global.completedBooks--;
  }
  char filePath[72];
  bookStatsPath(path, filePath, sizeof(filePath));
  return saveFile(filePath, stats) && saveFile(GLOBAL_PATH, global);
}

bool ReadingStats::resetBook(const std::string& path) {
  char filePath[72];
  bookStatsPath(path, filePath, sizeof(filePath));
  return !Storage.exists(filePath) || Storage.remove(filePath);
}

void ReadingStats::formatDuration(const uint32_t seconds, char* buffer, const size_t size) {
  if (seconds < 60) {
    snprintf(buffer, size, "%s", tr(STR_DURATION_UNDER_MINUTE));
    return;
  }
  const uint32_t minutes = seconds / 60;
  const uint32_t hours = minutes / 60;
  if (hours > 0) {
    snprintf(buffer, size, tr(STR_DURATION_HOURS_MINUTES), static_cast<unsigned long>(hours),
             static_cast<unsigned long>(minutes % 60));
  } else {
    snprintf(buffer, size, tr(STR_DURATION_MINUTES), static_cast<unsigned long>(minutes));
  }
}

void ReadingStats::startSession(const std::string& path) {
  if (active_) finishSession();
  path_ = path;
  loadBook(path_, book_);
  loadGlobal(global_);
  book_.sessions = saturatedAdd(book_.sessions, 1);
  global_.sessions = saturatedAdd(global_.sessions, 1);
  lastInteractionMs_ = millis();
  sessionSeconds_ = 0;
  active_ = true;
  dirty_ = true;
}

void ReadingStats::collectInterval() {
  if (!active_) return;
  const unsigned long now = millis();
  const unsigned long elapsed = now - lastInteractionMs_;
  lastInteractionMs_ = now;
  if (elapsed > MAX_ACTIVE_INTERVAL_MS) return;
  sessionSeconds_ = saturatedAdd(sessionSeconds_, elapsed / 1000UL);
}

void ReadingStats::recordPageTurn(const bool forward) {
  if (!active_) return;
  collectInterval();
  if (forward) {
    book_.forwardPages = saturatedAdd(book_.forwardPages, 1);
    global_.forwardPages = saturatedAdd(global_.forwardPages, 1);
  }
  dirty_ = true;
}

uint32_t ReadingStats::currentSessionSeconds() const {
  if (!active_) return sessionSeconds_;
  const unsigned long elapsed = millis() - lastInteractionMs_;
  return elapsed <= MAX_ACTIVE_INTERVAL_MS ? saturatedAdd(sessionSeconds_, elapsed / 1000UL) : sessionSeconds_;
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
  book_.readingSeconds = saturatedAdd(book_.readingSeconds, sessionSeconds_);
  global_.readingSeconds = saturatedAdd(global_.readingSeconds, sessionSeconds_);
  book_.lastSessionSeconds = sessionSeconds_;
  global_.lastSessionSeconds = sessionSeconds_;
  active_ = false;
  dirty_ = true;
  persist();
}

uint32_t ReadingStats::finishSessionForSleep() {
  finishSession();
  return sessionSeconds_;
}
