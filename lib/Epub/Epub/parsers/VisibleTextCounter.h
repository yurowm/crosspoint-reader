#pragma once

#include <Print.h>
#include <expat.h>

#include <cstddef>
#include <cstdint>

class VisibleTextCounter final : public Print {
  XML_Parser parser = nullptr;
  size_t remainingSize = 0;
  uint32_t characterCount = 0;
  uint16_t nonVisibleDepth = 0;
  bool insideBody = false;
  bool hasVisibleText = false;
  bool pendingWhitespace = false;

  static void XMLCALL startElement(void* userData, const XML_Char* name, const XML_Char** atts);
  static void XMLCALL characterData(void* userData, const XML_Char* text, int length);
  static void XMLCALL endElement(void* userData, const XML_Char* name);
  static void XMLCALL defaultHandlerExpand(void* userData, const XML_Char* text, int length);

  void addCodepoint(uint32_t codepoint);

 public:
  VisibleTextCounter() = default;
  ~VisibleTextCounter() override;

  bool begin(size_t documentSize);
  uint32_t getCharacterCount() const { return characterCount; }

  size_t write(uint8_t data) override;
  size_t write(const uint8_t* buffer, size_t size) override;
};
