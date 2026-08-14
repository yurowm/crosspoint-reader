#pragma once

#include <string>

#include "LibraryIndex.h"

class GfxRenderer;

namespace BookListItem {

static constexpr int ITEMS_PER_PAGE = 4;
static constexpr int ROW_GAP = 3;
static constexpr int DEFAULT_COVER_CACHE_HEIGHT = 140;

int rowHeight(int contentHeight);
int drawCover(GfxRenderer& renderer, const LibraryBook& book, int x, int y, int maxWidth, int height);
int draw(GfxRenderer& renderer, const LibraryBook& book, int x, int y, int width, int height, bool selected);

}  // namespace BookListItem
