#include "EpubIllustrationsActivity.h"

#include <Bitmap.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <JpegToBmpConverter.h>
#include <Logging.h>
#include <Memory.h>
#include <PngToBmpConverter.h>

#include <algorithm>

#include "ReaderUtils.h"
#include "fontIds.h"

EpubIllustrationsActivity::EpubIllustrationsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                     std::shared_ptr<Epub> epub)
    : Activity("EpubIllustrations", renderer, mappedInput), sharedEpub(std::move(epub)), epub(sharedEpub.get()) {}

EpubIllustrationsActivity::EpubIllustrationsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                     const std::string& bookPath)
    : Activity("EpubIllustrations", renderer, mappedInput), bookPath(bookPath) {}

void EpubIllustrationsActivity::onEnter() {
  Activity::onEnter();
  if (!epub && !bookPath.empty()) {
    ownedEpub = makeUniqueNoThrow<Epub>(bookPath, "/.crosspoint");
    if (!ownedEpub || !ownedEpub->loadMetadata()) {
      LOG_ERR("ILL", "Failed to load EPUB metadata");
      ownedEpub.reset();
    }
    epub = ownedEpub.get();
  }
  illustrations.clear();
  currentIndex = 0;
  if (!epub || !epub->listIllustrations(illustrations)) {
    LOG_ERR("ILL", "Failed to enumerate EPUB illustrations");
  }
  requestUpdate();
}

void EpubIllustrationsActivity::clearExtracted() {
  if (!extractedPath.empty()) {
    Storage.remove(extractedPath.c_str());
    extractedPath.clear();
  }
  if (!convertedPath.empty()) {
    Storage.remove(convertedPath.c_str());
    convertedPath.clear();
  }
}

void EpubIllustrationsActivity::onExit() {
  clearExtracted();
  illustrations.clear();
  epub = nullptr;
  sharedEpub.reset();
  ownedEpub.reset();
  Activity::onExit();
}

bool EpubIllustrationsActivity::extractCurrent() {
  clearExtracted();
  if (!epub || currentIndex < 0 || currentIndex >= static_cast<int>(illustrations.size())) return false;

  const auto& itemPath = illustrations[static_cast<size_t>(currentIndex)];
  const char* extension = FsHelpers::hasPngExtension(itemPath) ? ".png" : ".jpg";
  extractedPath = epub->getCachePath() + "/.illustration" + extension;
  if (epub->extractItemToFile(itemPath, extractedPath)) return true;

  extractedPath.clear();
  return false;
}

bool EpubIllustrationsActivity::convertCurrent() {
  if (extractedPath.empty()) return false;

  convertedPath = epub->getCachePath() + "/.illustration.bmp";
  HalFile source;
  HalFile output;
  if (!Storage.openFileForRead("ILL", extractedPath, source) ||
      !Storage.openFileForWrite("ILL", convertedPath, output)) {
    if (source.isOpen()) source.close();
    if (output.isOpen()) output.close();
    Storage.remove(convertedPath.c_str());
    convertedPath.clear();
    return false;
  }

  // Use the same area-aware scaling and Atkinson error-diffusion pipeline as
  // full-screen book covers. The resulting BMP still contains the panel's four
  // native levels, but error diffusion preserves the apparent tonal range.
  const bool success = FsHelpers::hasPngExtension(extractedPath)
                           ? PngToBmpConverter::pngFileToBmpStream(source, output, false)
                           : JpegToBmpConverter::jpegFileToBmpStream(source, output, false);
  output.close();
  source.close();
  if (success) return true;

  Storage.remove(convertedPath.c_str());
  convertedPath.clear();
  return false;
}

bool EpubIllustrationsActivity::drawCurrent() {
  if (!extractCurrent() || !convertCurrent()) return false;

  HalFile file;
  if (!Storage.openFileForRead("ILL", convertedPath, file)) return false;
  Bitmap bitmap(file, false);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) {
    file.close();
    return false;
  }

  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  const int x = (screenWidth - bitmap.getWidth()) / 2;
  const int y = (screenHeight - bitmap.getHeight()) / 2;

  renderer.drawBitmap(bitmap, x, y, screenWidth, screenHeight);
  renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);

  bitmap.rewindToData();
  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
  renderer.drawBitmap(bitmap, x, y, screenWidth, screenHeight);
  renderer.copyGrayscaleLsbBuffers();

  bitmap.rewindToData();
  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
  renderer.drawBitmap(bitmap, x, y, screenWidth, screenHeight);
  renderer.copyGrayscaleMsbBuffers();
  renderer.displayGrayBuffer();
  renderer.setRenderMode(GfxRenderer::BW);
  file.close();
  return true;
}

void EpubIllustrationsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  bool displayed = false;
  if (illustrations.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, renderer.getScreenHeight() / 2, tr(STR_NO_ILLUSTRATIONS));
  } else {
    displayed = drawCurrent();
    if (!displayed) {
      renderer.clearScreen();
      renderer.drawCenteredText(UI_10_FONT_ID, renderer.getScreenHeight() / 2, tr(STR_FILE_OPEN_FAILED));
    }
  }
  if (!displayed) ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh);
}

void EpubIllustrationsActivity::turn(const int direction) {
  const int next = currentIndex + direction;
  if (next < 0 || next >= static_cast<int>(illustrations.size())) return;
  currentIndex = next;
  requestUpdate();
}

void EpubIllustrationsActivity::loop() {
  Activity::loop();

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  const auto touch = ReaderUtils::detectTouchPageTurn(renderer, mappedInput);
  const auto buttons = ReaderUtils::detectPageTurn(mappedInput);
  if (touch.prev || buttons.prev) {
    turn(-1);
  } else if (touch.next || buttons.next) {
    turn(1);
  }
}
