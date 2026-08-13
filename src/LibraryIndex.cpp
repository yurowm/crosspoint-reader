#include "LibraryIndex.h"

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace {
constexpr std::array<uint8_t, 5> HEADER = {'L', 'I', 'D', 'X', 1};
constexpr const char* TMP_FILE_PATH = "/.crosspoint/library.idx.tmp";
constexpr uint32_t MAX_BOOKS = 4096;
constexpr uint32_t MAX_STRING_BYTES = 16 * 1024;
constexpr uint16_t MAX_LIST_ITEMS = 512;
constexpr uint64_t MAX_INDEX_BYTES = 4 * 1024 * 1024;

bool readExact(HalFile& file, void* data, const size_t size) {
  return size == 0 || file.read(data, size) == static_cast<int>(size);
}

bool writeExact(HalFile& file, const void* data, const size_t size) {
  return size == 0 || file.write(data, size) == size;
}

template <typename T>
bool readPod(HalFile& file, T& value) {
  return readExact(file, &value, sizeof(value));
}

template <typename T>
bool writePod(HalFile& file, const T& value) {
  return writeExact(file, &value, sizeof(value));
}

bool readString(HalFile& file, std::string& value) {
  uint32_t length = 0;
  if (!readPod(file, length) || length > MAX_STRING_BYTES) return false;
  value.resize(length);
  return length == 0 || readExact(file, value.data(), length);
}

bool writeString(HalFile& file, const std::string& value) {
  if (value.size() > MAX_STRING_BYTES || value.size() > std::numeric_limits<uint32_t>::max()) return false;
  const uint32_t length = static_cast<uint32_t>(value.size());
  return writePod(file, length) && writeExact(file, value.data(), length);
}

bool readStringList(HalFile& file, std::vector<std::string>& values) {
  uint16_t count = 0;
  if (!readPod(file, count) || count > MAX_LIST_ITEMS) return false;
  values.clear();
  values.reserve(count);
  for (uint16_t i = 0; i < count; i++) {
    std::string value;
    if (!readString(file, value)) return false;
    values.push_back(std::move(value));
  }
  return true;
}

bool writeStringList(HalFile& file, const std::vector<std::string>& values) {
  if (values.size() > MAX_LIST_ITEMS || values.size() > std::numeric_limits<uint16_t>::max()) return false;
  const uint16_t count = static_cast<uint16_t>(values.size());
  if (!writePod(file, count)) return false;
  return std::all_of(values.begin(), values.end(),
                     [&file](const std::string& value) { return writeString(file, value); });
}

bool readBook(HalFile& file, LibraryBook& book) {
  uint8_t started = 0;
  const bool ok = readPod(file, book.fileSize) && readPod(file, book.modifiedDate) &&
                  readPod(file, book.modifiedTime) && readString(file, book.path) && readString(file, book.title) &&
                  readString(file, book.author) && readStringList(file, book.authors) &&
                  readString(file, book.series) && readString(file, book.seriesIndex) &&
                  readStringList(file, book.tags) && readString(file, book.coverBmpPath) &&
                  readPod(file, book.progressPercent) && readPod(file, started);
  if (!ok) return false;
  book.started = started != 0;
  return !book.path.empty() && book.path.front() == '/';
}

bool writeBook(HalFile& file, const LibraryBook& book) {
  const uint8_t started = book.started ? 1 : 0;
  return writePod(file, book.fileSize) && writePod(file, book.modifiedDate) && writePod(file, book.modifiedTime) &&
         writeString(file, book.path) && writeString(file, book.title) && writeString(file, book.author) &&
         writeStringList(file, book.authors) && writeString(file, book.series) && writeString(file, book.seriesIndex) &&
         writeStringList(file, book.tags) && writeString(file, book.coverBmpPath) &&
         writePod(file, book.progressPercent) && writePod(file, started);
}
}  // namespace

bool LibraryIndex::load(std::vector<LibraryBook>& books) {
  books.clear();
  if (!Storage.exists(FILE_PATH)) return false;

  HalFile file;
  if (!Storage.openFileForRead("LIDX", FILE_PATH, file)) return false;
  if (file.fileSize64() > MAX_INDEX_BYTES) {
    LOG_ERR("LIDX", "Library index is too large");
    file.close();
    return false;
  }

  std::array<uint8_t, HEADER.size()> header{};
  uint32_t count = 0;
  if (!readExact(file, header.data(), header.size()) || header != HEADER || !readPod(file, count) ||
      count > MAX_BOOKS) {
    LOG_ERR("LIDX", "Invalid library index header");
    file.close();
    return false;
  }

  books.reserve(count);
  for (uint32_t i = 0; i < count; i++) {
    LibraryBook book;
    if (!readBook(file, book)) {
      LOG_ERR("LIDX", "Truncated library index at entry %u", static_cast<unsigned>(i));
      books.clear();
      file.close();
      return false;
    }
    book.progressPercent = std::min<uint8_t>(book.progressPercent, 100);
    books.push_back(std::move(book));
  }
  file.close();
  LOG_DBG("LIDX", "Loaded %u library entries", static_cast<unsigned>(books.size()));
  return true;
}

bool LibraryIndex::save(const std::vector<LibraryBook>& books) {
  if (books.size() > MAX_BOOKS) {
    LOG_ERR("LIDX", "Too many books for library index: %u", static_cast<unsigned>(books.size()));
    return false;
  }

  Storage.mkdir("/.crosspoint");
  Storage.remove(TMP_FILE_PATH);
  HalFile file;
  if (!Storage.openFileForWrite("LIDX", TMP_FILE_PATH, file)) return false;

  const uint32_t count = static_cast<uint32_t>(books.size());
  bool ok = writeExact(file, HEADER.data(), HEADER.size()) && writePod(file, count);
  if (ok) {
    ok = std::all_of(books.begin(), books.end(), [&file](const LibraryBook& book) { return writeBook(file, book); });
  }
  if (ok) file.flush();
  file.close();

  if (!ok) {
    LOG_ERR("LIDX", "Failed writing library index");
    Storage.remove(TMP_FILE_PATH);
    return false;
  }

  Storage.remove(FILE_PATH);
  if (!Storage.rename(TMP_FILE_PATH, FILE_PATH)) {
    LOG_ERR("LIDX", "Failed moving library index into place");
    Storage.remove(TMP_FILE_PATH);
    return false;
  }
  LOG_DBG("LIDX", "Saved %u library entries", static_cast<unsigned>(books.size()));
  return true;
}

bool LibraryIndex::updateProgress(const std::string& path, const uint8_t progressPercent) {
  std::vector<LibraryBook> books;
  if (!load(books)) return true;
  auto it = std::find_if(books.begin(), books.end(), [&path](const LibraryBook& book) { return book.path == path; });
  if (it == books.end()) return true;

  const uint8_t clamped = std::min<uint8_t>(progressPercent, 100);
  if (it->started && it->progressPercent == clamped) return true;
  it->started = true;
  it->progressPercent = clamped;
  return save(books);
}

bool LibraryIndex::invalidate(const std::string& path) {
  std::vector<LibraryBook> books;
  if (!load(books)) return true;
  const auto end =
      std::remove_if(books.begin(), books.end(), [&path](const LibraryBook& book) { return book.path == path; });
  if (end == books.end()) return true;
  books.erase(end, books.end());
  return save(books);
}

bool LibraryIndex::sourceMatches(const LibraryBook& book, const LibraryFileInfo& file) {
  return book.path == file.path && book.fileSize == file.fileSize && book.modifiedDate == file.modifiedDate &&
         book.modifiedTime == file.modifiedTime;
}
