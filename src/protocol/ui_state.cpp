#include "ui_state.h"

#include <ArduinoJson.h>
#include <cstring>

namespace protocol {
namespace {
constexpr size_t kMaxAgentMarkdownBytes = 3072;
constexpr size_t kMaxMessageIdBytes = 128;
constexpr size_t kMaxMessageDetailBytes = 16 * 1024;

String textOr(JsonVariantConst value, const char* fallback = "") {
  return value.is<const char*>() ? String(value.as<const char*>()) : String(fallback);
}

String boundedUtf8(JsonVariantConst value, size_t maxBytes, bool& truncated) {
  truncated = false;
  if (!value.is<const char*>()) return String();
  const char* raw = value.as<const char*>();
  if (!raw) return String();
  size_t length = strlen(raw);
  if (length > maxBytes) {
    truncated = true;
    length = maxBytes;
    // Do not leave a UTF-8 continuation byte at the beginning of the removed
    // tail. JSON byte capacity is fixed, but text length is counted in bytes.
    while (length && (static_cast<uint8_t>(raw[length]) & 0xc0) == 0x80) --length;
  }
  String result;
  result.reserve(length);
  for (size_t i = 0; i < length; ++i) result += raw[i];
  return result;
}

String messageId(JsonVariantConst value) {
  if (!value.is<const char*>()) return String();
  const char* raw = value.as<const char*>();
  if (!raw || !raw[0] || strlen(raw) > kMaxMessageIdBytes) return String();
  bool ignored = false;
  return boundedUtf8(value, kMaxMessageIdBytes, ignored);
}

String boundedText(JsonVariantConst value, size_t maxBytes) {
  bool ignored = false;
  return boundedUtf8(value, maxBytes, ignored);
}

void readIds(JsonVariantConst field, String (&ids)[5], uint8_t count) {
  for (String& id : ids) id = "";
  if (!field.is<JsonArrayConst>()) return;
  JsonArrayConst values = field.as<JsonArrayConst>();
  for (uint8_t i = 0; i < count && i < 5 && i < values.size(); ++i) {
    ids[i] = messageId(values[i]);
  }
}

void clearMessageDetailRequest(UiState& s) {
  s.messageDetailRequestedId = "";
  s.messageDetailBody = "";
  s.messageDetailRole = "";
  s.messageDetailError = "";
  s.messageDetailPending = false;
  s.messageDetailTruncated = false;
  s.messageDetailHasMore = false;
  s.messageDetailRequestedOffset = 0;
  s.messageDetailNextOffset = 0;
  s.messageDetailTotalBytes = 0;
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
    readIds(root["ids"], s.historyIds, s.historyCount);
    markLive(s, nowMs);
    return true;
  }
  if (!strcmp(type, "msg")) {
    // Late replies from a prior selection, replies after close, and replies
    // arriving after a newer request must not replace the visible detail.
    if (!s.modalActive || !s.messageDetailPending || s.messageDetailRequestedId.isEmpty() ||
        !root["id"].is<const char*>() ||
        !root["id"].as<const char*>()[0] ||
        strcmp(root["id"].as<const char*>(), s.messageDetailRequestedId.c_str())) return false;

    const bool hasBody = root["body"].is<const char*>();
    const bool hasError = root["error"].is<const char*>() && root["error"].as<const char*>()[0];
    if (!hasBody && !hasError) return false;
    const bool hasPagingFields = !root["body_offset"].isNull() ||
                                 !root["body_total"].isNull() ||
                                 !root["has_more"].isNull();
    if (hasPagingFields && (!root["body_offset"].is<uint32_t>() ||
                            !root["body_total"].is<uint32_t>() ||
                            !root["has_more"].is<bool>() ||
                            root["body_total"].as<uint32_t>() > kMaxMessageDetailBytes))
      return false;
    if (hasError) {
      if (hasPagingFields &&
          root["body_offset"].as<uint32_t>() != s.messageDetailRequestedOffset) return false;
      s.messageDetailError = boundedText(root["error"], 160);
      s.messageDetailPending = false;
      s.messageDetailHasMore = false;
    } else {
      const bool paged = hasPagingFields;
      const uint32_t bodyOffset = paged ? root["body_offset"].as<uint32_t>() : 0;
      bool locallyTruncated = false;
      String body = boundedUtf8(root["body"], kMaxMessageDetailBytes, locallyTruncated);
      if (paged && (bodyOffset != s.messageDetailRequestedOffset ||
                    bodyOffset != s.messageDetailBody.length() ||
                    body.length() > kMaxMessageDetailBytes - s.messageDetailBody.length())) {
        return false;
      }
      if (paged) {
        const uint32_t total = root["body_total"].as<uint32_t>();
        const bool hasMore = root["has_more"].is<bool>() && root["has_more"].as<bool>();
        const uint32_t nextOffset = bodyOffset + body.length();
        if (nextOffset < bodyOffset || (hasMore && (body.isEmpty() || nextOffset >= total)) ||
            (bodyOffset && s.messageDetailTotalBytes != total) ||
            (!hasMore && total && nextOffset != total)) return false;
        s.messageDetailBody += body;
        s.messageDetailNextOffset = nextOffset;
        s.messageDetailTotalBytes = total;
        s.messageDetailHasMore = hasMore && nextOffset < kMaxMessageDetailBytes;
        const bool retainedTruncated = root["retained_truncated"].is<bool>()
            ? root["retained_truncated"].as<bool>()
            : (!root["has_more"].is<bool>() &&
               root["truncated"].is<bool>() && root["truncated"].as<bool>());
        s.messageDetailTruncated = locallyTruncated || retainedTruncated ||
                                   (nextOffset >= kMaxMessageDetailBytes && hasMore);
        s.messageDetailRole = boundedText(root["role"], 32);
        s.messageDetailError = "";
      } else {
        s.messageDetailBody = body;
        s.messageDetailHasMore = false;
        s.messageDetailNextOffset = body.length();
        s.messageDetailTotalBytes = body.length();
        s.messageDetailRole = boundedText(root["role"], 32);
        s.messageDetailError = "";
        s.messageDetailTruncated = locallyTruncated ||
            (root["truncated"].is<bool>() && root["truncated"].as<bool>());
      }
      s.messageDetailPending = false;
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
      clearMessageDetailRequest(s);
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
    clearMessageDetailRequest(s);
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
  for (String& id : s.entryIds) id = "";
  for (JsonVariantConst value : root["entries"].as<JsonArrayConst>()) {
    if (s.entryCount == 5) break;
    s.entries[s.entryCount++] = textOr(value);
  }
  readIds(root["entry_ids"], s.entryIds, s.entryCount);
  if (root["msg_markdown"].is<const char*>()) {
    bool locallyTruncated = false;
    s.agentResponseMarkdown = boundedUtf8(root["msg_markdown"], kMaxAgentMarkdownBytes, locallyTruncated);
    s.agentResponseMarkdownTruncated =
        locallyTruncated || (root["msg_markdown_truncated"] | false);
  }
  markLive(s, nowMs);
  return true;
}

}  // namespace protocol
