#include <cassert>
#include <cstddef>
#include <string_view>

#include "../../src/ui/emoji_assets.h"

int main() {
  using ui::emoji_assets::EmojiBitmap;

  const std::string_view sequences[] = {
      u8"😀",       // supplementary-plane face
      u8"👍🏽",     // skin tone
      u8"❤️",       // variation selector
      u8"🚀",       // colorful symbol
      u8"🇺🇸",     // flag
      u8"👨‍👩‍👧‍👦", // family ZWJ
      u8"👩‍💻",     // profession ZWJ
      u8"1️⃣",     // keycap
  };
  for (const auto sequence : sequences) {
    EmojiBitmap bitmap;
    assert(ui::emoji_assets::findEmoji(sequence, bitmap));
    assert(bitmap.rgb565 != nullptr);
    assert(bitmap.alpha4 != nullptr);
    assert(bitmap.width == 12 && bitmap.height == 12 && bitmap.advance == 13);
  }

  EmojiBitmap bitmap;
  assert(!ui::emoji_assets::findEmoji(u8"❤️x", bitmap));
  assert(bitmap.rgb565 == nullptr && bitmap.alpha4 == nullptr);
  assert(!ui::emoji_assets::findEmoji(u8"❤︎", bitmap));  // explicit text presentation
  std::size_t matched = 99;
  assert(ui::emoji_assets::findEmojiPrefix(u8"🚀 next", matched, bitmap));
  assert(matched == std::string_view(u8"🚀").size());
  assert(bitmap.advance == 13);
  assert(!ui::emoji_assets::findEmojiPrefix("\xF0\x28\x8C\x28", matched, bitmap));
  assert(matched == 0);
}
