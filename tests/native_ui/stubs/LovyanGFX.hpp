#pragma once

#include <cstdint>
#include "Arduino.h"

namespace lgfx {
class LGFX_Sprite {
 public:
  void fillScreen(uint16_t) {}
  void fillRect(int, int, int, int, uint16_t) {}
  void drawFastHLine(int, int, int, uint16_t) {}
  void drawFastVLine(int, int, int, uint16_t) {}
  void drawRoundRect(int, int, int, int, int, uint16_t) {}
  void fillRoundRect(int, int, int, int, int, uint16_t) {}
  void drawCircle(int, int, int, uint16_t) {}
  void setTextSize(uint8_t) {}
  void setTextColor(uint16_t, uint16_t) {}
  void setCursor(int, int) {}
  void print(const String&) {}
  void print(const char*) {}
  void printf(const char*, ...) {}
};
}  // namespace lgfx
