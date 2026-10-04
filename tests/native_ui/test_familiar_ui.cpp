#include <cassert>
#include <iostream>
#include <string>
#include <vector>

#include "ui/familiar_ui.h"
#include "ui/emoji_text.h"

namespace {
struct Capture {
  std::vector<std::string> lines;
};

void capture(void* context, const String& line) {
  static_cast<Capture*>(context)->lines.push_back(line.str());
}

void tap(ui::FamiliarUi& ui, int x, int y, uint32_t at) {
  ui.touchGesture(x, y, x + 1, y + 1, 80, at);
}

bool has(const Capture& capture, const std::string& fragment) {
  for (const auto& line : capture.lines) if (line.find(fragment) != std::string::npos) return true;
  return false;
}

bool textHas(const lgfx::LGFX_Sprite& sprite, const std::string& fragment) {
  for (const auto& call : sprite.calls) if (call.text.find(fragment) != std::string::npos) return true;
  return false;
}

bool hasTextWithFont(const lgfx::LGFX_Sprite& sprite, const std::string& text,
                     const lgfx::IFont* font) {
  for (const auto& call : sprite.calls)
    if (call.text == text && call.font == font) return true;
  return false;
}

bool hasTextCall(const lgfx::LGFX_Sprite& sprite, const std::string& text,
                 int x, int y, float size) {
  for (const auto& call : sprite.calls) {
    if (call.text == text && call.x == x && call.y == y && call.size == size) return true;
  }
  return false;
}

bool isValidUtf8(const std::string& text) {
  for (size_t i = 0; i < text.size();) {
    const uint8_t lead = static_cast<uint8_t>(text[i]);
    size_t length = lead < 0x80 ? 1 : (lead & 0xe0) == 0xc0 ? 2 :
                    (lead & 0xf0) == 0xe0 ? 3 : (lead & 0xf8) == 0xf0 ? 4 : 0;
    if (!length || i + length > text.size()) return false;
    for (size_t byte = 1; byte < length; ++byte)
      if ((static_cast<uint8_t>(text[i + byte]) & 0xc0) != 0x80) return false;
    i += length;
  }
  return true;
}

void assertTextCallsFit(const lgfx::LGFX_Sprite& sprite) {
  for (const auto& call : sprite.calls) {
    assert(isValidUtf8(call.text));
    assert(call.x >= 0 && call.width >= 0 && call.x + call.width <= ui::FamiliarUi::kWidth);
    const int height = static_cast<int>(12 * call.size);
    assert(call.y >= 0 && call.y + height <= ui::FamiliarUi::kHeight);
  }
}

size_t count(const Capture& capture, const std::string& fragment) {
  size_t matches = 0;
  for (const auto& line : capture.lines) matches += line.find(fragment) != std::string::npos;
  return matches;
}

void testTabBoundariesAndDeckGaps() {
  lgfx::LGFX_Sprite sprite;
  protocol::UiState state;
  Capture sent;
  ui::FamiliarUi view(sprite, state, capture, &sent);
  for (uint8_t i = 0; i < 8; ++i) {
    const ui::Rect rect = ui::FamiliarUi::tabRect(i);
    assert(rect.x >= 0 && rect.w > 0 && rect.x + rect.w <= ui::FamiliarUi::kWidth);
    tap(view, rect.x, 10, 100 + i);
    assert(state.page == static_cast<protocol::Page>(i));
    tap(view, rect.x + rect.w - 1, 10, 200 + i);
    assert(state.page == static_cast<protocol::Page>(i));
    if (i < 7) {
      tap(view, rect.x + rect.w, 10, 250 + i);
      assert(state.page == static_cast<protocol::Page>(i + 1));
    }
  }
  const ui::Rect last = ui::FamiliarUi::tabRect(7);
  assert(last.x + last.w == ui::FamiliarUi::kWidth);
  tap(view, ui::FamiliarUi::kWidth, 10, 300);
  assert(state.page == protocol::Page::Settings);

  state.page = protocol::Page::Operations;
  state.deckCount = 6;
  for (uint8_t i = 0; i < state.deckCount; ++i) state.deck[i].label = String("action");
  const ui::Rect first = ui::FamiliarUi::deckRect(0);
  tap(view, first.x + first.w - 1, first.y + 4, 400);
  assert(has(sent, "\"cmd\":\"deck\""));
  const size_t deckCommands = count(sent, "\"cmd\":\"deck\"");
  tap(view, first.x + first.w, first.y + 4, 401);  // the horizontal gap
  tap(view, first.x + 3, first.y + first.h, 402);  // the vertical gap
  assert(count(sent, "\"cmd\":\"deck\"") == deckCommands);
}

void testMessageCardGeometryTapAndModalReturn() {
  lgfx::LGFX_Sprite sprite;
  protocol::UiState state;
  Capture sent;
  ui::FamiliarUi view(sprite, state, capture, &sent);
  state.page = protocol::Page::Messages;
  state.entryCount = 3;
  state.entries[0] = "14:10 w: first user preview";
  state.entries[1] = "14:11 a: second assistant preview";
  state.entries[2] = "14:12 u: third user preview";
  state.entryIds[0] = "first-user";
  state.entryIds[1] = "second-assistant";
  state.entryIds[2] = "third-user";
  state.agentResponseMarkdown = "**unrelated latest response**";
  const ui::Rect first = ui::FamiliarUi::messageCardRect(0);
  const ui::Rect second = ui::FamiliarUi::messageCardRect(1);
  assert(first.x == 8 && first.w == 624 && first.h >= 54 && first.h <= 56);
  assert(second.x == first.x && second.w == first.w && second.h == first.h);
  assert(first.y >= 48 && first.y + first.h < second.y);
  assert(second.y + second.h <= ui::FamiliarUi::kHeight);
  assert(ui::FamiliarUi::messageCardRect(2).w == 0);

  view.render(10);
  assert(textHas(sprite, "WORKER"));
  assert(textHas(sprite, "ASSISTANT"));
  assert(textHas(sprite, "OPEN >"));
  assert(textHas(sprite, "first user preview"));
  assert(textHas(sprite, "second assistant preview"));
  assert(hasTextCall(sprite, "WORKER  14:10", first.x + 8, first.y + 4, 1.0f));
  assert(hasTextCall(sprite, "ASSISTANT  14:11", second.x + 8, second.y + 4, 1.0f));
  assert(hasTextCall(sprite, "OPEN >", first.x + first.w - 72, first.y + 4, 1.0f));
  assertTextCallsFit(sprite);

  tap(view, 300, first.y + first.h + 1, 11);
  assert(!state.modalActive); // Card gap stays inert.
  tap(view, second.x + second.w / 2, second.y + second.h / 2, 12);
  assert(state.modalActive && state.modalReturn == protocol::Page::Messages);
  assert(state.modalBody == "second assistant preview");
  assert(!textHas(sprite, "unrelated latest response"));
  tap(view, 300, 90, 13);
  assert(!state.modalActive && state.page == protocol::Page::Messages);
  view.render(14);
  assert(textHas(sprite, "first user preview"));
  assert(textHas(sprite, "second assistant preview")); // Second-card open kept the viewport.
  state.entries[0] = "14:13 a: new live entry";
  state.entries[1] = "14:10 u: first user preview";
  state.entries[2] = "14:11 a: second assistant preview";
  state.entryIds[0] = "new-live";
  state.entryIds[1] = "first-user";
  state.entryIds[2] = "second-assistant";
  view.render(15);
  assert(textHas(sprite, "first user preview"));
  assert(textHas(sprite, "second assistant preview"));
  view.touchGesture(320, 80, 250, 80, 200, 16);
  assert(state.page == protocol::Page::Operations); // Horizontal tabs still navigate.
}

void testMessageBrowsingLocalHistoryAndDetailRequest() {
  lgfx::LGFX_Sprite sprite;
  protocol::UiState state;
  Capture sent;
  ui::FamiliarUi view(sprite, state, capture, &sent);
  state.page = protocol::Page::Messages;
  state.entryCount = 5;
  for (uint8_t i = 0; i < 5; ++i) {
    state.entries[i] = String("14:0") + String(i) + " a: local entry " + String(i);
    state.entryIds[i] = String("local-") + String(i);
  }
  view.render(99);
  assert(textHas(sprite, "local entry 0") && textHas(sprite, "local entry 1"));
  for (uint8_t i = 0; i < 4; ++i) {
    view.touchGesture(300, 120, 300, 70, 200, 100 + i);
    assert(!has(sent, "\"cmd\":\"msgs\""));
    assert(!state.modalActive); // A vertical browse gesture never acts as a tap.
    view.render(110 + i);
    assert(textHas(sprite, "local entry " + std::to_string(i + 1)));
  }
  view.touchGesture(300, 120, 300, 70, 200, 120);
  assert(has(sent, "\"cmd\":\"msgs\""));
  assert(has(sent, "\"off\":5"));
  state.historyOffset = 5;
  state.historyCount = 5;
  state.historyTotal = 15;
  for (uint8_t i = 0; i < 5; ++i) {
    state.history[i] = String("14:1") + String(i) + " u: history entry " + String(i);
    state.historyIds[i] = String("history-") + String(i);
  }
  view.render(121);
  assert(textHas(sprite, "history entry 0"));
  for (uint8_t i = 0; i < 4; ++i) {
    view.touchGesture(300, 120, 300, 70, 200, 130 + i);
    view.render(135 + i);
    assert(textHas(sprite, "history entry " + std::to_string(i + 1)));
  }
  view.touchGesture(300, 120, 300, 70, 200, 140);
  assert(has(sent, "\"off\":10"));
  state.historyOffset = 10;
  view.render(141);
  state.historyTotal = 12;
  state.historyCount = 2;
  state.history[0] = "14:20 a: final older 0";
  state.history[1] = "14:21 a: final older 1";
  view.render(142);
  view.touchGesture(300, 70, 300, 120, 200, 150);
  assert(state.historyOffset == 10); // The final batch cannot request older entries.
  view.touchGesture(300, 70, 300, 120, 200, 151);
  view.touchGesture(300, 70, 300, 120, 200, 152);

  // Move newer across batches, then return from offset five to the live tail.
  assert(has(sent, "\"off\":5"));
  state.historyOffset = 5;
  state.historyCount = 5;
  view.render(153);
  for (uint8_t i = 0; i < 4; ++i)
    view.touchGesture(300, 70, 300, 120, 200, 160 + i);
  view.touchGesture(300, 70, 300, 120, 200, 170);
  assert(state.historyOffset == 0); // Newer reaches the live tail locally.
  for (uint8_t i = 0; i < 4; ++i)
    view.touchGesture(300, 70, 300, 120, 200, 180 + i);
  view.touchGesture(300, 70, 300, 120, 200, 190);
  assert(state.historyOffset == 0 && !has(sent, "\"off\":0"));

  state.entryIds[0] = "detail \"id\"";
  const String previewBefore = state.entries[0];
  view.render(200);
  const ui::Rect first = ui::FamiliarUi::messageCardRect(0);
  tap(view, first.x + 10, first.y + 10, 201);
  assert(state.modalActive && state.messageDetailPending);
  assert(state.messageDetailRequestedId == "detail \"id\"");
  assert(has(sent, "\"cmd\":\"msg\""));
  assert(has(sent, "detail \\\"id\\\""));
  view.render(202);
  assert(textHas(sprite, "loading full message"));
  tap(view, 400, 100, 203);
  assert(!state.messageDetailPending && state.messageDetailRequestedId.length() == 0);
  assert(state.entries[0] == previewBefore);
}

void testMessageIdentitySurvivesReorderAndSamePreview() {
  lgfx::LGFX_Sprite sprite;
  protocol::UiState state;
  Capture sent;
  ui::FamiliarUi view(sprite, state, capture, &sent);
  state.page = protocol::Page::Messages;
  state.entryCount = 3;
  for (uint8_t i = 0; i < 3; ++i)
    state.entries[i] = "14:30 a: repeated preview";
  state.entryIds[0] = "message-a";
  state.entryIds[1] = "message-b";
  state.entryIds[2] = "message-c";
  view.touchGesture(300, 120, 300, 70, 200, 10);

  // A live state update can reorder identical previews; keep message-b selected.
  state.entryIds[0] = "message-b";
  state.entryIds[1] = "message-a";
  view.render(11);
  const ui::Rect first = ui::FamiliarUi::messageCardRect(0);
  tap(view, first.x + 10, first.y + 10, 12);
  assert(state.messageDetailRequestedId == "message-b");
  assert(has(sent, "message-b"));
  state.messageDetailBody = "Full text for message-b";
  state.messageDetailRole = "assistant";
  state.messageDetailPending = false;
  view.render(13);
  assert(textHas(sprite, "Full text for message-b"));
  tap(view, 400, 100, 14);

  // Keep the cursor bound to the same ID after dismissal and another reorder.
  state.entryIds[0] = "message-c";
  state.entryIds[1] = "message-b";
  view.render(15);
  assert(textHas(sprite, "repeated preview"));
  tap(view, first.x + 10, first.y + 10, 16);
  assert(state.messageDetailRequestedId == "message-b");
  state.messageDetailBody = "partial retained text";
  view.render(8016); // A timeout leaves already received detail readable.
  assert(!state.messageDetailPending);
  assert(state.messageDetailError == "timeout");
  assert(textHas(sprite, "partial retained text"));
  assert(textHas(sprite, "message incomplete"));
}

void testConfirmExpiry() {
  lgfx::LGFX_Sprite sprite;
  protocol::UiState state;
  Capture sent;
  ui::FamiliarUi view(sprite, state, capture, &sent);
  state.page = protocol::Page::Operations;
  state.deckCount = 1;
  state.deck[0].label = "dangerous";
  state.deck[0].confirm = true;
  const ui::Rect button = ui::FamiliarUi::deckRect(0);
  tap(view, button.x + 10, button.y + 10, 1000);
  assert(state.armedDeck == 0 && sent.lines.size() == 1);
  view.tick(3500);
  assert(state.armedDeck == -1);
  tap(view, button.x + 10, button.y + 10, 3501);
  assert(state.armedDeck == 0 && sent.lines.size() == 2);
  tap(view, button.x + 10, button.y + 10, 3502);
  assert(state.armedDeck == -1 && has(sent, "\"cmd\":\"deck\""));

  state.armedDeck = -1;
  sent.lines.clear();
  constexpr uint32_t nearWrap = 0xfffffff0u;
  tap(view, button.x + 10, button.y + 10, nearWrap);
  assert(state.armedDeck == 0 && state.armedUntilMs == 2484u);
  view.tick(2483u);
  assert(state.armedDeck == 0);
  view.tick(2484u);
  assert(state.armedDeck == -1);
}

void setupApproval(protocol::UiState& state) {
  state.page = protocol::Page::Operations;
  state.modalActive = false;
  state.modalBody = "";
  state.connected = true;
  state.lastSeenMs = 5000;
  state.approval.active = true;
  state.approval.id = "approval-7";
  state.approval.text = "Run command?";
}

void testApprovalRequiresLiveCurrentRequestAndExactButtons() {
  lgfx::LGFX_Sprite sprite;
  protocol::UiState state;
  Capture sent;
  ui::FamiliarUi view(sprite, state, capture, &sent);
  setupApproval(state);
  const ui::Rect allow = ui::FamiliarUi::allowRect();
  const ui::Rect deny = ui::FamiliarUi::denyRect();
  const ui::Rect detail = ui::FamiliarUi::approvalTextRect();

  tap(view, allow.x + allow.w - 1, allow.y + 5, 5100);
  assert(has(sent, "\"decision\":\"once\""));
  assert(has(sent, "\"id\":\"approval-7\""));
  assert(!state.approval.active);

  sent.lines.clear();
  setupApproval(state);
  tap(view, 320, 145, 5100);  // gap between ALLOW and DENY
  assert(state.approval.active && !has(sent, "\"cmd\":\"permission\""));
  tap(view, deny.x, deny.y, 5100);
  assert(has(sent, "\"decision\":\"deny\""));

  sent.lines.clear();
  setupApproval(state);
  tap(view, detail.x + 5, detail.y + 5, 5100);
  assert(state.approval.active && state.modalActive);
  assert(!has(sent, "\"cmd\":\"permission\""));

  const auto blocked = [&](bool connected, uint32_t lastSeen, const char* id) {
    sent.lines.clear();
    setupApproval(state);
    state.connected = connected;
    state.lastSeenMs = lastSeen;
    state.approval.id = id;
    tap(view, allow.x + 10, allow.y + 8, 40000);
    assert(state.approval.active);
    assert(!has(sent, "\"cmd\":\"permission\""));
  };
  blocked(false, 39900, "approval-7");
  blocked(true, 9000, "approval-7");
  blocked(true, 10000, "approval-7");  // exactly 30,000 ms old
  blocked(true, 39900, "");
}

void testSwipeCannotResolveApprovalAndModalReturnsToOrigin() {
  lgfx::LGFX_Sprite sprite;
  protocol::UiState state;
  Capture sent;
  ui::FamiliarUi view(sprite, state, capture, &sent);
  setupApproval(state);
  view.touchGesture(120, 146, 190, 148, 100, 5100);
  assert(state.approval.active);
  assert(!has(sent, "\"cmd\":\"permission\""));

  state.page = protocol::Page::Fleet;
  state.hostPages[2].set = true;
  state.hostPages[2].title = "Fleet detail";
  state.hostPages[2].lines[0] = "An item with useful detail";
  tap(view, 30, 60, 5200);
  assert(state.modalActive && state.modalReturn == protocol::Page::Fleet);
  tap(view, 400, 100, 5201);
  assert(!state.modalActive && state.page == protocol::Page::Fleet);
}

void testUtf8TypographyMeasuredWrappingAcrossPages() {
  lgfx::LGFX_Sprite sprite;
  protocol::UiState state;
  Capture sent;
  ui::FamiliarUi view(sprite, state, capture, &sent);

  state.connected = true;
  state.lastSeenMs = 100;
  state.page = protocol::Page::Fleet;
  auto& fleet = state.hostPages[2];
  fleet.set = true;
  fleet.title = "Семейство — fleet";
  fleet.lines[0] = "Привет мир — кириллица читается без повреждения UTF-8";
  fleet.lines[1] = "Команда rm *.json сохраняет glob и пробелы";
  fleet.lines[2] = "Длинная строка содержит достаточно слов чтобы корректно переноситься по ширине и показывать многоточие если высоты строки недостаточно для всего текста";

  view.render(200);
  assert(sprite.selectedFont == &fonts::efontJA_12);
  assert(!sprite.wrapX && !sprite.wrapY);
  assert(textHas(sprite, "Привет"));
  assert(textHas(sprite, "*.json"));
  assert(textHas(sprite, "..."));
  for (const auto& call : sprite.calls) {
    assert(isValidUtf8(call.text));
    assert(call.x >= 0 && call.width >= 0 && call.x + call.width <= ui::FamiliarUi::kWidth);
    assert(call.size >= 0.8f);
  }

  // Populate every page with bounded synthetic Unicode test content. These
  // assertions cover recorded API calls and geometry, not physical glyph output.
  state.jobLabel = "Очень длинная подпись задачи для проверки переноса текста";
  state.message = "Задача обработана — длинная строка с кириллицей для проверки ширины шрифта и многоточия";
  state.toast = "Состояние обновлено — проверка сообщения";
  state.toastUntilMs = 5000;
  state.entryCount = 5;
  state.historyCount = 5;
  state.historyOffset = 5;
  state.historyTotal = 20;
  for (uint8_t i = 0; i < 5; ++i) {
    state.entries[i] = "Событие пользователя с длинным текстом для проверки ширины — запись " + String(i);
    state.history[i] = "Историческое сообщение на русском языке с проверкой переноса строки " + String(i);
  }
  state.hostPages[0].set = true;
  state.hostPages[0].title = "Расписание заданий";
  state.hostPages[0].lines[0] = "Задание работает успешно — описание с длинным текстом";
  state.hostPages[0].lines[1] = fleet.lines[2];
  state.hostPages[0].lines[2] = "Следующий запуск состоится позже";
  state.hostPages[1].set = true;
  state.hostPages[1].title = "Сетевая конфигурация";
  state.hostPages[1].lines[0] = "Wi-Fi подключён, адрес задан — длинное сообщение состояния";
  state.hostPages[1].lines[1] = "Сервер доступен по локальной сети";
  state.hostPages[1].lines[2] = "Синхронизация завершена успешно";
  state.sdStatus = "SD карта готова";
  state.touchStatus = "сенсор касания готов";
  state.batteryStatus = "питание 4.1 V";
  state.rtcStatus = "время синхронизировано";
  state.wifiStatus = "Wi-Fi подключён";
  state.networkStatus = "сеть работает";
  state.audioStatus = "аудио готово";
  state.motionStatus = "движение: 0.02, 0.01, 1.00 g";
  state.linkStatus = "USB READY";

  for (uint8_t page = 0; page < 8; ++page) {
    state.page = static_cast<protocol::Page>(page);
    view.render(300 + page);
    assert(sprite.selectedFont == &fonts::efontJA_12 && !sprite.wrapX);
    assertTextCallsFit(sprite);
    if (page == static_cast<uint8_t>(protocol::Page::Cron) ||
        page == static_cast<uint8_t>(protocol::Page::Network)) {
      assert(hasTextCall(sprite, "tap a line to read full text", 8, 159, 1.0f));
    }
  }

  state.page = protocol::Page::Messages;
  view.render(400);
  assert(textHas(sprite, "MESSAGES") || textHas(sprite, "HISTORY"));
  assertTextCallsFit(sprite);
  state.historyOffset = 0;
  view.render(401);
  assert(textHas(sprite, "MESSAGES"));
  assertTextCallsFit(sprite);

  state.page = protocol::Page::Operations;
  state.deckCount = 1;
  state.deck[0].label = "Очень длинная подпись для действия в колоде";
  state.jobState = "running";
  view.render(402);
  assertTextCallsFit(sprite);

  state.approval.active = true;
  state.approval.id = "approval-test";
  state.approval.text = "rm *.json --flag C:\\temp\\queue";
  state.approval.detail = "Проверка текста подтверждения и подробного описания";
  view.render(403);
  assert(textHas(sprite, "*.json"));
  assert(textHas(sprite, "--flag"));
  assertTextCallsFit(sprite);
  const ui::Rect detail = ui::FamiliarUi::approvalTextRect();
  tap(view, detail.x + 10, detail.y + 10, 404);
  view.render(405);
  assert(state.modalActive);
  assert(textHas(sprite, "swipe up/down to read"));
  assert(textHas(sprite, "*.json"));
  assertTextCallsFit(sprite);
}

void testEmojiClustersAndCodepointSafeRendering() {
  const std::string sample = "x👨‍👩‍👧‍👦👍🏽🇷🇺1️⃣";
  size_t offset = 0;
  ui::emoji_text::Token token;
  assert(ui::emoji_text::nextToken(sample, offset, token));
  assert(token.kind == ui::emoji_text::TokenKind::Text);
  assert(sample.substr(token.begin, token.end - token.begin) == "x");
  assert(ui::emoji_text::nextToken(sample, offset, token));
  assert(token.kind == ui::emoji_text::TokenKind::Emoji);
  assert(sample.substr(token.begin, token.end - token.begin) == "👨‍👩‍👧‍👦");
  assert(ui::emoji_text::nextToken(sample, offset, token));
  assert(token.kind == ui::emoji_text::TokenKind::Emoji);
  assert(sample.substr(token.begin, token.end - token.begin) == "👍🏽");
  assert(ui::emoji_text::nextToken(sample, offset, token));
  assert(token.kind == ui::emoji_text::TokenKind::Emoji);
  assert(sample.substr(token.begin, token.end - token.begin) == "🇷🇺");
  assert(ui::emoji_text::nextToken(sample, offset, token));
  assert(token.kind == ui::emoji_text::TokenKind::Emoji);
  assert(sample.substr(token.begin, token.end - token.begin) == "1️⃣");
  assert(!ui::emoji_text::nextToken(sample, offset, token));

  lgfx::LGFX_Sprite metricsSprite;
  metricsSprite.setFont(&fonts::efontJA_12);
  assert(ui::emoji_text::measure(metricsSprite, "👨‍👩‍👧‍👦") == 13);

  const auto assertTokens = [](const std::string& value,
                               const std::vector<std::string>& expected) {
    size_t at = 0;
    size_t index = 0;
    ui::emoji_text::Token part;
    while (ui::emoji_text::nextToken(value, at, part)) {
      assert(index < expected.size());
      assert(value.substr(part.begin, part.end - part.begin) == expected[index++]);
    }
    assert(index == expected.size());
  };
  assertTokens("1😀", {"1", "😀"});
  assertTokens("123 ABC", {"1", "2", "3", " ", "A", "B", "C"});
  assertTokens("#heading", {"#", "h", "e", "a", "d", "i", "n", "g"});
  assertTokens("* x", {"*", " ", "x"});
  assertTokens("1️😄", {"1️", "😄"});
  assertTokens("⭐", {"⭐"});
  assertTokens("↔️", {"↔️"});
  assertTokens("↩️", {"↩️"});
  assertTokens("▶️", {"▶️"});
  assertTokens("🔲", {"🔲"});
  assertTokens("◻️", {"◻️"});
  assertTokens("🙂‍↔️", {"🙂‍↔️"});
  assertTokens("⭐︎", {"⭐︎"});
  assertTokens("🙂‍A", {"🙂‍", "A"});
  const std::string incompleteTagFlag = "🏴\U000E0067";
  assertTokens(incompleteTagFlag, {incompleteTagFlag});
  offset = 0;
  assert(ui::emoji_text::nextToken(incompleteTagFlag, offset, token));
  assert(token.kind == ui::emoji_text::TokenKind::Text);
  assertTokens("\xF0\x80\x80\x80😀", {"\xF0", "\x80", "\x80", "\x80", "😀"});
  assertTokens("\xED\xA0\x80😀", {"\xED", "\xA0", "\x80", "😀"});
  assertTokens("\xF4\x90\x80\x80😀", {"\xF4", "\x90", "\x80", "\x80", "😀"});

  const std::string nerd = "x\U000F0001у";
  offset = 0;
  assert(ui::emoji_text::nextToken(nerd, offset, token));
  assert(token.kind == ui::emoji_text::TokenKind::Text);
  assert(ui::emoji_text::nextToken(nerd, offset, token));
  assert(token.kind == ui::emoji_text::TokenKind::NerdIcon);
  assert(token.codepoint == 0xf0001);
  assert(ui::emoji_text::nextToken(nerd, offset, token));
  assert(token.kind == ui::emoji_text::TokenKind::Text);
  assert(!ui::emoji_text::nextToken(nerd, offset, token));

  offset = 0;
  const std::string textPresentation = "⭐︎";
  assert(ui::emoji_text::nextToken(textPresentation, offset, token));
  assert(token.kind == ui::emoji_text::TokenKind::Text);

  lgfx::LGFX_Sprite sprite;
  sprite.setFont(&fonts::efontJA_12);
  ui::emoji_text::draw(sprite, sample, 10, 20, 0xffff, 0x0000);
  assert(!sprite.imageCalls.empty());
  assert(textHas(sprite, "x"));
  assert(!textHas(sprite, "👨"));  // bitmap clusters never reach the font printer
  for (const auto& call : sprite.imageCalls) {
    assert(call.width > 0 && call.height == 1);
    assert(call.x >= 0 && call.x + call.width <= ui::FamiliarUi::kWidth);
    assert(call.y >= 0 && call.y + call.height <= ui::FamiliarUi::kHeight);
  }

  sprite.fillScreen(0);
  ui::emoji_text::draw(sprite, nerd, 10, 40, 0x07e0, 0x0000);
  assert(!sprite.imageCalls.empty());
  assert(textHas(sprite, "x") && textHas(sprite, "у"));

  sprite.fillScreen(0);
  ui::emoji_text::draw(sprite, "😄", 10, 10, 0xffff, 0x0000, 3.0f);
  assert(!sprite.imageCalls.empty());
  for (const auto& call : sprite.imageCalls) assert(call.width <= 255);
  sprite.fillScreen(0);
  ui::emoji_text::draw(sprite, "😄", 10, 10, 0xffff, 0x0000, 30.0f);
  assert(!sprite.imageCalls.empty());
  for (const auto& call : sprite.imageCalls) assert(call.width <= 255);
}

void testEmojiRenderingInPlainAndStyledMarkdownText() {
  lgfx::LGFX_Sprite sprite;
  protocol::UiState state;
  Capture sent;
  ui::FamiliarUi view(sprite, state, capture, &sent);
  state.page = protocol::Page::Fleet;
  state.hostPages[2].set = true;
  state.hostPages[2].title = "FLEET";
  state.hostPages[2].lines[0] = "Привет 👩🏽‍🚀 🇷🇺 1️⃣";
  view.render(200);
  assert(!sprite.imageCalls.empty());
  assert(textHas(sprite, "Привет"));
  assertTextCallsFit(sprite);

  state.page = protocol::Page::Face;
  state.agentResponseMarkdown = "**bold 😄**\n\n`code 👩‍💻` 🇷🇺";
  const std::string sourceBefore = state.agentResponseMarkdown.str();
  view.render(201);
  tap(view, 200, 120, 202);
  view.render(203);
  assert(!sprite.imageCalls.empty());
  assert(textHas(sprite, "bold"));
  assert(textHas(sprite, "code"));
  assert(state.agentResponseMarkdown.str() == sourceBefore);
  assertTextCallsFit(sprite);
}

void testFaceLivenessAtExpiryAndMillisWrap() {
  lgfx::LGFX_Sprite sprite;
  protocol::UiState state;
  Capture sent;
  ui::FamiliarUi view(sprite, state, capture, &sent);
  state.connected = true;
  state.lastSeenMs = 100;
  state.page = protocol::Page::Face;

  view.render(30099);
  assert(textHas(sprite, "ONLINE"));
  view.render(30100);
  assert(textHas(sprite, "OFFLINE"));

  state.lastSeenMs = 0xfffffff0u;
  view.render(29983u);
  assert(textHas(sprite, "ONLINE"));  // 29,999 ms across millis wrap.
  view.render(29984u);
  assert(textHas(sprite, "OFFLINE"));  // Exactly 30,000 ms.
}

void testTextKeepsFilledTabAndRunningCardBackgrounds() {
  lgfx::LGFX_Sprite sprite;
  protocol::UiState state;
  Capture sent;
  ui::FamiliarUi view(sprite, state, capture, &sent);
  state.page = protocol::Page::Operations;
  state.deckCount = 1;
  state.deck[0].label = "RUN TASK";
  state.runningDeck = 0;
  view.render(100);
  bool selectedTabKeepsPanel = false;
  bool runningLabelKeepsTile = false;
  bool stopLabelKeepsTile = false;
  for (const auto& call : sprite.calls) {
    if (call.text == "OPS") selectedTabKeepsPanel = call.background == 0x0841;
    if (call.text == "RUN TASK") runningLabelKeepsTile = call.background != 0;
    if (call.text == "STOP") stopLabelKeepsTile = call.background != 0;
  }
  assert(selectedTabKeepsPanel && runningLabelKeepsTile && stopLabelKeepsTile);
}

void testHostRowsCompactBlankParagraphsAndTapRenderedRow() {
  lgfx::LGFX_Sprite sprite;
  protocol::UiState state;
  Capture sent;
  ui::FamiliarUi view(sprite, state, capture, &sent);
  state.page = protocol::Page::Fleet;
  auto& fleet = state.hostPages[2];
  fleet.set = true;
  fleet.title = "Fleet";
  fleet.lines[0] = "first row\n\n\ncontinued row";
  fleet.lines[1] = "short row";
  fleet.lines[2] = "last row";

  view.render(100);
  int secondRowY = -1;
  for (const auto& call : sprite.calls)
    if (call.text == "short row") secondRowY = call.y;
  assert(secondRowY == 86);  // Two font-height-plus-one lines plus row padding.
  tap(view, 20, secondRowY + 1, 101);
  assert(state.modalActive && state.modalBody == "short row");
  assert(state.modalReturn == protocol::Page::Fleet);
}

void testStatsModalRowsUseCompactMeasuredSpacing() {
  lgfx::LGFX_Sprite sprite;
  protocol::UiState state;
  Capture sent;
  ui::FamiliarUi view(sprite, state, capture, &sent);
  state.modalActive = true;
  state.modal.title = "STATS";
  state.modal.lines[0] = "S:42 tok:18k tools:7";
  state.modal.lines[1] = "job idle Status brief";
  state.modal.lines[2] = "up 3h17m serial";

  view.render(100);
  int statsY[3] = {-1, -1, -1};
  for (const auto& call : sprite.calls) {
    if (call.text == "S:42 tok:18k tools:7") statsY[0] = call.y;
    if (call.text == "job idle Status brief") statsY[1] = call.y;
    if (call.text == "up 3h17m serial") statsY[2] = call.y;
  }
  assert(statsY[0] == 50);
  assert(statsY[1] == 69);
  assert(statsY[2] == 88);

  state.modal.lines[0] = String(std::string(1000, 'x'));
  view.render(101);
  int secondRowY = -1, thirdRowY = -1;
  for (const auto& call : sprite.calls) {
    if (call.text == "job idle Status brief") secondRowY = call.y;
    if (call.text == "up 3h17m serial") thirdRowY = call.y;
    if (call.text.find('x') != std::string::npos)
      assert(call.y >= 50 && call.y + 16 <= 152);
  }
  assert(secondRowY == 103 && thirdRowY == 122);  // Rows follow the larger measured font height.
}

void testLatestResponseOpensScrollableModalFromFaceAndMessages() {
  lgfx::LGFX_Sprite sprite;
  protocol::UiState state;
  Capture sent;
  ui::FamiliarUi view(sprite, state, capture, &sent);
  state.connected = true;
  state.lastSeenMs = 100;
  state.agentResponseMarkdown = "## Заголовок\n\n- Ответ агента с **выделением** и *курсивом*, затем [ссылка](https://example.invalid).\n\n```sh\nrm *.json --flag\n```\n\nТретий абзац с продолжением ответа.\n\nЧетвёртый абзац с продолжением ответа.\n\nПятый абзац с продолжением ответа.\n\nШестой абзац с продолжением ответа.\n\nСедьмой абзац с продолжением ответа.\n\nВосьмой абзац с продолжением ответа.";
  state.agentResponseMarkdownTruncated = true;
  state.page = protocol::Page::Face;
  tap(view, 180, 120, 101);
  assert(state.modalActive && state.modalReturn == protocol::Page::Face);
  view.render(102);
  assert(textHas(sprite, "Заголовок"));
  assert(!textHas(sprite, "##"));
  assert(textHas(sprite, "выделением"));
  assert(textHas(sprite, "курсивом"));
  assert(textHas(sprite, "ссылка"));
  assert(textHas(sprite, "rm *.json --flag"));
  assert(textHas(sprite, "response shortened"));
  bool linkAccent = false, codePanel = false, headingAccent = false;
  for (const auto& call : sprite.calls) {
    if (call.text == "ссылка") linkAccent = call.foreground == 0x07F5;
    if (call.text == "rm *.json --flag") codePanel = call.background == 0x0841;
    if (call.text == "Заголовок") headingAccent = call.foreground == 0xFEE0;
  }
  assert(linkAccent && codePanel && headingAccent);
  view.touchGesture(300, 130, 300, 60, 150, 103);
  state.agentResponseMarkdown = "## Replacement B";
  state.agentResponseMarkdownTruncated = false;
  view.render(104);
  assert(!textHas(sprite, "Заголовок"));
  assert(textHas(sprite, "Четвёртый абзац"));
  assert(!textHas(sprite, "Replacement B"));
  assert(textHas(sprite, "response shortened"));
  tap(view, 300, 100, 105);
  assert(!state.modalActive && state.page == protocol::Page::Face);

  state.page = protocol::Page::Messages;
  state.entryCount = 1;
  state.entries[0] = "14:32 a: Latest response preview";
  tap(view, 50, 50, 106);
  assert(state.modalActive && state.modalReturn == protocol::Page::Messages);
  assert(state.modalBody == "Latest response preview");
  assert(!textHas(sprite, "Replacement B"));
}

void testFaceLatestPreviewStripsMarkdownSyntax() {
  lgfx::LGFX_Sprite sprite;
  protocol::UiState state;
  ui::FamiliarUi view(sprite, state, capture, nullptr);
  state.connected = true;
  state.lastSeenMs = 100;
  state.page = protocol::Page::Face;
  state.message = "## Latest **bold**\n\nA *small* `code` preview Привет 🚀";
  view.render(101);
  assert(textHas(sprite, "Latest"));
  assert(textHas(sprite, "bold"));
  assert(textHas(sprite, "small"));
  assert(textHas(sprite, "code"));
  assert(textHas(sprite, "Привет"));
  assert(!textHas(sprite, "##"));
  assert(!textHas(sprite, "**"));
  assert(!textHas(sprite, "*small*"));
  assert(hasTextWithFont(sprite, "bold", &fonts::efontJA_12_b));
  bool codeStyled = false;
  for (const auto& call : sprite.calls)
    if (call.text == "code") codeStyled = call.foreground == 0x07F5 && call.background == 0x0841;
  assert(codeStyled);
  assertTextCallsFit(sprite);
}

void testMessageMarkdownForLegacyPreviewAndAsyncFullBody() {
  lgfx::LGFX_Sprite sprite;
  protocol::UiState state;
  Capture sent;
  ui::FamiliarUi view(sprite, state, capture, &sent);
  state.page = protocol::Page::Messages;
  state.entryCount = 1;
  state.entries[0] = "14:10 a: **Legacy preview** with `code` and Привет 🚀";

  const ui::Rect card = ui::FamiliarUi::messageCardRect(0);
  view.render(100);
  assert(textHas(sprite, "Legacy preview"));
  assert(hasTextWithFont(sprite, "Legacy preview", &fonts::efontJA_12_b));
  assert(textHas(sprite, "Привет"));
  assert(!textHas(sprite, "**") && !textHas(sprite, "`"));
  bool cardCodeStyled = false;
  uint16_t previewCodeForeground = 0, previewCodeBackground = 0;
  for (const auto& call : sprite.calls) {
    if (call.text == "code") {
      cardCodeStyled = call.foreground == 0x07F5 && call.background == 0x0841;
      previewCodeForeground = call.foreground;
      previewCodeBackground = call.background;
    }
  }
  assert(cardCodeStyled);
  const ui::Rect cardBody{static_cast<int16_t>(card.x + 8), static_cast<int16_t>(card.y + 19),
                          static_cast<int16_t>(card.w - 16), static_cast<int16_t>(card.h - 20)};
  for (const auto& call : sprite.calls) {
    if (call.y < cardBody.y) continue;
    assert(call.y + static_cast<int>(16 * call.size) <= cardBody.y + cardBody.h);
    assert(call.x >= cardBody.x && call.x + call.width <= cardBody.x + cardBody.w);
  }
  assertTextCallsFit(sprite);

  tap(view, card.x + 20, card.y + 30, 101);
  assert(state.modalActive && !state.messageDetailPending);
  view.render(102);
  assert(textHas(sprite, "preview only"));
  assert(textHas(sprite, "Legacy preview"));
  assert(!textHas(sprite, "**"));
  assert(hasTextWithFont(sprite, "Legacy preview", &fonts::efontJA_12_b));
  bool modalCodeStyled = false;
  for (const auto& call : sprite.calls) {
    if (call.text == "code")
      modalCodeStyled = call.foreground == previewCodeForeground &&
                        call.background == previewCodeBackground;
  }
  assert(modalCodeStyled && textHas(sprite, "Привет"));
  assert(!textHas(sprite, "**") && !textHas(sprite, "`"));
  tap(view, 300, 100, 103);

  state.entryIds[0] = "message-with-detail";
  state.entries[0] = "14:11 a: preview text";
  tap(view, card.x + 20, card.y + 30, 104);
  assert(state.modalActive && state.messageDetailPending);
  view.render(105);
  assert(textHas(sprite, "loading full message"));
  state.messageDetailBody = "# Full **async detail**";
  state.messageDetailPending = false;
  view.render(106);
  assert(textHas(sprite, "Full"));
  assert(textHas(sprite, "async detail"));
  assert(!textHas(sprite, "#"));
  assert(!textHas(sprite, "**"));
  assert(hasTextWithFont(sprite, "async detail", &fonts::efontJA_12_b));
}

void testMessageCardsShowTwoFullFontLinesAndDetailReadsBeyondFirstFrame() {
  lgfx::LGFX_Sprite sprite;
  protocol::UiState state;
  Capture sent;
  ui::FamiliarUi view(sprite, state, capture, &sent);
  state.page = protocol::Page::Messages;
  state.entryCount = 1;
  state.entries[0] = "14:10 a: **Line one**\nLine two";
  state.entryIds[0] = "long-message";
  const ui::Rect card = ui::FamiliarUi::messageCardRect(0);
  view.render(10);
  const int16_t bodyY = card.y + 19;
  const int16_t secondLineY = bodyY + 17; // pinned efontJA_12 16 px height + 1 px gap
  bool firstLine = false, secondLine = false;
  for (const auto& call : sprite.calls) {
    firstLine |= call.text == "Line one" && call.y == bodyY && call.size == 1.0f;
    secondLine |= call.text == "Line two" && call.y == secondLineY && call.size == 1.0f;
  }
  assert(firstLine && secondLine);

  tap(view, card.x + 20, card.y + 30, 20);
  assert(state.modalActive && state.messageDetailPending);
  assert(has(sent, "\"offset\":0"));
  state.messageDetailBody = "**first page**";
  state.messageDetailHasMore = true;
  state.messageDetailNextOffset = 2800;
  state.messageDetailPending = false;
  view.tick(21);
  assert(state.messageDetailPending && state.messageDetailRequestedOffset == 2800);
  assert(has(sent, "\"offset\":2800"));

  // A complete retained body may be larger than one JSON line; scrolling must
  // still expose text from its end after the Markdown parser reads all bytes.
  state.messageDetailBody = String(std::string(12000, 'x') + " TAIL-SENTINEL");
  state.messageDetailPending = false;
  state.messageDetailHasMore = false;
  view.render(22);
  assert(!textHas(sprite, "TAIL-SENTINEL"));
  for (uint32_t i = 0; i < 80; ++i) {
    view.touchGesture(300, 120, 300, 70, 100, 30 + i);
    view.render(30 + i);
  }
  assert(textHas(sprite, "TAIL-SENTINEL"));
}

void testSettingsPageControlsAndHitRegions() {
  lgfx::LGFX_Sprite sprite;
  protocol::UiState state;
  Capture sent;
  ui::FamiliarUi view(sprite, state, capture, &sent);
  for (uint8_t i = 0; i < 8; ++i) {
    const ui::Rect tab = ui::FamiliarUi::tabRect(i);
    for (uint8_t j = i + 1; j < 8; ++j) {
      const ui::Rect other = ui::FamiliarUi::tabRect(j);
      assert(tab.x + tab.w <= other.x);
    }
  }
  const ui::Rect settingsTab = ui::FamiliarUi::tabRect(7);
  tap(view, settingsTab.x + settingsTab.w / 2, 10, 100);
  assert(state.page == protocol::Page::Settings);

  const ui::Rect theme0 = ui::FamiliarUi::settingsThemeRect(0);
  const ui::Rect theme1 = ui::FamiliarUi::settingsThemeRect(1);
  const ui::Rect theme2 = ui::FamiliarUi::settingsThemeRect(2);
  const ui::Rect theme3 = ui::FamiliarUi::settingsThemeRect(3);
  const ui::Rect theme4 = ui::FamiliarUi::settingsThemeRect(4);
  assert(theme0.x + theme0.w < theme1.x && theme1.x + theme1.w < theme2.x &&
         theme2.x + theme2.w < theme3.x && theme3.x + theme3.w < theme4.x);
  assert(theme4.x + theme4.w <= 632);
  assert(ui::FamiliarUi::settingsThemeRect(5).w == 0);
  const ui::Rect sound = ui::FamiliarUi::settingsSoundRect();
  const ui::Rect animation = ui::FamiliarUi::settingsAnimationRect();
  const ui::Rect brightness0 = ui::FamiliarUi::settingsBrightnessRect(0);
  const ui::Rect brightness3 = ui::FamiliarUi::settingsBrightnessRect(3);
  assert(theme0.y + theme0.h < sound.y && sound.x + sound.w < animation.x);
  assert(sound.y + sound.h < brightness0.y && brightness3.x + brightness3.w <= 632);
  for (uint8_t i = 0; i < 4; ++i)
    for (uint8_t j = i + 1; j < 4; ++j) {
      const ui::Rect a = ui::FamiliarUi::settingsBrightnessRect(i);
      const ui::Rect b = ui::FamiliarUi::settingsBrightnessRect(j);
      assert(a.x + a.w < b.x);
    }

  tap(view, theme1.x + theme1.w / 2, theme1.y + theme1.h / 2, 101);
  assert(state.themeId == 1 && state.uiSettingsDirty);
  state.uiSettingsDirty = false;
  tap(view, theme4.x + theme4.w / 2, theme4.y + theme4.h / 2, 102);
  assert(state.themeId == static_cast<uint8_t>(ui::themes::Id::Gruvbox));
  assert(state.uiSettingsDirty);
  view.render(102);
  bool gruvboxColorsRendered = false;
  for (const auto& call : sprite.calls)
    if (call.text == "SETTINGS")
      gruvboxColorsRendered = call.foreground == ui::themes::get(ui::themes::Id::Gruvbox).text &&
                              call.background == ui::themes::get(ui::themes::Id::Gruvbox).background;
  assert(gruvboxColorsRendered);
  state.uiSettingsDirty = false;
  tap(view, sound.x + 20, sound.y + 10, 103);
  assert(state.soundMuted && state.uiSettingsDirty);
  state.uiSettingsDirty = false;
  tap(view, animation.x + 20, animation.y + 10, 103);
  assert(!state.animationEnabled && state.uiSettingsDirty);
  state.uiSettingsDirty = false;
  tap(view, brightness0.x + brightness0.w / 2, brightness0.y + brightness0.h / 2, 104);
  assert(state.brightnessPercent == 25 && state.uiSettingsDirty);

  view.render(105);
  assert(textHas(sprite, "SOUND  MUTED"));
  assert(textHas(sprite, "ANIMATION  PAUSED"));
  assert(textHas(sprite, "25%"));
  assertTextCallsFit(sprite);
}

void testPaperThemeWhiteBackgroundSentinelAndMarkdownPanels() {
  lgfx::LGFX_Sprite sprite;
  protocol::UiState state;
  Capture sent;
  ui::FamiliarUi view(sprite, state, capture, &sent);
  const ui::Rect settingsTab = ui::FamiliarUi::tabRect(7);
  tap(view, settingsTab.x + settingsTab.w / 2, 10, 100);
  view.render(101);

  bool paperNameKeepsWhiteTile = false;
  bool paperPreviewKeepsWhiteTile = false;
  for (const auto& call : sprite.calls) {
    if (call.text == "Paper")
      paperNameKeepsWhiteTile = call.foreground == 0x1082 && call.background == 0xFFFF;
    if (call.text == "PREVIEW" && call.background == 0xFFFF)
      paperPreviewKeepsWhiteTile = true;
  }
  assert(paperNameKeepsWhiteTile && paperPreviewKeepsWhiteTile);

  const ui::Rect paper = ui::FamiliarUi::settingsThemeRect(3);
  tap(view, paper.x + paper.w / 2, paper.y + paper.h / 2, 102);
  view.render(103);
  bool settingsUsesWhiteBackground = false;
  bool settingsTextUsesWhiteBackground = false;
  for (const auto& call : sprite.calls) {
    if (call.text == "SETTINGS" && call.background == 0xFFFF)
      settingsUsesWhiteBackground = true;
    if (call.text == "SOUND  ON" && call.background == 0xFFFF)
      settingsTextUsesWhiteBackground = true;
  }
  assert(settingsUsesWhiteBackground && settingsTextUsesWhiteBackground);

  state.page = protocol::Page::Messages;
  state.entryCount = 1;
  state.entries[0] = "14:10 a: Paper `message code`";
  view.render(104);
  bool messageCodeUsesPaperPalette = false;
  for (const auto& call : sprite.calls)
    if (call.text == "message code")
      messageCodeUsesPaperPalette = call.foreground == 0x2017 && call.background == 0xEF5D;
  assert(messageCodeUsesPaperPalette);

  state.page = protocol::Page::Face;
  state.message = "Paper `face code`";
  view.render(105);
  bool faceCodeUsesPaperPalette = false;
  for (const auto& call : sprite.calls)
    if (call.text == "face code")
      faceCodeUsesPaperPalette = call.foreground == 0x2017 && call.background == 0xEF5D;
  assert(faceCodeUsesPaperPalette);
}

void testResponseOverflowKeepsFormattedPrefixAndShowsFooter() {
  lgfx::LGFX_Sprite sprite;
  protocol::UiState state;
  Capture sent;
  ui::FamiliarUi view(sprite, state, capture, &sent);
  state.connected = true;
  state.lastSeenMs = 100;
  state.page = protocol::Page::Face;
  for (int i = 0; i <= static_cast<int>(ui::markdown::kMaxBlocks); ++i)
    state.agentResponseMarkdown += String("## Heading ") + String(i) + "\n";
  tap(view, 180, 120, 101);
  view.render(102);
  assert(textHas(sprite, "Heading"));
  assert(!textHas(sprite, "##"));
  assert(textHas(sprite, "response shortened"));
}
}  // namespace

