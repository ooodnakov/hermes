#include "nerd_icons.h"

#include "nerd_icons_data.h"

namespace ui {

bool findNerdIcon(uint32_t codepoint, NerdIconBitmap& out) {
  uint32_t first = 0;
  uint32_t last = nerd_icons_data::kGlyphCount;
  while (first < last) {
    const uint32_t middle = first + (last - first) / 2;
    const auto& entry = nerd_icons_data::kGlyphs[middle];
    if (entry.codepoint < codepoint) {
      first = middle + 1;
    } else {
      last = middle;
    }
  }
  if (first == nerd_icons_data::kGlyphCount ||
      nerd_icons_data::kGlyphs[first].codepoint != codepoint) {
    out = {};
    return false;
  }

  const auto& entry = nerd_icons_data::kGlyphs[first];
  out.alpha4 = nerd_icons_data::kAlpha4 + entry.offset;
  out.width = entry.width;
  out.height = entry.height;
  out.advance = entry.advance;
  out.xOffset = entry.xOffset;
  out.yOffset = entry.yOffset;
  return true;
}

}  // namespace ui
