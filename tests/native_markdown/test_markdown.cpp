#include "../../src/ui/markdown.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>

namespace {
using ui::markdown::BlockKind;
using ui::markdown::Document;
using ui::markdown::InlineCode;
using ui::markdown::Link;
using ui::markdown::Span;

std::string_view spanText(const Document& doc, const Span& span) {
  return std::string_view(doc.text).substr(span.textBegin, span.textLength);
}

std::string_view spanDestination(const Document& doc, const Span& span) {
  return std::string_view(doc.destinations).substr(
      span.destinationBegin, span.destinationLength);
}

bool containsSpan(const Document& doc, std::string_view text, uint8_t style) {
  for (const Span& span : doc.spans)
    if (spanText(doc, span) == text && (span.style & style) == style) return true;
  return false;
}

void testBlocksAndInlineStyles() {
  Document doc;
  const std::string source =
      "# Header\n\nA **bold** and *italic* response with [docs](https://example.test/a_(b)).\n"
      "- first bullet\n+ second bullet\n1. first ordered\n2) second ordered";
  assert(ui::markdown::parse(source, doc));
  assert(doc.blocks.size() == 7);
  assert(doc.blocks[0].kind == BlockKind::Heading && doc.blocks[0].headingLevel == 1);
  assert(doc.blocks[1].kind == BlockKind::Blank);
  assert(doc.blocks[2].kind == BlockKind::Paragraph);
  assert(doc.blocks[3].kind == BlockKind::BulletItem);
  assert(doc.blocks[4].kind == BlockKind::BulletItem);
  assert(doc.blocks[5].kind == BlockKind::OrderedItem && doc.blocks[5].listNumber == 1);
  assert(doc.blocks[6].kind == BlockKind::OrderedItem && doc.blocks[6].listNumber == 2);
  assert(containsSpan(doc, "bold", ui::markdown::Bold));
  assert(containsSpan(doc, "italic", ui::markdown::Italic));
  bool foundLink = false;
  for (const Span& span : doc.spans) {
    if (span.style & Link) {
      foundLink = spanText(doc, span) == "docs" &&
          spanDestination(doc, span) == "https://example.test/a_(b)";
    }
  }
  assert(foundLink);
}

void testLiteralCommandsAndUnmatchedMarkup() {
  Document doc;
  const std::string source =
      "rm *.json --flag --path C:\\tmp\\*.cfg python __init__.py "
      "and **unfinished *word \\*literal\\*";
  assert(ui::markdown::parse(source, doc));
  assert(doc.blocks.size() == 1 && doc.blocks[0].kind == BlockKind::Paragraph);
  assert(doc.text == source);
  for (const Span& span : doc.spans) assert(span.style == ui::markdown::Plain);
}

void testInlineAndFencedCodeRemainLiteral() {
  Document doc;
  const std::string source =
      "Run `rm *.json` now.\n\n```sh\nif [ -f a ]; then\n  rm *.json\n```\n"
      "~~~\nunfinished fence * stays code";
  assert(ui::markdown::parse(source, doc));
  assert(doc.blocks.size() == 4);
  assert(doc.blocks[0].kind == BlockKind::Paragraph);
  assert(containsSpan(doc, "rm *.json", InlineCode));
  assert(doc.blocks[1].kind == BlockKind::Blank);
  assert(doc.blocks[2].kind == BlockKind::CodeBlock);
  assert(doc.blocks[3].kind == BlockKind::CodeBlock);
  assert(doc.text.find("if [ -f a ]; then\n  rm *.json\n") != std::string::npos);
  assert(doc.text.find("unfinished fence * stays code") != std::string::npos);
}

void testUnicodeAndEmptyParagraphs() {
  Document doc;
  assert(ui::markdown::parse("Привет — **мир**\n\n第二段", doc));
  assert(doc.blocks.size() == 3);
  assert(doc.blocks[0].kind == BlockKind::Paragraph);
  assert(doc.blocks[1].kind == BlockKind::Blank);
  assert(doc.blocks[2].kind == BlockKind::Paragraph);
  assert(containsSpan(doc, "мир", ui::markdown::Bold));
  assert(doc.text.find("Привет — мир") != std::string::npos);
  assert(doc.text.find("第二段") != std::string::npos);
}

void testOversizedInputIsRejectedAndClearsOutput() {
  Document doc;
  assert(ui::markdown::parse("**old**", doc));
  const std::string tooLong(ui::markdown::kMaxSourceBytes + 1, 'x');
  assert(!ui::markdown::parse(tooLong, doc));
  assert(doc.text.empty() && doc.blocks.empty() && doc.spans.empty());
}

void testOutputLimitsRejectFragmentedAdversarialMarkup() {
  Document doc;
  std::string manySpans;
  for (std::size_t i = 0; i < ui::markdown::kMaxSpans; ++i)
    manySpans += (i & 1) ? "*x* " : "_y_ ";
  assert(!ui::markdown::parse(manySpans, doc));
  assert(doc.text.empty() && doc.blocks.empty() && doc.spans.empty());

  std::string manyBlocks;
  for (std::size_t i = 0; i <= ui::markdown::kMaxBlocks; ++i) manyBlocks += "x\n\n";
  assert(!ui::markdown::parse(manyBlocks, doc));
  assert(doc.text.empty() && doc.blocks.empty() && doc.spans.empty());

  const std::string longDestination = "[label](" +
      std::string(ui::markdown::kMaxDestinationBytes + 1, 'u') + ")";
  assert(!ui::markdown::parse(longDestination, doc));
  assert(doc.text.empty() && doc.destinations.empty());
}

void testPrefixParsingRetainsValidContentAtOutputLimits() {
  Document doc;
  std::string manyBlocks;
  for (std::size_t i = 0; i <= ui::markdown::kMaxBlocks; ++i)
    manyBlocks += "## Heading\n";
  assert(ui::markdown::parsePrefix(manyBlocks, doc));
  assert(doc.truncated && doc.blocks.size() == ui::markdown::kMaxBlocks);

  std::string manySpans;
  for (std::size_t i = 0; i <= ui::markdown::kMaxSpans / 2; ++i)
    manySpans += "**x** ";
  assert(ui::markdown::parsePrefix(manySpans, doc));
  assert(doc.truncated && doc.spans.size() == ui::markdown::kMaxSpans);
  for (const auto& block : doc.blocks) {
    assert(block.firstSpan + block.spanCount <= doc.spans.size());
    for (std::size_t i = block.firstSpan; i < block.firstSpan + block.spanCount; ++i) {
      const auto& span = doc.spans[i];
      assert(span.textBegin + span.textLength <= doc.text.size());
      assert(span.destinationBegin + span.destinationLength <= doc.destinations.size());
    }
  }

  const std::string tooLong(ui::markdown::kMaxSourceBytes + 1, 'x');
  assert(!ui::markdown::parsePrefix(tooLong, doc));
  assert(doc.text.empty() && doc.blocks.empty() && doc.spans.empty() && !doc.truncated);
}
}  // namespace

int main() {
  testBlocksAndInlineStyles();
  testLiteralCommandsAndUnmatchedMarkup();
  testInlineAndFencedCodeRemainLiteral();
  testUnicodeAndEmptyParagraphs();
  testOversizedInputIsRejectedAndClearsOutput();
  testOutputLimitsRejectFragmentedAdversarialMarkup();
  testPrefixParsingRetainsValidContentAtOutputLimits();
  std::cout << "Markdown parser native tests passed\n";
}
