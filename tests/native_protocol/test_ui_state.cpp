#include "../../src/protocol/ui_state.h"

#include <ArduinoJson.h>
#include <cassert>
#include <string>

namespace {
String makeStateFrame(const std::string& markdown, bool truncated) {
  JsonDocument document;
  document["type"] = "state";
  document["running"] = 0;
  document["waiting"] = 0;
  document["msg"] = "short compact preview";
  document["msg_markdown"] = markdown;
  document["msg_markdown_truncated"] = truncated;
  char serialized[16384];
  const size_t length = serializeJson(document, serialized, sizeof(serialized));
  assert(length < sizeof(serialized));
  return String(serialized);
}

void testMarkdownAndCompactPreviewAreIndependent() {
  protocol::UiState state;
  const std::string raw = "## Привет\n\n**Ответ** со [ссылкой](https://example.test)\n\n```py\nprint('да')\n```";
  assert(protocol::applyJsonFrame(state, makeStateFrame(raw, false), 1000));
  assert(state.message.str() == "short compact preview");
  assert(state.agentResponseMarkdown.str() == raw);
  assert(!state.agentResponseMarkdownTruncated);
}

void testMarkdownCapDoesNotSplitUtf8() {
  protocol::UiState state;
  const std::string raw = std::string(3071, 'a') + "Жtail";
  assert(protocol::applyJsonFrame(state, makeStateFrame(raw, false), 2000));
  assert(state.agentResponseMarkdown.length() <= 3072);
  assert(state.agentResponseMarkdown.length() == 3071);
  assert(state.agentResponseMarkdown.str() == std::string(3071, 'a'));
  assert(state.agentResponseMarkdownTruncated);
}

void testHostTruncatedFlagSurvivesParsingAndEmptyFieldClears() {
  protocol::UiState state;
  assert(protocol::applyJsonFrame(state, makeStateFrame("## clipped\n\n[Response clipped]", true), 3000));
  assert(state.agentResponseMarkdownTruncated);
  assert(protocol::applyJsonFrame(state, makeStateFrame("", false), 3100));
  assert(state.agentResponseMarkdown.isEmpty());
  assert(!state.agentResponseMarkdownTruncated);
}
}  // namespace

int main() {
  testMarkdownAndCompactPreviewAreIndependent();
  testMarkdownCapDoesNotSplitUtf8();
  testHostTruncatedFlagSurvivesParsingAndEmptyFieldClears();
}