int main() {
  testTabBoundariesAndDeckGaps();
  testMessageCardGeometryTapAndModalReturn();
  testMessageBrowsingLocalHistoryAndDetailRequest();
  testMessageIdentitySurvivesReorderAndSamePreview();
  testConfirmExpiry();
  testApprovalRequiresLiveCurrentRequestAndExactButtons();
  testSwipeCannotResolveApprovalAndModalReturnsToOrigin();
  testUtf8TypographyMeasuredWrappingAcrossPages();
  testEmojiClustersAndCodepointSafeRendering();
  testEmojiRenderingInPlainAndStyledMarkdownText();
  testFaceLivenessAtExpiryAndMillisWrap();
  testTextKeepsFilledTabAndRunningCardBackgrounds();
  testHostRowsCompactBlankParagraphsAndTapRenderedRow();
  testStatsModalRowsUseCompactMeasuredSpacing();
  testLatestResponseOpensScrollableModalFromFaceAndMessages();
  testFaceLatestPreviewStripsMarkdownSyntax();
  testMessageMarkdownForLegacyPreviewAndAsyncFullBody();
  testMessageCardsShowTwoFullFontLinesAndDetailReadsBeyondFirstFrame();
  testSettingsPageControlsAndHitRegions();
  testPaperThemeWhiteBackgroundSentinelAndMarkdownPanels();
  testResponseOverflowKeepsFormattedPrefixAndShowsFooter();
  std::cout << "Familiar UI native regression tests passed\n";
}
