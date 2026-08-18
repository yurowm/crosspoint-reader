#pragma once

#include <cstdint>

class GfxRenderer;

namespace BookPageEstimator {

// Estimates the current reader layout's capacity from its viewport, font
// metrics and line spacing. The result is reusable for every book in one view.
uint32_t charactersPerPage(const GfxRenderer& renderer);

uint32_t pageCount(uint32_t visibleCharacters, uint32_t charactersPerPage);

}  // namespace BookPageEstimator
