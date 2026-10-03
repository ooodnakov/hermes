#pragma once

#include <algorithm>
#include <cctype>
#include <limits>

namespace ui::markdown {
namespace detail {

constexpr std::uint8_t kMaxInlineDepth = 8;

inline bool whitespace(char c) { return c == ' ' || c == '\t'; }
inline bool asciiWord(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9');
}
inline bool delimiterBoundary(char c) { return !asciiWord(c); }
inline bool escaped(std::string_view text, std::size_t at) {
  std::size_t backslashes = 0;
  while (at > backslashes && text[at - backslashes - 1] == '\\') ++backslashes;
  return (backslashes & 1U) != 0;
}

inline bool beginBlock(Document& doc, BlockKind kind, std::uint16_t& index,
                       std::uint8_t heading = 0, std::uint16_t number = 0) {
  if (doc.blocks.size() >= kMaxBlocks) return false;
  doc.blocks.push_back(Block{kind, heading, number,
      static_cast<std::uint16_t>(doc.spans.size()), 0});
  index = static_cast<std::uint16_t>(doc.blocks.size() - 1);
  return true;
}

inline bool addSpan(Document& doc, std::uint16_t blockIndex,
                    std::string_view text, std::uint8_t style,
                    std::uint16_t destinationBegin = 0,
                    std::uint16_t destinationLength = 0) {
  if (text.empty()) return true;
  if (blockIndex >= doc.blocks.size() || doc.text.size() + text.size() > kMaxSourceBytes)
    return false;
  const auto textBegin = static_cast<std::uint16_t>(doc.text.size());
  Block& block = doc.blocks[blockIndex];
  if (block.spanCount) {
    Span& previous = doc.spans.back();
    if (previous.textBegin + previous.textLength == textBegin &&
        previous.style == style &&
        previous.destinationBegin == destinationBegin &&
        previous.destinationLength == destinationLength) {
      doc.text.append(text.data(), text.size());
      previous.textLength = static_cast<std::uint16_t>(previous.textLength + text.size());
      return true;
    }
  }
  if (doc.spans.size() >= kMaxSpans) return false;
  doc.text.append(text.data(), text.size());
  if (!block.spanCount) block.firstSpan = static_cast<std::uint16_t>(doc.spans.size());
  doc.spans.push_back(Span{textBegin, static_cast<std::uint16_t>(text.size()),
                           destinationBegin, destinationLength, style});
  ++block.spanCount;
  return true;
}

inline bool canOpen(std::string_view text, std::size_t at, std::size_t length) {
  if (at + length >= text.size() || whitespace(text[at + length]) ||
      (static_cast<unsigned char>(text[at + length]) < 0x80 &&
       std::ispunct(static_cast<unsigned char>(text[at + length])))) return false;
  return at == 0 || delimiterBoundary(text[at - 1]);
}

inline bool canClose(std::string_view text, std::size_t at, std::size_t length) {
  if (at == 0 || whitespace(text[at - 1])) return false;
  // Double-underscore identifiers followed by a filename/path suffix (for
  // example __init__.py) are common command text, so keep their delimiters.
  if (length == 2 && text[at] == '_' && at + length < text.size() &&
      (text[at + length] == '.' || text[at + length] == '/' ||
       text[at + length] == '\\')) return false;
  return at + length == text.size() || delimiterBoundary(text[at + length]);
}

inline bool findClosing(std::string_view text, std::size_t start,
                        char marker, std::size_t length, std::size_t& close) {
  for (std::size_t i = start; i + length <= text.size();) {
    if (text[i] != marker) { ++i; continue; }
    std::size_t run = 1;
    while (i + run < text.size() && text[i + run] == marker) ++run;
    if (run == length && !escaped(text, i) && canClose(text, i, length)) {
      close = i;
      return true;
    }
    i += run;
  }
  return false;
}

inline bool findCodeClose(std::string_view text, std::size_t start,
                          std::size_t ticks, std::size_t& close) {
  for (std::size_t i = start; i + ticks <= text.size();) {
    if (text[i] != '`') { ++i; continue; }
    std::size_t run = 1;
    while (i + run < text.size() && text[i + run] == '`') ++run;
    if (run == ticks) { close = i; return true; }
    i += run;
  }
  return false;
}

inline std::size_t runLength(std::string_view text, std::size_t at, char marker) {
  std::size_t length = 0;
  while (at + length < text.size() && text[at + length] == marker) ++length;
  return length;
}

inline bool parseInline(Document& doc, std::uint16_t blockIndex,
                        std::string_view text, std::uint8_t style = Plain,
                        std::uint16_t destinationBegin = 0,
                        std::uint16_t destinationLength = 0,
                        std::uint8_t depth = 0) {
  if (depth >= kMaxInlineDepth) {
    return addSpan(doc, blockIndex, text, style, destinationBegin, destinationLength);
  }
  std::size_t literalStart = 0;
  std::size_t at = 0;
  while (at < text.size()) {
    if (text[at] == '`' && !escaped(text, at)) {
      const std::size_t ticks = runLength(text, at, '`');
      std::size_t close = 0;
      if (findCodeClose(text, at + ticks, ticks, close)) {
        if (!addSpan(doc, blockIndex, text.substr(literalStart, at - literalStart),
                     style, destinationBegin, destinationLength) ||
            !addSpan(doc, blockIndex, text.substr(at + ticks, close - at - ticks),
                     static_cast<std::uint8_t>(style | InlineCode),
                     destinationBegin, destinationLength)) return false;
        at = close + ticks;
        literalStart = at;
        continue;
      }
      at += ticks;
      continue;
    }

    if (text[at] == '[' && !escaped(text, at) && !(style & Link)) {
      const std::size_t labelEnd = text.find(']', at + 1);
      if (labelEnd != std::string_view::npos && labelEnd + 1 < text.size() &&
          text[labelEnd + 1] == '(') {
        std::size_t destEnd = labelEnd + 2;
        std::uint16_t nesting = 1;
        for (; destEnd < text.size() && nesting; ++destEnd) {
          if (text[destEnd] == '(') ++nesting;
          else if (text[destEnd] == ')') --nesting;
        }
        if (!nesting) {
          const std::string_view destination =
              text.substr(labelEnd + 2, destEnd - labelEnd - 3);
          if (doc.destinations.size() + destination.size() > kMaxDestinationBytes ||
              !addSpan(doc, blockIndex, text.substr(literalStart, at - literalStart),
                       style, destinationBegin, destinationLength)) return false;
          const auto linkBegin = static_cast<std::uint16_t>(doc.destinations.size());
          if (!destination.empty()) doc.destinations.append(destination.data(), destination.size());
          if (!parseInline(doc, blockIndex, text.substr(at + 1, labelEnd - at - 1),
                           static_cast<std::uint8_t>(style | Link), linkBegin,
                           static_cast<std::uint16_t>(destination.size()), depth + 1)) return false;
          at = destEnd;
          literalStart = at;
          continue;
        }
      }
    }

    const char marker = text[at];
    if ((marker == '*' || marker == '_') &&
        !escaped(text, at) && (at == 0 || text[at - 1] != marker)) {
      const std::size_t length =
          at + 1 < text.size() && text[at + 1] == marker ? 2 : 1;
      if (canOpen(text, at, length)) {
        std::size_t close = 0;
        if (findClosing(text, at + length, marker, length, close)) {
          const std::uint8_t added = length == 2 ? Bold : Italic;
          if (!addSpan(doc, blockIndex, text.substr(literalStart, at - literalStart),
                       style, destinationBegin, destinationLength)) return false;
          if (!parseInline(doc, blockIndex,
                           text.substr(at + length, close - at - length),
                           static_cast<std::uint8_t>(style | added), destinationBegin,
                           destinationLength, depth + 1)) return false;
          at = close + length;
          literalStart = at;
          continue;
        }
      }
      at += length;
      continue;
    }
    ++at;
  }
  return addSpan(doc, blockIndex, text.substr(literalStart), style,
                 destinationBegin, destinationLength);
}

struct LineMarker {
  BlockKind kind;
  std::size_t content;
  std::uint8_t heading = 0;
  std::uint16_t number = 0;
};

inline bool parseLineMarker(std::string_view line, LineMarker& marker) {
  std::size_t at = 0;
  while (at < line.size() && at < 3 && line[at] == ' ') ++at;
  if (at < line.size() && line[at] == '#') {
    const std::size_t start = at;
    while (at < line.size() && line[at] == '#' && at - start < 6) ++at;
    const std::size_t count = at - start;
    if (count && (at == line.size() || whitespace(line[at]))) {
      if (at < line.size()) ++at;
      marker = {BlockKind::Heading, at, static_cast<std::uint8_t>(count), 0};
      return true;
    }
  }
  if (at + 1 < line.size() &&
      (line[at] == '-' || line[at] == '+' || line[at] == '*') && whitespace(line[at + 1])) {
    marker = {BlockKind::BulletItem, at + 2, 0, 0};
    return true;
  }
  std::size_t digits = at;
  std::uint32_t number = 0;
  while (digits < line.size() && line[digits] >= '0' && line[digits] <= '9') {
    number = std::min<std::uint32_t>(65535, number * 10U + (line[digits] - '0'));
    ++digits;
  }
  if (digits > at && digits + 1 < line.size() &&
      (line[digits] == '.' || line[digits] == ')') && whitespace(line[digits + 1])) {
    marker = {BlockKind::OrderedItem, digits + 2, 0, static_cast<std::uint16_t>(number)};
    return true;
  }
  return false;
}

inline bool parseFence(std::string_view line, char& marker, std::size_t& count,
                       std::size_t& after) {
  std::size_t at = 0;
  while (at < line.size() && at < 3 && line[at] == ' ') ++at;
  if (at >= line.size() || (line[at] != '`' && line[at] != '~')) return false;
  const char candidate = line[at];
  const std::size_t length = runLength(line, at, candidate);
  if (length < 3) return false;
  marker = candidate;
  count = length;
  after = at + length;
  return true;
}

inline bool isFenceClose(std::string_view line, char marker, std::size_t minimum) {
  char candidate = 0;
  std::size_t count = 0, after = 0;
  if (!parseFence(line, candidate, count, after) || candidate != marker || count < minimum) return false;
  for (; after < line.size(); ++after) if (!whitespace(line[after])) return false;
  return true;
}

inline bool blankLine(std::string_view line) {
  return std::all_of(line.begin(), line.end(), whitespace);
}

}  // namespace detail

inline bool parse(std::string_view source, Document& output) {
  output.clear();
  if (source.size() > kMaxSourceBytes) return false;
  if (source.empty()) return true;
  output.text.reserve(source.size());
  output.destinations.reserve(std::min(source.size(), kMaxDestinationBytes));
  output.blocks.reserve(std::min((source.size() + 1) / 2, kMaxBlocks));
  output.spans.reserve(std::min(source.size(), kMaxSpans));

  std::uint16_t paragraph = std::numeric_limits<std::uint16_t>::max();
  bool inFence = false;
  char fenceMarker = 0;
  std::size_t fenceLength = 0;
  std::uint16_t codeBlock = std::numeric_limits<std::uint16_t>::max();
  std::string codeContent;
  std::size_t lineStart = 0;
  while (lineStart < source.size()) {
    const std::size_t newline = source.find('\n', lineStart);
    const bool hasNewline = newline != std::string_view::npos;
    const std::size_t lineEnd = hasNewline ? newline : source.size();
    std::string_view line = source.substr(lineStart, lineEnd - lineStart);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);

    if (inFence) {
      if (detail::isFenceClose(line, fenceMarker, fenceLength)) {
        if (!codeContent.empty() && !detail::addSpan(output, codeBlock, codeContent, InlineCode)) {
          output.clear();
          return false;
        }
        codeContent.clear();
        inFence = false;
        codeBlock = std::numeric_limits<std::uint16_t>::max();
      } else {
        codeContent.append(line.data(), line.size());
        if (hasNewline) codeContent.push_back('\n');
      }
    } else {
      char marker = 0;
      std::size_t count = 0, after = 0;
      if (detail::parseFence(line, marker, count, after)) {
        paragraph = std::numeric_limits<std::uint16_t>::max();
        if (!detail::beginBlock(output, BlockKind::CodeBlock, codeBlock)) {
          output.clear();
          return false;
        }
        codeContent.clear();
        fenceMarker = marker;
        fenceLength = count;
        inFence = true;
      } else if (detail::blankLine(line)) {
        paragraph = std::numeric_limits<std::uint16_t>::max();
        std::uint16_t blank = 0;
        if (!detail::beginBlock(output, BlockKind::Blank, blank)) {
          output.clear();
          return false;
        }
      } else {
        detail::LineMarker parsed{};
        const bool structural = detail::parseLineMarker(line, parsed);
        if (structural) {
          paragraph = std::numeric_limits<std::uint16_t>::max();
          std::uint16_t block = 0;
          if (!detail::beginBlock(output, parsed.kind, block,
                                  parsed.heading, parsed.number) ||
              !detail::parseInline(output, block, line.substr(parsed.content))) {
            output.clear();
            return false;
          }
        } else {
          if (paragraph == std::numeric_limits<std::uint16_t>::max() &&
              !detail::beginBlock(output, BlockKind::Paragraph, paragraph)) {
            output.clear();
            return false;
          }
          if ((output.blocks[paragraph].spanCount &&
               !detail::addSpan(output, paragraph, "\n", Plain)) ||
              !detail::parseInline(output, paragraph, line)) {
            output.clear();
            return false;
          }
        }
      }
    }
    if (!hasNewline) break;
    lineStart = newline + 1;
  }

  // An unclosed fence follows the usual Markdown convention: it runs to EOF.
  if (inFence && !codeContent.empty() &&
      !detail::addSpan(output, codeBlock, codeContent, InlineCode)) {
    output.clear();
    return false;
  }
  return true;
}

}  // namespace ui::markdown
