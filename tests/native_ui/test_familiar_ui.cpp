#include <cassert>
#include <iostream>
#include <string>
#include <vector>

#include "ui/familiar_ui.h"

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
  for (uint8_t i = 0; i < 7; ++i) {
    const ui::Rect rect = ui::FamiliarUi::tabRect(i);
    assert(rect.x >= 0 && rect.w > 0 && rect.x + rect.w <= ui::FamiliarUi::kWidth);
    tap(view, rect.x, 10, 100 + i);
    assert(state.page == static_cast<protocol::Page>(i));
    tap(view, rect.x + rect.w - 1, 10, 200 + i);
    assert(state.page == static_cast<protocol::Page>(i));
    if (i < 6) {
      tap(view, rect.x + rect.w, 10, 250 + i);
      assert(state.page == static_cast<protocol::Page>(i + 1));
    }
  }
  const ui::Rect last = ui::FamiliarUi::tabRect(6);
  assert(last.x + last.w == ui::FamiliarUi::kWidth);
  tap(view, ui::FamiliarUi::kWidth, 10, 300);
  assert(state.page == protocol::Page::Device);

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

void testMessageHistoryRequests() {
  lgfx::LGFX_Sprite sprite;
  protocol::UiState state;
  Capture sent;
  ui::FamiliarUi view(sprite, state, capture, &sent);
  state.page = protocol::Page::Messages;
  view.touchGesture(300, 120, 300, 60, 220, 1000);
  assert(has(sent, "\"cmd\":\"msgs\""));
  assert(has(sent, "\"off\":5"));
  sent.lines.clear();
  state.historyOffset = 10;
  view.touchGesture(300, 60, 300, 120, 220, 1100);
  assert(has(sent, "\"cmd\":\"msgs\""));
  assert(has(sent, "\"off\":5"));
}
}  // namespace

int main() {
  testTabBoundariesAndDeckGaps();
  testConfirmExpiry();
  testApprovalRequiresLiveCurrentRequestAndExactButtons();
  testSwipeCannotResolveApprovalAndModalReturnsToOrigin();
  testMessageHistoryRequests();
  std::cout << "Familiar UI native regression tests passed\n";
}
