#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <string>
#include <vector>

#include "Arduino.h"

namespace lgfx { struct IFont {}; }

namespace fonts {
struct TestFont : lgfx::IFont {};
inline const TestFont efontJA_12{};
inline const TestFont efontJA_12_b{};
inline const TestFont efontJA_12_i{};
inline const TestFont efontJA_12_bi{};
}  // namespace fonts

namespace lgfx {
struct TextCall {
  std::string text;
  int x = 0, y = 0, width = 0;
  float size = 1.0f;
  uint16_t foreground = 0, background = 0;
};

class LGFX_Sprite {
 public:
  void fillScreen(uint16_t) { calls.clear(); }
  void fillRect(int, int, int, int, uint16_t) {}
  void drawFastHLine(int, int, int, uint16_t) {}
  void drawFastVLine(int, int, int, uint16_t) {}
  void drawRoundRect(int, int, int, int, int, uint16_t) {}
  void fillRoundRect(int, int, int, int, int, uint16_t) {}
  void drawCircle(int, int, int, uint16_t) {}
  void setFont(const IFont* font) { selectedFont = font; }
  void setTextWrap(bool x, bool y = false) { wrapX = x; wrapY = y; }
  void setTextSize(float size) { textSize = size; }
  void setTextColor(uint16_t foreground, uint16_t background) {
    fg = foreground; bg = background;
  }
  void setCursor(int x, int y) { cursorX = x; cursorY = y; }
  int textWidth(const String& value) const {
    int width = 0;
    for (uint16_t i = 0; i < value.length();) {
      const uint8_t lead = static_cast<uint8_t>(value[i]);
      if ((lead & 0xc0) != 0x80) width += lead < 0x80 ? (lead == 'i' || lead == ' ' || lead == '.' ? 4 : 7) : 12;
      ++i;
    }
    return static_cast<int>(width * textSize);
  }
  int fontHeight() const { return static_cast<int>(12 * textSize); }
  void print(const String& value) { record(value); }
  void print(const char* value) { record(String(value)); }
  void printf(const char* format, ...) {
    char buffer[256];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    record(String(buffer));
  }

  const IFont* selectedFont = nullptr;
  bool wrapX = true, wrapY = false;
  float textSize = 1.0f;
  std::vector<TextCall> calls;

 private:
  int cursorX = 0, cursorY = 0;
  uint16_t fg = 0, bg = 0;
  void record(const String& value) {
    calls.push_back({value.str(), cursorX, cursorY, textWidth(value), textSize, fg, bg});
  }
};
}  // namespace lgfx
