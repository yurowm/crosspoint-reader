#include "BookPageEstimator.h"

#include <BoardConfig.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Utf8.h>

#include <algorithm>
#include <cstdint>

#include "CrossPointSettings.h"
#include "components/UITheme.h"

namespace BookPageEstimator {
namespace {
GfxRenderer::Orientation readerOrientation() {
  switch (SETTINGS.orientation) {
    case CrossPointSettings::ORIENTATION::LANDSCAPE_CW:
      return GfxRenderer::Orientation::LandscapeClockwise;
    case CrossPointSettings::ORIENTATION::INVERTED:
      return GfxRenderer::Orientation::PortraitInverted;
    case CrossPointSettings::ORIENTATION::LANDSCAPE_CCW:
      return GfxRenderer::Orientation::LandscapeCounterClockwise;
    case CrossPointSettings::ORIENTATION::PORTRAIT:
    default:
      return GfxRenderer::Orientation::Portrait;
  }
}

bool isLandscape(const GfxRenderer::Orientation orientation) {
  return orientation == GfxRenderer::Orientation::LandscapeClockwise ||
         orientation == GfxRenderer::Orientation::LandscapeCounterClockwise;
}

void orientedInsets(const GfxRenderer::Orientation orientation, int& top, int& right, int& bottom, int& left) {
  const auto& insets = BoardConfig::ACTIVE.viewableInsets;
  switch (orientation) {
    case GfxRenderer::Orientation::Portrait:
      top = insets.top;
      right = insets.right;
      bottom = insets.bottom;
      left = insets.left;
      break;
    case GfxRenderer::Orientation::LandscapeClockwise:
      top = insets.left;
      right = insets.top;
      bottom = insets.right;
      left = insets.bottom;
      break;
    case GfxRenderer::Orientation::PortraitInverted:
      top = insets.bottom;
      right = insets.left;
      bottom = insets.top;
      left = insets.right;
      break;
    case GfxRenderer::Orientation::LandscapeCounterClockwise:
      top = insets.right;
      right = insets.bottom;
      bottom = insets.left;
      left = insets.top;
      break;
  }
}

uint32_t codepointCount(const char* text) {
  if (!text) return 0;
  uint32_t count = 0;
  const auto* cursor = reinterpret_cast<const unsigned char*>(text);
  while (*cursor != '\0') {
    utf8NextCodepoint(&cursor);
    count++;
  }
  return count;
}
}  // namespace

uint32_t charactersPerPage(const GfxRenderer& renderer) {
  const auto orientation = readerOrientation();
  const bool currentLandscape = isLandscape(renderer.getOrientation());
  const int panelWidth = currentLandscape ? renderer.getScreenWidth() : renderer.getScreenHeight();
  const int panelHeight = currentLandscape ? renderer.getScreenHeight() : renderer.getScreenWidth();
  const int screenWidth = isLandscape(orientation) ? panelWidth : panelHeight;
  const int screenHeight = isLandscape(orientation) ? panelHeight : panelWidth;

  int top = 0;
  int right = 0;
  int bottom = 0;
  int left = 0;
  orientedInsets(orientation, top, right, bottom, left);
  top += SETTINGS.screenMargin;
  top += UITheme::getInstance().getTopStatusBarHeight();
  left += SETTINGS.screenMargin;
  right += SETTINGS.screenMargin;
  const int statusBarHeight = UITheme::getInstance().getStatusBarHeight();
  const int statusBarReservation =
      statusBarHeight > 0 ? statusBarHeight + UITheme::getInstance().getMetrics().readerStatusBarGap : 0;
  bottom += std::max<int>(SETTINGS.screenMargin, statusBarReservation);

  const int viewportWidth = screenWidth - left - right;
  const int viewportHeight = screenHeight - top - bottom;
  const int fontId = SETTINGS.getReaderFontId();
  const int lineAdvance = renderer.getLineHeight(fontId, SETTINGS.getReaderLineCompression());
  const char* sample = tr(STR_FONT_PREVIEW_TEXT);
  const uint32_t sampleCharacters = codepointCount(sample);
  const int sampleWidth = renderer.getTextWidth(fontId, sample);
  if (viewportWidth <= 0 || viewportHeight <= 0 || lineAdvance <= 0 || sampleCharacters == 0 || sampleWidth <= 0) {
    return 0;
  }

  const uint32_t charactersPerLine =
      std::max<uint32_t>(1, static_cast<uint32_t>((static_cast<uint64_t>(viewportWidth) * sampleCharacters) /
                                                  static_cast<uint32_t>(sampleWidth)));
  const uint32_t linesPerPage = std::max(1, viewportHeight / lineAdvance);
  return charactersPerLine * linesPerPage;
}

uint32_t pageCount(const uint32_t visibleCharacters, const uint32_t charactersPerPage) {
  if (visibleCharacters == 0 || charactersPerPage == 0) return 0;
  return static_cast<uint32_t>((static_cast<uint64_t>(visibleCharacters) + charactersPerPage - 1) / charactersPerPage);
}

}  // namespace BookPageEstimator
