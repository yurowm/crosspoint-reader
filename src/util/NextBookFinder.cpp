#include "NextBookFinder.h"

#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdlib>

namespace {
struct FindCurrentContext {
  const std::string* path;
  LibraryBook* current;
  bool found = false;
};

struct RecommendationContext {
  const std::string* currentPath;
  const LibraryBook* current;
  LibraryBook* best;
  NextBookFinder::Reason bestTier = NextBookFinder::Reason::None;
  bool currentSeriesIndexNumeric = false;
  double currentSeriesIndex = 0.0;
  bool bestSeriesIndexNumeric = false;
  double bestSeriesIndex = 0.0;
};

bool parseSeriesIndex(const std::string& value, double& result) {
  if (value.empty()) return false;
  char* end = nullptr;
  result = std::strtod(value.c_str(), &end);
  return end != value.c_str() && *end == '\0';
}

bool sameAuthor(const LibraryBook& left, const LibraryBook& right) {
  const auto matches = [](const std::string& value, const LibraryBook& book) {
    if (value.empty()) return false;
    if (book.author == value) return true;
    return std::find(book.authors.begin(), book.authors.end(), value) != book.authors.end();
  };

  if (matches(left.author, right)) return true;
  return std::any_of(left.authors.begin(), left.authors.end(),
                     [&right, &matches](const std::string& author) { return matches(author, right); });
}

bool bookOrderLess(const LibraryBook& left, const LibraryBook& right) {
  if (left.title == right.title) return FsHelpers::naturalLess(left.path, right.path);
  return FsHelpers::naturalLess(left.title, right.title);
}

bool seriesOrderLess(const LibraryBook& candidate, const bool candidateNumeric, const double candidateIndex,
                     const LibraryBook& best, const bool bestNumeric, const double bestIndex) {
  if (candidateNumeric != bestNumeric) return candidateNumeric;
  if (candidateNumeric && candidateIndex != bestIndex) return candidateIndex < bestIndex;
  if (candidate.seriesIndex != best.seriesIndex) {
    if (candidate.seriesIndex.empty()) return false;
    if (best.seriesIndex.empty()) return true;
    return FsHelpers::naturalLess(candidate.seriesIndex, best.seriesIndex);
  }
  return bookOrderLess(candidate, best);
}

bool findCurrent(const LibraryBook& book, void* rawContext) {
  auto* context = static_cast<FindCurrentContext*>(rawContext);
  if (book.path != *context->path) return true;
  *context->current = book;
  context->found = true;
  return false;
}

bool considerBook(const LibraryBook& book, void* rawContext) {
  auto* context = static_cast<RecommendationContext*>(rawContext);
  if (book.path == *context->currentPath || book.progressPercent >= 100 || !Storage.exists(book.path.c_str())) {
    return true;
  }

  NextBookFinder::Reason tier = NextBookFinder::Reason::Unread;
  bool candidateSeriesIndexNumeric = false;
  double candidateSeriesIndex = 0.0;
  const bool sameSeries = !context->current->series.empty() && book.series == context->current->series;
  if (sameSeries) {
    candidateSeriesIndexNumeric = parseSeriesIndex(book.seriesIndex, candidateSeriesIndex);
    const bool isNext = context->currentSeriesIndexNumeric
                          ? candidateSeriesIndexNumeric && candidateSeriesIndex > context->currentSeriesIndex
                          : book.seriesIndex != context->current->seriesIndex;
    if (isNext) tier = NextBookFinder::Reason::NextInSeries;
  }
  if (tier != NextBookFinder::Reason::NextInSeries && sameAuthor(*context->current, book)) {
    tier = NextBookFinder::Reason::SameAuthor;
  }

  bool replace = tier < context->bestTier;
  if (tier == context->bestTier) {
    replace = tier == NextBookFinder::Reason::NextInSeries
                ? seriesOrderLess(book, candidateSeriesIndexNumeric, candidateSeriesIndex, *context->best,
                                  context->bestSeriesIndexNumeric, context->bestSeriesIndex)
                : bookOrderLess(book, *context->best);
  }
  if (!replace) return true;

  *context->best = book;
  context->bestTier = tier;
  context->bestSeriesIndexNumeric = candidateSeriesIndexNumeric;
  context->bestSeriesIndex = candidateSeriesIndex;
  return true;
}
}  // namespace

bool NextBookFinder::findRecommendedBook(const std::string& currentBookPath, LibraryBook& recommendation,
                                         Reason& reason) {
  reason = Reason::None;
  if (currentBookPath.empty()) return false;

  auto current = makeUniqueNoThrow<LibraryBook>();
  if (!current) {
    LOG_ERR("NBF", "OOM: current LibraryBook");
    return false;
  }

  FindCurrentContext findContext{&currentBookPath, current.get()};
  if (!LibraryIndex::visitBooks(&findCurrent, &findContext)) return false;
  if (!findContext.found) {
    LOG_DBG("NBF", "Current book is missing from library index: %s", currentBookPath.c_str());
    return false;
  }

  RecommendationContext recommendationContext{&currentBookPath, current.get(), &recommendation};
  recommendationContext.currentSeriesIndexNumeric =
      parseSeriesIndex(current->seriesIndex, recommendationContext.currentSeriesIndex);
  if (!LibraryIndex::visitBooks(&considerBook, &recommendationContext)) return false;

  if (recommendationContext.bestTier == Reason::None) return false;
  reason = recommendationContext.bestTier;
  LOG_DBG("NBF", "Recommended %s (tier %u)", recommendation.path.c_str(),
          static_cast<unsigned>(recommendationContext.bestTier));
  return true;
}
