#include "ui_state.h"

#include <ArduinoJson.h>
#include <cstring>

namespace protocol {
namespace {
constexpr size_t kMaxAgentMarkdownBytes = 3072;

String textOr(JsonVariantConst value, const char* fallback = "") {
  return value.is<const char*>() ? String(value.as<const char*>()) : String(fallback);
}

String boundedMarkdown(JsonVariantConst value, bool& truncated) {
  truncated = false;
  if (!value.is<const char*>()) return String();
  const char* raw = value.as<const char*>();
  if (!raw) return String();
  size_t length = strlen(raw);
  if (length > kMaxAgentMarkdownBytes) {
    truncated = true;
    length = kMaxAgentMarkdownBytes;
    // Do not leave a UTF-8 continuation byte at the beginning of the removed
    // tail. JSON byte capacity is fixed, but text length is counted in bytes.
    while (length && (static_cast<uint8_t>(raw[length]) & 0xc0) == 0x80) --length;
  }
  String result;
  result.reserve(length);
  for (size_t i = 0; i < length; ++i) result += raw[i];
  return result;
}

void markLive(UiState& s, uint32_t nowMs) {
  s.connected = true;
  s.lastSeenMs = nowMs;
  s.dirty = true;
}

}  // namespace

bool applyJsonFrame(UiState& s, const String& line, uint32_t nowMs) {
  // The serial reader caps each line at 4096 bytes; ArduinoJson 7 allocates
  // only the document storage needed for that bounded frame. This keeps long
  // approval details readable without placing a large static document on the
  // loop task's stack.
  JsonDocument doc;
  if (deserializeJson(doc, line.c_str())) return false;
  JsonVariantConst root = doc.as<JsonVariantConst>();
  const char* type = root["type"] | "";
  const char* cmd = root["cmd"] | "";

  if (!strcmp(cmd, "ping") || !strcmp(cmd, "clear")) return true;
  if (!strcmp(type, "ack")) {
    s.message = textOr(root["msg"], s.message.c_str());
    markLive(s, nowMs);
    return true;
  }
  if (!strcmp(type, "event")) {
    s.message = textOr(root["msg"], s.message.c_str());
    if (!strcmp(root["event"] | "", "message")) {
      s.toast = s.message;
      s.toastUntilMs = nowMs + 4000;
    }
    markLive(s, nowMs);
    return true;
  }
  if (!strcmp(type, "notify")) {
    s.message = textOr(root["msg"]);
    s.toast = s.message;
    uint32_t seconds = root["secs"] | 8U;
    s.toastUntilMs = nowMs + seconds * 1000U;
    markLive(s, nowMs);
    return true;
  }
  if (!strcmp(type, "say")) {
    markLive(s, nowMs);
    return true;
  }
  if (!strcmp(type, "config")) return true;
  if (!strcmp(type, "deck")) {
    s.deckCount = 0;
    JsonArrayConst buttons = root["buttons"].as<JsonArrayConst>();
    for (JsonVariantConst button : buttons) {
      if (s.deckCount == 6) break;
      DeckAction& action = s.deck[s.deckCount++];
      action.label = textOr(button["label"], "BTN");
      const char* color = button["color"] | "green";
      action.color = !strcmp(color, "amber") ? 1 : !strcmp(color, "red") ? 2 : !strcmp(color, "cyan") ? 3 : 0;
      action.confirm = button["confirm"] | false;
    }
    s.armedDeck = -1;
    markLive(s, nowMs);
    return true;
  }
  if (!strcmp(type, "msgs")) {
    s.historyCount = 0;
    s.historyOffset = root["off"] | 0;
    s.historyTotal = root["total"] | 0;
    for (JsonVariantConst value : root["lines"].as<JsonArrayConst>()) {
      if (s.historyCount == 5) break;
      s.history[s.historyCount++] = textOr(value);
    }
    markLive(s, nowMs);
    return true;
  }
  if (!strcmp(type, "page")) {
    int slot = root["slot"] | 0;
    HostPage page;
    page.title = textOr(root["title"], "HOST");
    for (uint8_t i = 0; i < 3; ++i) page.lines[i] = textOr(root["lines"][i]);
    page.set = true;
    if (slot == 9) {
      if (!s.modalActive) s.modalReturn = s.page;
      s.modal = page;
      s.modalBody = "";
      s.modalActive = true;
    } else {
      if (slot < 0 || slot > 2) slot = 0;
      s.hostPages[slot] = page;
    }
    markLive(s, nowMs);
    return true;
  }
  if (!strcmp(type, "permission")) {
    s.approval.active = true;
    s.approval.id = textOr(root["id"]);
    s.approval.text = textOr(root["text"], "Hermes needs approval");
    s.approval.detail = textOr(root["detail"]);
    s.approval.choiceCount = 0;
    for (JsonVariantConst value : root["choices"].as<JsonArrayConst>()) {
      if (s.approval.choiceCount == 3) break;
      s.approval.choices[s.approval.choiceCount++] = textOr(value);
    }
    if (!s.approval.choiceCount) {
      s.approval.choices[0] = "once";
      s.approval.choices[1] = "deny";
      s.approval.choiceCount = 2;
    }
    s.page = Page::Operations;
    s.message = s.approval.text;
    markLive(s, nowMs);
    return true;
  }

  // The legacy state frame is intentionally recognized by its known fields;
  // arbitrary JSON must not refresh host liveness.
  const bool stateFrame = root["total"].is<int>() || root["running"].is<int>() ||
                          root["waiting"].is<int>() || root["job_state"].is<const char*>() ||
                          root["entries"].is<JsonArrayConst>();
  if (!stateFrame) return false;
  s.total = root["total"] | s.total;
  s.running = root["running"] | s.running;
  s.waiting = root["waiting"] | s.waiting;
  if (!s.waiting) s.approval.active = false;
  s.tokensToday = root["tokens_today"] | s.tokensToday;
  s.toolsToday = root["tools_today"] | s.toolsToday;
  s.jobState = textOr(root["job_state"], s.jobState.c_str());
  s.jobLabel = textOr(root["job_label"], s.jobLabel.c_str());
  s.runningDeck = root["job_index"] | s.runningDeck;
  s.message = textOr(root["msg"], s.message.c_str());
  s.entryCount = 0;
  for (JsonVariantConst value : root["entries"].as<JsonArrayConst>()) {
    if (s.entryCount == 5) break;
    s.entries[s.entryCount++] = textOr(value);
  }
  if (root["msg_markdown"].is<const char*>()) {
    bool locallyTruncated = false;
    s.agentResponseMarkdown = boundedMarkdown(root["msg_markdown"], locallyTruncated);
    s.agentResponseMarkdownTruncated =
        locallyTruncated || (root["msg_markdown_truncated"] | false);
  }
  markLive(s, nowMs);
  return true;
}

}  // namespace protocol
