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

String serialize(JsonDocument& document) {
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

void testEntryIdsStayAlignedAndClearOnMissingOrInvalidIds() {
  protocol::UiState state;
  JsonDocument frame;
  frame["type"] = "state";
  JsonArray entries = frame["entries"].to<JsonArray>();
  JsonArray ids = frame["entry_ids"].to<JsonArray>();
  for (int i = 0; i < 7; ++i) {
    entries.add(std::string("entry-") + std::to_string(i));
    ids.add(std::string("id-") + std::to_string(i));
  }
  assert(protocol::applyJsonFrame(state, serialize(frame), 100));
  assert(state.entryCount == 5);
  assert(state.entryIds[0].str() == "id-0");
  assert(state.entryIds[4].str() == "id-4");

  JsonDocument missing;
  missing["type"] = "state";
  JsonArray nextEntries = missing["entries"].to<JsonArray>();
  nextEntries.add("new entry");
  assert(protocol::applyJsonFrame(state, serialize(missing), 200));
  assert(state.entryCount == 1);
  assert(state.entryIds[0].isEmpty());
  assert(state.entryIds[4].isEmpty());

  JsonDocument invalid;
  invalid["type"] = "state";
  invalid["entries"].to<JsonArray>().add("latest");
  invalid["entry_ids"] = true;
  assert(protocol::applyJsonFrame(state, serialize(invalid), 300));
  assert(state.entryIds[0].isEmpty());
}

void testHistoryIdsAreBoundedAndClearWithEachWindow() {
  protocol::UiState state;
  JsonDocument frame;
  frame["type"] = "msgs";
  JsonArray lines = frame["lines"].to<JsonArray>();
  JsonArray ids = frame["ids"].to<JsonArray>();
  for (int i = 0; i < 8; ++i) {
    lines.add(std::string("line-") + std::to_string(i));
    ids.add(std::string("history-") + std::to_string(i));
  }
  assert(protocol::applyJsonFrame(state, serialize(frame), 100));
  assert(state.historyCount == 5);
  assert(state.historyIds[0].str() == "history-0");
  assert(state.historyIds[4].str() == "history-4");

  JsonDocument next;
  next["type"] = "msgs";
  next["lines"].to<JsonArray>().add("older");
  JsonArray invalidIds = next["ids"].to<JsonArray>();
  invalidIds.add(44);
  assert(protocol::applyJsonFrame(state, serialize(next), 200));
  assert(state.historyCount == 1);
  assert(state.historyIds[0].isEmpty());
  assert(state.historyIds[4].isEmpty());
}

String makeMessageDetail(const char* id, const std::string& body, bool truncated) {
  JsonDocument document;
  document["type"] = "msg";
  document["id"] = id;
  document["body"] = body;
  document["role"] = "assistant";
  document["truncated"] = truncated;
  return serialize(document);
}

void testMessageDetailRequiresCurrentOpenPendingRequest() {
  protocol::UiState state;
  state.modalActive = true;
  state.messageDetailPending = true;
  state.messageDetailRequestedId = "message-2";
  assert(!protocol::applyJsonFrame(state, makeMessageDetail("message-1", "stale", false), 100));
  assert(state.messageDetailPending);
  assert(state.messageDetailBody.isEmpty());

  state.messageDetailRequestedId = "message-2";
  const std::string body = "Привет — полный ответ";
  assert(protocol::applyJsonFrame(state, makeMessageDetail("message-2", body, false), 200));
  assert(!state.messageDetailPending);
  assert(state.messageDetailBody.str() == body);
  assert(state.messageDetailRole.str() == "assistant");
  assert(!state.messageDetailTruncated);

  state.messageDetailPending = true;
  state.messageDetailRequestedId = "message-3";
  state.messageDetailBody = "clear before request";
  state.modalActive = false;
  assert(!protocol::applyJsonFrame(state, makeMessageDetail("message-3", "late", false), 300));
  assert(state.messageDetailPending);
  assert(state.messageDetailBody.str() == "clear before request");
}

void testMessageDetailUtf8CapAndStrictTruncatedBoolean() {
  protocol::UiState state;
  state.modalActive = true;
  state.messageDetailPending = true;
  state.messageDetailRequestedId = "large";
  const std::string body = std::string(3199, 'a') + "Жtail";
  JsonDocument frame;
  frame["type"] = "msg";
  frame["id"] = "large";
  frame["body"] = body;
  frame["role"] = "assistant";
  frame["truncated"] = "false";  // wrong JSON type must not coerce to true
  assert(protocol::applyJsonFrame(state, serialize(frame), 400));
  assert(state.messageDetailBody.length() == 3199);
  assert(state.messageDetailBody.str() == std::string(3199, 'a'));
  assert(state.messageDetailTruncated);

  state.messageDetailPending = true;
  state.messageDetailRequestedId = "host-clipped";
  JsonDocument clipped;
  clipped["type"] = "msg";
  clipped["id"] = "host-clipped";
  clipped["body"] = "partial response";
  clipped["truncated"] = true;
  assert(protocol::applyJsonFrame(state, serialize(clipped), 500));
  assert(state.messageDetailTruncated);
}

void testMessageDetailNotFoundAndMalformedResponses() {
  protocol::UiState state;
  state.modalActive = true;
  state.messageDetailPending = true;
  state.messageDetailRequestedId = "missing";
  JsonDocument notFound;
  notFound["type"] = "msg";
  notFound["id"] = "missing";
  notFound["error"] = "not found";
  assert(protocol::applyJsonFrame(state, serialize(notFound), 100));
  assert(!state.messageDetailPending);
  assert(state.messageDetailError.str() == "not found");

  state.messageDetailPending = true;
  state.messageDetailRequestedId = "bad";
  JsonDocument malformed;
  malformed["type"] = "msg";
  malformed["id"] = "bad";
  malformed["body"] = true;
  malformed["error"] = false;
  assert(!protocol::applyJsonFrame(state, serialize(malformed), 200));
  assert(state.messageDetailPending);

  malformed["error"] = "not found";
  assert(protocol::applyJsonFrame(state, serialize(malformed), 300));
  assert(state.messageDetailError.str() == "not found");
}

void testTransientPageCancelsDetailAndIgnoresLateReply() {
  protocol::UiState state;
  state.modalActive = true;
  state.modalBody = "message preview";
  state.messageDetailPending = true;
  state.messageDetailRequestedId = "selected-message";
  state.messageDetailBody = "old detail";
  state.messageDetailRole = "assistant";
  state.messageDetailError = "old error";
  state.messageDetailTruncated = true;

  JsonDocument page;
  page["type"] = "page";
  page["slot"] = 9;
  page["title"] = "TRANSIENT";
  page["lines"].to<JsonArray>().add("Temporary host notice");
  assert(protocol::applyJsonFrame(state, serialize(page), 100));
  assert(state.modalActive);
  assert(state.modal.title.str() == "TRANSIENT");
  assert(state.modalBody.isEmpty());
  assert(!state.messageDetailPending);
  assert(state.messageDetailRequestedId.isEmpty());
  assert(state.messageDetailBody.isEmpty());
  assert(state.messageDetailRole.isEmpty());
  assert(state.messageDetailError.isEmpty());
  assert(!state.messageDetailTruncated);

  assert(!protocol::applyJsonFrame(
      state, makeMessageDetail("selected-message", "late full response", false), 200));
  assert(state.modalActive);
  assert(state.modal.title.str() == "TRANSIENT");
  assert(state.modal.lines[0].str() == "Temporary host notice");
  assert(state.modalBody.isEmpty());
}

void testPermissionTakeoverCancelsMessageDetailRequest() {
  protocol::UiState state;
  state.modalActive = true;
  state.messageDetailPending = true;
  state.messageDetailRequestedId = "selected-message";
  state.messageDetailBody = "old detail";
  state.messageDetailError = "old error";
  state.messageDetailTruncated = true;

  JsonDocument permission;
  permission["type"] = "permission";
  permission["id"] = "approval-1";
  permission["text"] = "Approve action?";
  assert(protocol::applyJsonFrame(state, serialize(permission), 100));
  assert(!state.messageDetailPending);
  assert(state.messageDetailRequestedId.isEmpty());
  assert(state.messageDetailBody.isEmpty());
  assert(state.messageDetailError.isEmpty());
  assert(!state.messageDetailTruncated);
  assert(!protocol::applyJsonFrame(
      state, makeMessageDetail("selected-message", "late full response", false), 200));
}
}  // namespace

int main() {
  testMarkdownAndCompactPreviewAreIndependent();
  testMarkdownCapDoesNotSplitUtf8();
  testHostTruncatedFlagSurvivesParsingAndEmptyFieldClears();
  testEntryIdsStayAlignedAndClearOnMissingOrInvalidIds();
  testHistoryIdsAreBoundedAndClearWithEachWindow();
  testMessageDetailRequiresCurrentOpenPendingRequest();
  testMessageDetailUtf8CapAndStrictTruncatedBoolean();
  testMessageDetailNotFoundAndMalformedResponses();
  testTransientPageCancelsDetailAndIgnoresLateReply();
  testPermissionTakeoverCancelsMessageDetailRequest();
}
