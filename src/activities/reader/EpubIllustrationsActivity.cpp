#include "EpubIllustrationsActivity.h"

#include <Epub/converters/ImageDecoderFactory.h>
#include <Epub/converters/ImageToFramebufferDecoder.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>

#include "ReaderUtils.h"
#include "fontIds.h"

EpubIllustrationsActivity::EpubIllustrationsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                     std::shared_ptr<Epub> epub)
    : Activity("EpubIllustrations", renderer, mappedInput), epub(std::move(epub)) {}

void EpubIllustrationsActivity::onEnter() {
  Activity::onEnter();
  illustrations.clear();
  currentIndex = 0;
  if (!epub || !epub->listIllustrations(illustrations)) {
    LOG_ERR("ILL", "Failed to enumerate EPUB illustrations");
  }
  requestUpdate();
}

void EpubIllustrationsActivity::clearExtracted() {
  if (extractedPath.empty()) return;
  Storage.remove(extractedPath.c_str());
  extractedPath.clear();
}

void EpubIllustrationsActivity::onExit() {
  clearExtracted();
  illustrations.clear();
  epub.reset();
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

bool EpubIllustrationsActivity::drawCurrent() {
  if (!extractCurrent()) return false;

  ImageToFramebufferDecoder* decoder = ImageDecoderFactory::getDecoder(extractedPath);
  if (!decoder) return false;

  ImageDimensions dimensions{};
  if (!decoder->getDimensions(extractedPath, dimensions) || dimensions.width <= 0 || dimensions.height <= 0) {
    return false;
  }

  const float scale = std::min(static_cast<float>(renderer.getScreenWidth()) / dimensions.width,
                               static_cast<float>(renderer.getScreenHeight()) / dimensions.height);
  const int width = std::max(1, std::min(renderer.getScreenWidth(), static_cast<int>(dimensions.width * scale)));
  const int height = std::max(1, std::min(renderer.getScreenHeight(), static_cast<int>(dimensions.height * scale)));
  RenderConfig config;
  config.x = (renderer.getScreenWidth() - width) / 2;
  config.y = (renderer.getScreenHeight() - height) / 2;
  config.maxWidth = width;
  config.maxHeight = height;
  config.useGrayscale = true;
  config.useDithering = true;
  config.useExactDimensions = true;
  return decoder->decodeToFramebuffer(extractedPath, renderer, config);
}

void EpubIllustrationsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  if (illustrations.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, renderer.getScreenHeight() / 2, tr(STR_NO_ILLUSTRATIONS));
  } else if (!drawCurrent()) {
    renderer.clearScreen();
    renderer.drawCenteredText(UI_10_FONT_ID, renderer.getScreenHeight() / 2, tr(STR_FILE_OPEN_FAILED));
  }
  ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh);
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
