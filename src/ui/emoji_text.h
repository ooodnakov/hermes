#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include <LovyanGFX.hpp>

namespace ui::emoji_text {

enum class TokenKind : uint8_t { Text, Emoji, NerdIcon };

struct Token {
  size_t begin = 0;
  size_t end = 0;
  TokenKind kind = TokenKind::Text;
  uint32_t codepoint = 0;
};

// Returns a UTF-8-safe token. A supported emoji grapheme (including ZWJ,
// modifiers, flags, keycaps, and tag flags) is kept whole; unsupported
// graphemes remain one ordinary-text token so no source bytes are discarded.
bool nextToken(std::string_view text, size_t& offset, Token& token);

int16_t tokenWidth(lgfx::LGFX_Sprite& sprite, std::string_view text,
                   const Token& token, float scale = 1.0f);
int16_t measure(lgfx::LGFX_Sprite& sprite, std::string_view text,
                float scale = 1.0f);
void draw(lgfx::LGFX_Sprite& sprite, std::string_view text, int16_t x,
          int16_t y, uint16_t foreground, uint16_t background,
          float scale = 1.0f);

}  // namespace ui::emoji_text
