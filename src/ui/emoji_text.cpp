#include "emoji_text.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

#include "emoji_assets.h"
#include "nerd_icons.h"

namespace ui::emoji_text {
namespace {

struct Codepoint {
  uint32_t value;
  size_t end;
};

Codepoint decode(std::string_view text, size_t offset) {
  const uint8_t lead = static_cast<uint8_t>(text[offset]);
  uint8_t count = 1;
  uint32_t value = lead;
  if ((lead & 0xe0) == 0xc0) { count = 2; value = lead & 0x1f; }
  else if ((lead & 0xf0) == 0xe0) { count = 3; value = lead & 0x0f; }
  else if ((lead & 0xf8) == 0xf0) { count = 4; value = lead & 0x07; }
  else return {lead, offset + 1};

  if (offset + count > text.size()) return {lead, offset + 1};
  for (uint8_t i = 1; i < count; ++i) {
    const uint8_t byte = static_cast<uint8_t>(text[offset + i]);
    if ((byte & 0xc0) != 0x80) return {lead, offset + 1};
    value = (value << 6) | (byte & 0x3f);
  }
  if ((count == 2 && value < 0x80) || (count == 3 && value < 0x800) ||
      (count == 4 && value < 0x10000) || value > 0x10ffff ||
      (value >= 0xd800 && value <= 0xdfff))
    return {lead, offset + 1};
  return {value, offset + count};
}

bool isRegionalIndicator(uint32_t cp) { return cp >= 0x1f1e6 && cp <= 0x1f1ff; }
bool isModifier(uint32_t cp) { return cp >= 0x1f3fb && cp <= 0x1f3ff; }
bool isVariationSelector(uint32_t cp) { return cp == 0xfe0e || cp == 0xfe0f; }
bool isTag(uint32_t cp) { return cp >= 0xe0020 && cp <= 0xe007e; }
bool isEmojiBase(std::string_view text, size_t offset, const Codepoint& cp) {
  size_t end = cp.end;
  if (end < text.size()) {
    const Codepoint selector = decode(text, end);
    if (selector.value == 0xfe0f) end = selector.end;
  }
  emoji_assets::EmojiBitmap bitmap{};
  return emoji_assets::findEmoji(text.substr(offset, end - offset), bitmap);
}
bool isKeycapBase(uint32_t cp) {
  return cp == '#' || cp == '*' || (cp >= '0' && cp <= '9');
}
bool isPrivateUse(uint32_t cp) {
  return (cp >= 0xe000 && cp <= 0xf8ff) ||
         (cp >= 0xf0000 && cp <= 0xffffd) ||
         (cp >= 0x100000 && cp <= 0x10fffd);
}

size_t consumeExtenders(std::string_view text, size_t offset) {
  if (offset >= text.size()) return offset;
  Codepoint cp = decode(text, offset);
  if (isVariationSelector(cp.value)) {
    offset = cp.end;
    if (offset < text.size()) cp = decode(text, offset);
  }
  if (isModifier(cp.value)) offset = cp.end;
  return offset;
}

size_t emojiClusterEnd(std::string_view text, size_t start, uint32_t first) {
  Codepoint cp = decode(text, start);
  if (isRegionalIndicator(first)) {
    if (cp.end < text.size()) {
      const Codepoint second = decode(text, cp.end);
      if (isRegionalIndicator(second.value)) return second.end;
    }
    return cp.end;
  }

  if (isKeycapBase(first)) {
    size_t end = cp.end;
    if (end < text.size()) {
      Codepoint suffix = decode(text, end);
      if (isVariationSelector(suffix.value)) {
        end = suffix.end;
        if (end < text.size()) suffix = decode(text, end);
      }
      if (suffix.value == 0x20e3) return suffix.end;
    }
    return end;
  }

  size_t end = consumeExtenders(text, cp.end);
  // Emoji subdivision flags are black-flag + tag letters + cancel tag.
  if (first == 0x1f3f4) {
    size_t tagEnd = end;
    bool sawTag = false;
    while (tagEnd < text.size()) {
      const Codepoint tag = decode(text, tagEnd);
      if (!isTag(tag.value)) break;
      sawTag = true;
      tagEnd = tag.end;
    }
    if (sawTag) {
      end = tagEnd;
      if (tagEnd < text.size()) {
        const Codepoint cancel = decode(text, tagEnd);
        if (cancel.value == 0xe007f) end = cancel.end;
      }
    }
  }

  // ZWJ sequences are one token; only complete graphemes are eligible for an
  // atlas match, so an unsupported family never becomes a row of partial icons.
  while (end < text.size()) {
    const Codepoint joiner = decode(text, end);
    if (joiner.value != 0x200d) break;
    end = joiner.end;
    if (end >= text.size()) break;
    const Codepoint component = decode(text, end);
    if (!isEmojiBase(text, end, component)) break;
    end = consumeExtenders(text, component.end);
  }
  return end;
}

String toString(std::string_view bytes) {
  const std::string copy(bytes);
  return String(copy.c_str());
}

uint8_t alphaAt(const uint8_t* alpha4, uint8_t width, uint8_t x, uint8_t y) {
  const size_t index = static_cast<size_t>(y) * ((width + 1) / 2) * 2 + x;
  const uint8_t packed = alpha4[index >> 1];
  return static_cast<uint8_t>((index & 1) ? packed >> 4 : packed & 0x0f);
}

uint16_t blend565(uint16_t foreground, uint16_t background, uint8_t alpha) {
  if (alpha >= 15) return foreground;
  if (!alpha) return background;
  const uint16_t r = static_cast<uint16_t>(((foreground >> 11) & 0x1f) * alpha +
      ((background >> 11) & 0x1f) * (15 - alpha) + 7) / 15;
  const uint16_t g = static_cast<uint16_t>(((foreground >> 5) & 0x3f) * alpha +
      ((background >> 5) & 0x3f) * (15 - alpha) + 7) / 15;
  const uint16_t b = static_cast<uint16_t>((foreground & 0x1f) * alpha +
      (background & 0x1f) * (15 - alpha) + 7) / 15;
  return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}

uint8_t scaledDimension(uint8_t value, float scale) {
  if (!std::isfinite(scale) || scale <= 0.0f) return 1;
  const double scaled = static_cast<double>(value) * scale;
  if (scaled >= 255.0) return 255;
  return static_cast<uint8_t>(std::max<long>(1, std::lround(scaled)));
}

template <typename Pixel>
void drawBitmap(lgfx::LGFX_Sprite& sprite, int16_t x, int16_t y,
                uint8_t width, uint8_t height,
                int8_t xOffset, int8_t yOffset, float scale,
                uint16_t foreground, uint16_t background, Pixel pixelAt) {
  const uint8_t drawWidth = scaledDimension(width, scale);
  const uint8_t drawHeight = scaledDimension(height, scale);
  const int16_t left = static_cast<int16_t>(x + std::lround(xOffset * scale));
  const int16_t top = static_cast<int16_t>(y + std::lround(yOffset * scale));
  std::array<uint16_t, 255> row{};
  for (uint8_t dy = 0; dy < drawHeight; ++dy) {
    const uint8_t sy = static_cast<uint8_t>(static_cast<uint16_t>(dy) * height / drawHeight);
    int16_t runStart = -1;
    for (uint16_t dx = 0; dx <= drawWidth; ++dx) {
      const bool visible = dx < drawWidth && pixelAt(
          static_cast<uint8_t>(dx * width / drawWidth), sy,
          row[dx], foreground, background);
      if (visible && runStart < 0) runStart = dx;
      if ((!visible || dx == drawWidth) && runStart >= 0) {
        sprite.pushImage(left + runStart, top + dy, dx - runStart, 1,
                         row.data() + runStart);
        runStart = -1;
      }
    }
  }
}

}  // namespace

bool nextToken(std::string_view text, size_t& offset, Token& token) {
  if (offset >= text.size()) return false;
  token = {};
  token.begin = offset;
  const Codepoint first = decode(text, offset);
  if (isRegionalIndicator(first.value) || isEmojiBase(text, offset, first) ||
      isKeycapBase(first.value)) {
    const size_t clusterEnd = emojiClusterEnd(text, offset, first.value);
    const std::string_view cluster = text.substr(offset, clusterEnd - offset);
    emoji_assets::EmojiBitmap bitmap{};
    if (emoji_assets::findEmoji(cluster, bitmap)) {
      token.kind = TokenKind::Emoji;
      token.end = clusterEnd;
      offset = clusterEnd;
      return true;
    }
    // Keep unsupported sequences together for safe fallback rendering.
    if (clusterEnd > first.end) {
      token.end = clusterEnd;
      offset = clusterEnd;
      return true;
    }
  }
  if (isPrivateUse(first.value)) {
    NerdIconBitmap icon{};
    if (findNerdIcon(first.value, icon)) {
      token.kind = TokenKind::NerdIcon;
      token.codepoint = first.value;
    }
  }
  token.end = first.end;
  offset = token.end;
  return true;
}

int16_t tokenWidth(lgfx::LGFX_Sprite& sprite, std::string_view text,
                   const Token& token, float scale) {
  sprite.setTextSize(scale);
  if (token.kind == TokenKind::Emoji) {
    emoji_assets::EmojiBitmap bitmap{};
    if (emoji_assets::findEmoji(text.substr(token.begin, token.end - token.begin), bitmap))
      return scaledDimension(bitmap.advance, scale);
  } else if (token.kind == TokenKind::NerdIcon) {
    NerdIconBitmap icon{};
    if (findNerdIcon(token.codepoint, icon)) return scaledDimension(icon.advance, scale);
  }
  return static_cast<int16_t>(sprite.textWidth(toString(text.substr(token.begin, token.end - token.begin))));
}

int16_t measure(lgfx::LGFX_Sprite& sprite, std::string_view text, float scale) {
  int32_t width = 0;
  size_t offset = 0;
  Token token;
  while (nextToken(text, offset, token)) width += tokenWidth(sprite, text, token, scale);
  return static_cast<int16_t>(std::min<int32_t>(width, INT16_MAX));
}

void draw(lgfx::LGFX_Sprite& sprite, std::string_view text, int16_t x,
          int16_t y, uint16_t foreground, uint16_t background, float scale) {
  sprite.setTextSize(scale);
  size_t offset = 0;
  size_t runBegin = 0;
  int16_t cursorX = x;
  Token token;
  auto flushText = [&](size_t end) {
    if (end <= runBegin) return;
    const String run = toString(text.substr(runBegin, end - runBegin));
    sprite.setTextColor(foreground, background);
    sprite.setCursor(cursorX, y);
    sprite.print(run);
    cursorX = static_cast<int16_t>(cursorX + sprite.textWidth(run));
  };

  while (nextToken(text, offset, token)) {
    if (token.kind == TokenKind::Text) continue;
    flushText(token.begin);
    runBegin = token.end;
    if (token.kind == TokenKind::Emoji) {
      emoji_assets::EmojiBitmap bitmap{};
      const std::string_view cluster = text.substr(token.begin, token.end - token.begin);
      if (emoji_assets::findEmoji(cluster, bitmap) && bitmap.alpha4 && bitmap.rgb565) {
        drawBitmap(sprite, cursorX, y, bitmap.width, bitmap.height,
                   0, 0, scale, foreground, background,
                   [&](uint8_t sx, uint8_t sy, uint16_t& color, uint16_t, uint16_t bg) {
          const uint8_t alpha = alphaAt(bitmap.alpha4, bitmap.width, sx, sy);
          if (!alpha) return false;
          const uint16_t rgb = bitmap.rgb565[static_cast<size_t>(sy) * bitmap.width + sx];
          color = blend565(rgb, bg, alpha);
          return true;
        });
        cursorX = static_cast<int16_t>(cursorX + scaledDimension(bitmap.advance, scale));
      }
    } else {
      NerdIconBitmap icon{};
      if (findNerdIcon(token.codepoint, icon)) {
        if (icon.alpha4 && icon.width && icon.height) {
          drawBitmap(sprite, cursorX, y, icon.width, icon.height,
                     icon.xOffset, icon.yOffset, scale, foreground, background,
                     [&](uint8_t sx, uint8_t sy, uint16_t& color, uint16_t fg, uint16_t bg) {
            const uint8_t alpha = alphaAt(icon.alpha4, icon.width, sx, sy);
            if (!alpha) return false;
            color = blend565(fg, bg, alpha);
            return true;
          });
        }
        cursorX = static_cast<int16_t>(cursorX + scaledDimension(icon.advance, scale));
      }
    }
  }
  flushText(text.size());
}

}  // namespace ui::emoji_text
