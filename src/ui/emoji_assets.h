#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ui::emoji_assets {

struct EmojiBitmap {
  const std::uint16_t* rgb565 = nullptr;
  // Row-major A4 coverage, low nibble for even pixels and high nibble for odd.
  const std::uint8_t* alpha4 = nullptr;
  std::uint8_t width = 0;
  std::uint8_t height = 0;
  std::uint8_t advance = 0;
};

// Finds the longest Twemoji sequence at the beginning of utf8. U+FE0F is
// ignored for asset matching but included in matchedBytes. U+FE0E is retained.
// On failure, matchedBytes is zero and out is cleared.
bool findEmojiPrefix(std::string_view utf8, std::size_t& matchedBytes,
                     EmojiBitmap& out);

// Finds an exact emoji cluster, including an optional U+FE0F presentation
// selector. Additional source bytes make this fail.
bool findEmoji(std::string_view exactCluster, EmojiBitmap& out);

}  // namespace ui::emoji_assets
