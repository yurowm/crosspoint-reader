#include "BookCoverActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Txt.h>
#include <Xtc.h>

#include <cmath>

#include "CrossPointSettings.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
struct BitmapPlacement {
  int x = 0;
  int y = 0;
  float cropX = 0.0f;
  float cropY = 0.0f;
};

BitmapPlacement calculatePlacement(const int bitmapWidth, const int bitmapHeight, const GfxRenderer& renderer) {
  BitmapPlacement placement;
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  if (bitmapWidth > pageWidth || bitmapHeight > pageHeight) {
    float ratio = static_cast<float>(bitmapWidth) / bitmapHeight;
    const float screenRatio = static_cast<float>(pageWidth) / pageHeight;
    if (ratio > screenRatio) {
      if (SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP) {
        placement.cropX = 1.0f - screenRatio / ratio;
        ratio = (1.0f - placement.cropX) * bitmapWidth / bitmapHeight;
      }
      placement.y = std::round((pageHeight - pageWidth / ratio) / 2.0f);
    } else {
      if (SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP) {
        placement.cropY = 1.0f - ratio / screenRatio;
        ratio = bitmapWidth / ((1.0f - placement.cropY) * bitmapHeight);
      }
      placement.x = std::round((pageWidth - pageHeight * ratio) / 2.0f);
    }
  } else {
    placement.x = (pageWidth - bitmapWidth) / 2;
    placement.y = (pageHeight - bitmapHeight) / 2;
  }
  return placement;
}
}  // namespace

bool BookCoverActivity::prepareCover() {
  if (FsHelpers::hasEpubExtension(bookPath)) {
    Epub epub(bookPath, "/.crosspoint");
    const bool cropped = SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP;
    if (!epub.loadMetadata() || !epub.generateCoverBmp(cropped)) return false;
    coverPath = epub.getCoverBmpPath(cropped);
    return true;
  }

  if (FsHelpers::hasXtcExtension(bookPath)) {
    Xtc xtc(bookPath, "/.crosspoint");
    if (!xtc.load() || !xtc.generateCoverBmp()) return false;
    coverPath = xtc.getCoverBmpPath();
    return true;
  }

  if (FsHelpers::hasTxtExtension(bookPath) || FsHelpers::hasMarkdownExtension(bookPath)) {
    Txt txt(bookPath, "/.crosspoint");
    if (!txt.load() || !txt.generateCoverBmp()) return false;
    coverPath = txt.getCoverBmpPath();
    return true;
  }

  return false;
}

void BookCoverActivity::onEnter() {
  Activity::onEnter();
  coverReady = false;
  requestUpdateAndWait();
  coverReady = prepareCover();
  if (!coverReady) {
    coverPath = "failed";
    LOG_ERR("BCA", "Cover unavailable for %s", bookPath.c_str());
  }
  requestUpdate();
}

void BookCoverActivity::loop() {
  int x = 0;
  int y = 0;
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
      mappedInput.wasReleased(MappedInputManager::Button::Confirm) || mappedInput.wasScreenTapped(x, y)) {
    finish();
  }
}

void BookCoverActivity::render(RenderLock&&) {
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  if (!coverReady) {
    renderer.clearScreen();
    UITheme::drawCenteredText(renderer, Rect{0, 0, pageWidth, pageHeight}, UI_12_FONT_ID, pageHeight / 2,
                              coverPath.empty() ? tr(STR_LOADING_POPUP) : tr(STR_BOOK_COVER_UNAVAILABLE));
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    return;
  }

  HalFile file;
  if (!Storage.openFileForRead("BCA", coverPath, file)) {
    renderer.clearScreen();
    UITheme::drawCenteredText(renderer, Rect{0, 0, pageWidth, pageHeight}, UI_12_FONT_ID, pageHeight / 2,
                              tr(STR_BOOK_COVER_UNAVAILABLE));
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    return;
  }
  Bitmap bitmap(file);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) {
    renderer.clearScreen();
    UITheme::drawCenteredText(renderer, Rect{0, 0, pageWidth, pageHeight}, UI_12_FONT_ID, pageHeight / 2,
                              tr(STR_BOOK_COVER_UNAVAILABLE));
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    return;
  }

  const auto placement = calculatePlacement(bitmap.getWidth(), bitmap.getHeight(), renderer);
  renderer.clearScreen();
  renderer.drawBitmap(bitmap, placement.x, placement.y, pageWidth, pageHeight, placement.cropX, placement.cropY);
  if (SETTINGS.sleepScreenCoverFilter ==
      CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::INVERTED_BLACK_AND_WHITE) {
    renderer.invertScreen();
  }

  const bool hasGreyscale =
      bitmap.hasGreyscale() &&
      SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER;
  if (!hasGreyscale) {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    return;
  }

  renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);
  bitmap.rewindToData();
  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
  renderer.drawBitmap(bitmap, placement.x, placement.y, pageWidth, pageHeight, placement.cropX, placement.cropY);
  renderer.copyGrayscaleLsbBuffers();

  bitmap.rewindToData();
  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
  renderer.drawBitmap(bitmap, placement.x, placement.y, pageWidth, pageHeight, placement.cropX, placement.cropY);
  renderer.copyGrayscaleMsbBuffers();
  renderer.displayGrayBuffer();
  renderer.setRenderMode(GfxRenderer::BW);
}
