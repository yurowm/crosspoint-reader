#pragma once

#include <cstdint>
#include <string>

#include "LibraryIndex.h"

namespace NextBookFinder {

enum class Reason : uint8_t { NextInSeries, SameAuthor, Unread, None };

// Selects one unread book from the persistent library index. Priority is the
// next numbered book in the same series, then the same author, then any unread
// book. The index is streamed, so only the current book and best candidate are
// retained in memory.
bool findRecommendedBook(const std::string& currentBookPath, LibraryBook& recommendation, Reason& reason);

}  // namespace NextBookFinder
