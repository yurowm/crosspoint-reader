#include "VisibleTextCounter.h"

#include <Logging.h>
#include <Utf8.h>
#include <XmlParserUtils.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <limits>
#include <string_view>

#include "Epub/VisibleTextUtils.h"
#include "Epub/htmlEntities.h"

namespace {
constexpr size_t PARSER_BUFFER_SIZE = 1024;

bool isWhitespace(const uint32_t codepoint) {
  return codepoint <= 0x20 || codepoint == 0x85 || codepoint == 0xA0 || codepoint == 0x1680 ||
         (codepoint >= 0x2000 && codepoint <= 0x200A) || codepoint == 0x2028 || codepoint == 0x2029 ||
         codepoint == 0x202F || codepoint == 0x205F || codepoint == 0x3000;
}

bool isTextBoundaryElement(const std::string_view name) {
  return VisibleTextUtils::equalsTag(name, "address") || VisibleTextUtils::equalsTag(name, "article") ||
         VisibleTextUtils::equalsTag(name, "aside") || VisibleTextUtils::equalsTag(name, "blockquote") ||
         VisibleTextUtils::equalsTag(name, "br") || VisibleTextUtils::equalsTag(name, "div") ||
         VisibleTextUtils::equalsTag(name, "dl") || VisibleTextUtils::equalsTag(name, "figcaption") ||
         VisibleTextUtils::equalsTag(name, "figure") || VisibleTextUtils::equalsTag(name, "footer") ||
         VisibleTextUtils::equalsTag(name, "h1") || VisibleTextUtils::equalsTag(name, "h2") ||
         VisibleTextUtils::equalsTag(name, "h3") || VisibleTextUtils::equalsTag(name, "h4") ||
         VisibleTextUtils::equalsTag(name, "h5") || VisibleTextUtils::equalsTag(name, "h6") ||
         VisibleTextUtils::equalsTag(name, "header") || VisibleTextUtils::equalsTag(name, "hr") ||
         VisibleTextUtils::equalsTag(name, "li") || VisibleTextUtils::equalsTag(name, "main") ||
         VisibleTextUtils::equalsTag(name, "nav") || VisibleTextUtils::equalsTag(name, "ol") ||
         VisibleTextUtils::equalsTag(name, "p") || VisibleTextUtils::equalsTag(name, "pre") ||
         VisibleTextUtils::equalsTag(name, "section") || VisibleTextUtils::equalsTag(name, "table") ||
         VisibleTextUtils::equalsTag(name, "tr") || VisibleTextUtils::equalsTag(name, "ul");
}

bool startsWithIgnoreCase(const char* value, const char* expected) {
  while (*expected != '\0') {
    if (*value == '\0') return false;
    if (std::tolower(static_cast<unsigned char>(*value)) != *expected) return false;
    value++;
    expected++;
  }
  return true;
}

bool hasDisplayNone(const XML_Char** atts) {
  if (!atts) return false;
  for (int i = 0; atts[i] != nullptr; i += 2) {
    if (!VisibleTextUtils::equalsTag(xmlLocalName(atts[i]), "style")) continue;
    const char* style = atts[i + 1];
    if (!style) return false;
    for (const char* cursor = style; *cursor != '\0'; cursor++) {
      if (!startsWithIgnoreCase(cursor, "display")) continue;
      const char* value = cursor + sizeof("display") - 1;
      while (std::isspace(static_cast<unsigned char>(*value))) value++;
      if (*value != ':') continue;
      value++;
      while (std::isspace(static_cast<unsigned char>(*value))) value++;
      if (startsWithIgnoreCase(value, "none")) return true;
    }
  }
  return false;
}
}  // namespace

VisibleTextCounter::~VisibleTextCounter() { destroyXmlParser(parser); }

bool VisibleTextCounter::begin(const size_t documentSize) {
  if (parser) {
    if (XML_ParserReset(parser, nullptr) == XML_FALSE) {
      LOG_ERR("VTC", "Could not reset XML parser");
      destroyXmlParser(parser);
      return false;
    }
  } else {
    parser = XML_ParserCreate(nullptr);
    if (!parser) {
      LOG_ERR("VTC", "Could not allocate XML parser");
      return false;
    }
  }

  remainingSize = documentSize;
  characterCount = 0;
  nonVisibleDepth = 0;
  insideBody = false;
  hasVisibleText = false;
  pendingWhitespace = false;

  XML_SetUserData(parser, this);
  XML_SetElementHandler(parser, startElement, endElement);
  XML_SetCharacterDataHandler(parser, characterData);
  XML_SetDefaultHandlerExpand(parser, defaultHandlerExpand);
  return true;
}

