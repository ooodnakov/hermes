#include "emoji_assets.h"

#include <cstring>

namespace ui::emoji_assets {
namespace {

#include "emoji_assets_data.inc"

constexpr std::size_t kPixelsPerEmoji = 12U * 12U;
constexpr std::size_t kAlphaBytesPerEmoji = kPixelsPerEmoji / 2U;
static_assert(sizeof(kEmojiRecords) / sizeof(kEmojiRecords[0]) == kEmojiCount,
              "emoji lookup record count mismatch");
static_assert(sizeof(kEmojiRgb565) / sizeof(kEmojiRgb565[0]) ==
                  kEmojiCount * kPixelsPerEmoji,
              "emoji RGB565 atlas length mismatch");
static_assert(sizeof(kEmojiAlpha4) == kEmojiCount * kAlphaBytesPerEmoji,
              "emoji A4 atlas length mismatch");
static_assert(sizeof(kEmojiKeys) <= UINT16_MAX,
              "emoji key offsets exceed uint16 range");

bool decodeUtf8(std::string_view text, std::size_t offset, std::uint32_t& codepoint,
                std::size_t& bytes) {
  if (offset >= text.size()) return false;
  const auto lead = static_cast<std::uint8_t>(text[offset]);
  if (lead < 0x80) {
    codepoint = lead;
    bytes = 1;
    return true;
  }

  std::uint32_t value = 0;
  std::uint32_t minimum = 0;
  if ((lead & 0xE0U) == 0xC0U) {
    value = lead & 0x1FU;
    minimum = 0x80;
    bytes = 2;
  } else if ((lead & 0xF0U) == 0xE0U) {
    value = lead & 0x0FU;
    minimum = 0x800;
    bytes = 3;
  } else if ((lead & 0xF8U) == 0xF0U) {
    value = lead & 0x07U;
    minimum = 0x10000;
    bytes = 4;
  } else {
    return false;
  }
  if (offset + bytes > text.size()) return false;
  for (std::size_t i = 1; i < bytes; ++i) {
    const auto continuation = static_cast<std::uint8_t>(text[offset + i]);
    if ((continuation & 0xC0U) != 0x80U) return false;
    value = (value << 6U) | (continuation & 0x3FU);
  }
  if (value < minimum || value > 0x10FFFFU ||
      (value >= 0xD800U && value <= 0xDFFFU)) {
    return false;
  }
  codepoint = value;
  return true;
}

bool findKey(const char* key, std::size_t length, std::uint16_t& bitmapIndex) {
  std::size_t first = 0;
  std::size_t last = kEmojiCount;
  while (first < last) {
    const std::size_t middle = first + (last - first) / 2U;
    const EmojiRecord& record = kEmojiRecords[middle];
    if (record.keyOffset > sizeof(kEmojiKeys) ||
        record.keyLength > sizeof(kEmojiKeys) - record.keyOffset ||
        record.bitmapIndex >= kEmojiCount) {
      return false;
    }
    const std::size_t common = length < record.keyLength ? length : record.keyLength;
    const int order = std::memcmp(key, kEmojiKeys + record.keyOffset, common);
    const int comparison = order ? order : (length < record.keyLength ? -1
                                                   : length > record.keyLength ? 1
                                                                               : 0);
    if (comparison < 0) {
      last = middle;
    } else if (comparison > 0) {
      first = middle + 1U;
    } else {
      bitmapIndex = record.bitmapIndex;
      return true;
    }
  }
  return false;
}

void setBitmap(std::uint16_t bitmapIndex, EmojiBitmap& out) {
  out.rgb565 = kEmojiRgb565 + static_cast<std::size_t>(bitmapIndex) * kPixelsPerEmoji;
  out.alpha4 = kEmojiAlpha4 + static_cast<std::size_t>(bitmapIndex) * kAlphaBytesPerEmoji;
  out.width = 12;
  out.height = 12;
  out.advance = 13;
}

}  // namespace

bool findEmojiPrefix(std::string_view utf8, std::size_t& matchedBytes,
                     EmojiBitmap& out) {
  matchedBytes = 0;
  out = {};
  char key[64]{};
  std::size_t keyLength = 0;
  std::size_t offset = 0;
  std::uint8_t codepointCount = 0;
  bool previousWasVs16 = false;
  std::uint16_t bestBitmap = 0;

  // Bound the scan even if a malformed or non-emoji prefix contains only
  // repeated variation selectors. Any source key can contain at most one
  // selector per source codepoint.
  const std::size_t maxSourceBytes =
      static_cast<std::size_t>(kEmojiMaxKeyBytes) +
      static_cast<std::size_t>(kEmojiMaxCodepoints) * 3U;
  const std::size_t scanLimit = utf8.size() < maxSourceBytes ? utf8.size() : maxSourceBytes;
  while (offset < scanLimit) {
    std::uint32_t codepoint = 0;
    std::size_t bytes = 0;
    if (!decodeUtf8(utf8, offset, codepoint, bytes) || offset + bytes > scanLimit) break;
    if (codepoint == 0xFE0FU) {
      if (!codepointCount || previousWasVs16) break;
      previousWasVs16 = true;
    } else {
      if (codepointCount >= kEmojiMaxCodepoints || keyLength + bytes > kEmojiMaxKeyBytes ||
          keyLength + bytes > sizeof(key)) {
        break;
      }
      std::memcpy(key + keyLength, utf8.data() + offset, bytes);
      keyLength += bytes;
      ++codepointCount;
      previousWasVs16 = false;
    }
    offset += bytes;
    std::uint16_t bitmapIndex = 0;
    if (keyLength && findKey(key, keyLength, bitmapIndex)) {
      bestBitmap = bitmapIndex;
      matchedBytes = offset;
    }
  }

  if (!matchedBytes) return false;
  setBitmap(bestBitmap, out);
  return true;
}

bool findEmoji(std::string_view exactCluster, EmojiBitmap& out) {
  std::size_t matchedBytes = 0;
  if (!findEmojiPrefix(exactCluster, matchedBytes, out) ||
      matchedBytes != exactCluster.size()) {
    out = {};
    return false;
  }
  return true;
}

}  // namespace ui::emoji_assets
