#pragma once

#include <stdint.h>

namespace ui {

constexpr uint8_t kNerdIconLineHeight = 12;
constexpr uint8_t kNerdIconBaseline = 9;

// A compact monochrome Nerd Font icon. alpha4 uses packed row-major pixels:
// each row has (width + 1) / 2 bytes, and the even x pixel is in the low
// nibble. Coverage 0 is transparent and 15 is fully covered. xOffset/yOffset
// position the bitmap from the line's top-left text cursor; yOffset keeps the
// complete bitmap inside the 12 px line box.
struct NerdIconBitmap {
  const uint8_t* alpha4 = nullptr;
  uint8_t width = 0;
  uint8_t height = 0;
  uint8_t advance = 0;
  int8_t xOffset = 0;
  int8_t yOffset = 0;
};

// Finds a Nerd Font PUA glyph in U+E000..U+F8FF or U+F0000..U+FFFFD.
// The returned mask points into read-only generated data and remains valid.
bool findNerdIcon(uint32_t codepoint, NerdIconBitmap& out);

}  // namespace ui