size_t VisibleTextCounter::write(const uint8_t data) { return write(&data, 1); }

size_t VisibleTextCounter::write(const uint8_t* buffer, const size_t size) {
  if (!parser || size > remainingSize) return 0;

  const uint8_t* cursor = buffer;
  size_t unparsed = size;
  while (unparsed > 0) {
    void* const parserBuffer = XML_GetBuffer(parser, PARSER_BUFFER_SIZE);
    if (!parserBuffer) {
      LOG_ERR("VTC", "Could not allocate XML parser buffer");
      return 0;
    }

    const size_t chunk = std::min(unparsed, PARSER_BUFFER_SIZE);
    memcpy(parserBuffer, cursor, chunk);
    const bool isFinal = remainingSize == chunk;
    if (XML_ParseBuffer(parser, static_cast<int>(chunk), isFinal) == XML_STATUS_ERROR) {
      LOG_DBG("VTC", "Parse error at line %lu: %s", XML_GetCurrentLineNumber(parser),
              XML_ErrorString(XML_GetErrorCode(parser)));
      return 0;
    }

    cursor += chunk;
    unparsed -= chunk;
    remainingSize -= chunk;
  }
  return size;
}

void VisibleTextCounter::addCodepoint(const uint32_t codepoint) {
  if (isWhitespace(codepoint)) {
    if (hasVisibleText) pendingWhitespace = true;
    return;
  }

  if (pendingWhitespace && characterCount < std::numeric_limits<uint32_t>::max()) {
    characterCount++;
  }
  if (characterCount < std::numeric_limits<uint32_t>::max()) {
    characterCount++;
  }
  hasVisibleText = true;
  pendingWhitespace = false;
}

void XMLCALL VisibleTextCounter::startElement(void* userData, const XML_Char* name, const XML_Char** atts) {
  auto* self = static_cast<VisibleTextCounter*>(userData);
  const std::string_view localName(xmlLocalName(name));

  if (VisibleTextUtils::equalsTag(localName, "body")) {
    self->insideBody = true;
  }
  if (!self->insideBody) return;

  if (self->nonVisibleDepth > 0 || VisibleTextUtils::isNonVisibleElement(localName) || hasDisplayNone(atts)) {
    self->nonVisibleDepth++;
    return;
  }
  if (isTextBoundaryElement(localName) && self->hasVisibleText) {
    self->pendingWhitespace = true;
  }
}

void XMLCALL VisibleTextCounter::characterData(void* userData, const XML_Char* text, const int length) {
  auto* self = static_cast<VisibleTextCounter*>(userData);
  if (!self->insideBody || self->nonVisibleDepth > 0 || length <= 0) return;

  const auto* cursor = reinterpret_cast<const unsigned char*>(text);
  const auto* const end = cursor + length;
  while (cursor < end) {
    self->addCodepoint(utf8NextCodepoint(&cursor));
  }
}

void XMLCALL VisibleTextCounter::endElement(void* userData, const XML_Char* name) {
  auto* self = static_cast<VisibleTextCounter*>(userData);
  const std::string_view localName(xmlLocalName(name));

  if (self->insideBody && self->nonVisibleDepth == 0 && isTextBoundaryElement(localName) && self->hasVisibleText) {
    self->pendingWhitespace = true;
  }
  if (self->nonVisibleDepth > 0) {
    self->nonVisibleDepth--;
  }
  if (VisibleTextUtils::equalsTag(localName, "body")) {
    self->insideBody = false;
  }
}

void XMLCALL VisibleTextCounter::defaultHandlerExpand(void* userData, const XML_Char* text, const int length) {
  if (length < 3 || text[0] != '&' || text[length - 1] != ';') return;

  auto* self = static_cast<VisibleTextCounter*>(userData);
  if (!self->insideBody || self->nonVisibleDepth > 0) return;
  const char* value = lookupHtmlEntity(text, static_cast<size_t>(length));
  if (value) {
    characterData(userData, value, static_cast<int>(strlen(value)));
  } else {
    self->addCodepoint(REPLACEMENT_GLYPH);
  }
}
