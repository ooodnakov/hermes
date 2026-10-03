#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ui::markdown {

constexpr std::size_t kMaxSourceBytes = 4095;
constexpr std::size_t kMaxBlocks = 128;
constexpr std::size_t kMaxSpans = 512;
constexpr std::size_t kMaxDestinationBytes = 2048;

enum class BlockKind : std::uint8_t {
  Paragraph,
  Heading,
  BulletItem,
  OrderedItem,
  CodeBlock,
  Blank,
};

enum InlineStyle : std::uint8_t {
  Plain = 0,
  Bold = 1U << 0,
  Italic = 1U << 1,
  InlineCode = 1U << 2,
  Link = 1U << 3,
};

// Offsets and lengths are UTF-8 byte ranges, not character counts. A span's
// text is in Document::text; a Link span's destination is in
// Document::destinations. Adjacent equal-style spans may be coalesced.
struct Span {
  std::uint16_t textBegin = 0;
  std::uint16_t textLength = 0;
  std::uint16_t destinationBegin = 0;
  std::uint16_t destinationLength = 0;
  std::uint8_t style = Plain;
};

// Blocks refer to a contiguous range in Document::spans. Heading levels are
// 1..6; listNumber is populated only for OrderedItem.
struct Block {
  BlockKind kind = BlockKind::Paragraph;
  std::uint8_t headingLevel = 0;
  std::uint16_t listNumber = 0;
  std::uint16_t firstSpan = 0;
  std::uint16_t spanCount = 0;
};

struct Document {
  // UTF-8 display text with recognized Markdown syntax removed. Paragraph
  // soft line breaks and all code-block bytes/newlines are retained. Source
  // block separators are represented by Blocks, not extra text bytes.
  std::string text;
  std::string destinations;
  std::vector<Block> blocks;
  std::vector<Span> spans;

  void clear() {
    text.clear();
    destinations.clear();
    blocks.clear();
    spans.clear();
  }
};

// Parses a deliberately small, bounded Markdown subset. Oversize input or
// output-limit overflow is rejected and clears output; malformed/unmatched
// markup stays literal.
bool parse(std::string_view source, Document& output);

}  // namespace ui::markdown

#include "markdown_impl.h"
