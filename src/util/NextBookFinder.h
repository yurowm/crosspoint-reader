#pragma once

#include <string>

#include "LibraryIndex.h"

namespace NextBookFinder {

// Selects one unread book from the persistent library index. Priority is the
// next numbered book in the same series, then the same author, then any unread
// book. The index is streamed, so only the current book and best candidate are
// retained in memory.
bool findRecommendedBook(const std::string& currentBookPath, LibraryBook& recommendation);

}  // namespace NextBookFinder
