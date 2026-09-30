#include "familiar_ui.h"

#include <ArduinoJson.h>
#include <stdlib.h>

namespace ui {
namespace {
constexpr uint16_t kBg = 0x0000, kPanel = 0x0841, kDim = 0x3D27;
constexpr uint16_t kGreen = 0x57EA, kInk = 0xA7F5, kAmber = 0xFEE0, kRed = 0xF965;
constexpr uint16_t kCyan = 0x07F5;
constexpr const char* kTabs[] = {"FACE", "MSGS", "OPS", "FLEET", "CRON", "NET", "DEV"};
constexpr uint16_t kDeckColors[] = {kGreen, kAmber, kRed, kCyan};
constexpr int16_t kContentX = 8, kContentRight = 632;
constexpr int16_t kApprovalTop = 130, kApprovalHeight = 36;

uint16_t pageIndex(protocol::Page page) { return static_cast<uint16_t>(page); }
}

FamiliarUi::FamiliarUi(lgfx::LGFX_Sprite& sprite, protocol::UiState& state,
                       Emit emit, void* emitContext)
    : sprite_(sprite), state_(state), emit_(emit), emitContext_(emitContext) {}

void FamiliarUi::setFacePainter(FacePainter painter, void* context) {
  facePainter_ = painter;
  faceContext_ = context;
}

Rect FamiliarUi::tabRect(uint8_t index) {
  if (index >= 7) return {0, 0, 0, 0};
  const int16_t left = (int32_t)index * kWidth / 7;
  const int16_t right = (int32_t)(index + 1) * kWidth / 7;
  return {left, 0, static_cast<int16_t>(right - left), kTabHeight};
}

Rect FamiliarUi::deckRect(uint8_t index) {
  if (index >= 6) return {0, 0, 0, 0};
  constexpr int16_t margin = 8, gapX = 8, top = 52, gapY = 8, height = 52;
  const int16_t usable = kWidth - 2 * margin - 2 * gapX;
  const int16_t left = margin + (index % 3) * (usable / 3 + gapX);
  const int16_t right = index % 3 == 2 ? kWidth - margin : left + usable / 3;
  return {left, static_cast<int16_t>(top + (index / 3) * (height + gapY)),
          static_cast<int16_t>(right - left), height};
}

Rect FamiliarUi::allowRect() { return {8, kApprovalTop, 304, kApprovalHeight}; }
Rect FamiliarUi::denyRect() { return {328, kApprovalTop, 304, kApprovalHeight}; }
Rect FamiliarUi::approvalTextRect() { return {8, 45, 624, 74}; }
Rect FamiliarUi::artRect() { return {0, kTabHeight, 144, 144}; }

void FamiliarUi::emit(const String& json) {
  if (emit_) emit_(emitContext_, json);
}

void FamiliarUi::tick(uint32_t nowMs) {
  nowMs_ = nowMs;
  if (state_.toastUntilMs && static_cast<int32_t>(nowMs - state_.toastUntilMs) >= 0) {
    state_.toastUntilMs = 0;
    state_.toast = "";
    state_.dirty = true;
  }
  if (state_.armedDeck >= 0 && static_cast<int32_t>(nowMs - state_.armedUntilMs) >= 0) {
    state_.armedDeck = -1;
    state_.dirty = true;
  }
}

void FamiliarUi::render(uint32_t nowMs) {
  tick(nowMs);
  sprite_.fillScreen(kBg);
  const bool live = state_.connected && nowMs - state_.lastSeenMs < 30000;
  const uint16_t accent = state_.waiting ? (((nowMs / 400) & 1) ? kRed : kAmber)
                         : state_.running ? kAmber : live ? kGreen : kDim;
  drawTabs(accent);
  if (state_.modalActive) drawModal();
  else drawPage();
  if (!state_.modalActive && state_.toastUntilMs && !state_.approval.active) {
    sprite_.fillRoundRect(410, 26, 222, 20, 4, kPanel);
    sprite_.setTextColor(kAmber, kPanel);
    sprite_.setTextSize(1);
    sprite_.setCursor(416, 32);
    sprite_.print(state_.toast.substring(0, 34));
  }
  if (!state_.modalActive && state_.runningDeck >= 0 &&
      state_.page != protocol::Page::Operations && state_.waiting == 0) {
    sprite_.fillRoundRect(504, 26, 128, 20, 4, kPanel);
    sprite_.drawRoundRect(504, 26, 128, 20, 4, kAmber);
    sprite_.setTextColor(kAmber, kPanel);
    sprite_.setTextSize(1);
    sprite_.setCursor(514, 32);
    sprite_.print("WORKING");
  }
  state_.dirty = false;
}

void FamiliarUi::drawTabs(uint16_t accent) {
  for (uint8_t i = 0; i < 7; ++i) {
    const Rect r = tabRect(i);
    const bool selected = pageIndex(state_.page) == i;
    sprite_.fillRect(r.x, r.y, r.w, r.h, selected ? kPanel : kBg);
    sprite_.drawFastHLine(r.x + 2, kTabHeight - 2, r.w - 4,
                          selected ? accent : kDim);
    sprite_.setTextSize(1);
    sprite_.setTextColor(selected ? kInk : kDim, selected ? kPanel : kBg);
    const int16_t textW = strlen(kTabs[i]) * 6;
    sprite_.setCursor(r.x + (r.w - textW) / 2, 7);
    sprite_.print(kTabs[i]);
  }
}

void FamiliarUi::drawPage() {
  switch (state_.page) {
    case protocol::Page::Face: drawFace(); break;
    case protocol::Page::Messages: drawMessages(); break;
    case protocol::Page::Operations: drawOperations(); break;
    case protocol::Page::Fleet: drawHostPage(state_.hostPages[2], "FLEET"); break;
    case protocol::Page::Cron: drawHostPage(state_.hostPages[0], "CRON JOBS"); break;
    case protocol::Page::Network: drawHostPage(state_.hostPages[1], "GATEWAY"); break;
    case protocol::Page::Device: {
      sprite_.setTextSize(1);
      sprite_.setTextColor(kInk, kBg);
      sprite_.setCursor(kContentX, 31); sprite_.print("DEVICE STATUS");
      sprite_.drawFastHLine(kContentX, 43, kContentRight - kContentX, kDim);
      const String rows[] = {
        state_.sdStatus + "   " + state_.touchStatus,
        state_.batteryStatus + "   " + state_.rtcStatus,
        state_.wifiStatus + "   " + state_.networkStatus + "  " + state_.linkStatus,
        state_.audioStatus,
        state_.motionStatus,
        "heap:" + String(state_.freeHeap) + "  rotation:" + String(state_.rotation)
      };
      for (uint8_t i = 0; i < 6; ++i) {
        sprite_.setTextColor(i == 2 && !state_.wifiConnected ? kAmber : kGreen, kBg);
        sprite_.setCursor(kContentX, 50 + i * 19); sprite_.print(rows[i].substring(0, 102));
      }
      break;
    }
    default: break;
  }
}

void FamiliarUi::drawFace() {
  const bool live = state_.connected && nowMs_ - state_.lastSeenMs < 30000;
  const String group = !live ? "sleep" : state_.waiting ? "waiting" : state_.running ? "thinking" : "idle";
  const Rect art = artRect();
  if (facePainter_) facePainter_(sprite_, group, art, nowMs_, faceContext_);
  else {
    sprite_.drawRoundRect(art.x + 30, art.y + 40, 84, 64, 14, kGreen);
    sprite_.drawCircle(art.x + 72, art.y + 72, 42, kDim);
    sprite_.setTextSize(2); sprite_.setTextColor(kGreen, kBg);
    sprite_.setCursor(art.x + 64, art.y + 63); sprite_.print("H");
  }
  sprite_.drawFastVLine(152, 30, 132, kDim);
  sprite_.setTextSize(1);
  sprite_.setTextColor(state_.waiting ? kRed : state_.running ? kAmber : live ? kGreen : kDim, kBg);
  sprite_.setCursor(164, 31); sprite_.print(state_.waiting ? "WAITING" : state_.running ? "WORKING" : live ? "ONLINE" : "IDLE");
  sprite_.setTextColor(kInk, kBg);
  sprite_.setCursor(164, 46); sprite_.printf("SESS %d  RUN %d  WAIT %d", state_.total, state_.running, state_.waiting);
  sprite_.setTextColor(kDim, kBg); sprite_.setCursor(164, 62); sprite_.print("JOB");
  sprite_.setTextColor(kGreen, kBg); sprite_.setCursor(190, 62); sprite_.print(state_.jobLabel.substring(0, 62));
  sprite_.setTextColor(kDim, kBg); sprite_.setCursor(164, 79); sprite_.print("TOKENS");
  sprite_.setTextColor(kInk, kBg); sprite_.setCursor(208, 79); sprite_.print(state_.tokensToday);
  sprite_.drawFastHLine(164, 94, 468, kDim);
  sprite_.setTextColor(kDim, kBg); sprite_.setCursor(164, 100); sprite_.print("LATEST");
  sprite_.setTextColor(kGreen, kBg);
  drawWrapped(state_.message, 164, 113, 468, 52, kGreen);
  sprite_.setTextColor(live ? kGreen : kDim, kBg);
  sprite_.setCursor(510, 31); sprite_.print(live ? state_.linkStatus : "OFFLINE");
}

void FamiliarUi::drawMessages() {
  const bool history = state_.historyOffset > 0 && state_.historyCount > 0;
  sprite_.setTextSize(1);
  sprite_.setTextColor(kInk, kBg);
  sprite_.setCursor(kContentX, 31);
  if (history) {
    sprite_.printf("HISTORY %u-%u / %u  swipe down for newer", state_.historyOffset + 1,
                   state_.historyOffset + state_.historyCount, state_.historyTotal);
  } else sprite_.print("RECENT TRAFFIC   swipe up for history");
  const uint8_t count = history ? state_.historyCount : state_.entryCount;
  const int16_t rowHeight = count <= 4 ? 27 : 23;
  for (uint8_t i = 0; i < count; ++i) {
    const int16_t y = 46 + i * rowHeight;
    const String& line = history ? state_.history[i] : state_.entries[i];
    sprite_.setTextColor(!history && i == 0 ? kInk : kGreen, kBg);
    drawWrapped(line, kContentX, y, kContentRight - 2 * kContentX, rowHeight - 2,
                !history && i == 0 ? kInk : kGreen);
    if (i + 1 < count) sprite_.drawFastHLine(kContentX, y + rowHeight - 2,
                                             kContentRight - 2 * kContentX, kPanel);
  }
  if (!count) {
    sprite_.setTextColor(kDim, kBg); sprite_.setCursor(kContentX, 56); sprite_.print("no messages yet");
  }
}

void FamiliarUi::drawOperations() {
  if (state_.approval.active || state_.waiting > 0) {
    sprite_.setTextSize(1);
    sprite_.setTextColor(kRed, kBg); sprite_.setCursor(8, 30); sprite_.print("APPROVAL REQUIRED - tap request for full text");
    const Rect detail = approvalTextRect();
    sprite_.drawRoundRect(detail.x, detail.y, detail.w, detail.h, 4, kDim);
    sprite_.setTextColor(kInk, kBg);
    drawWrapped(state_.approval.active ? state_.approval.text : state_.message,
                detail.x + 8, detail.y + 6, detail.w - 16, detail.h - 12, kInk);
    const Rect allow = allowRect(), deny = denyRect();
    sprite_.drawRoundRect(allow.x, allow.y, allow.w, allow.h, 5, kGreen);
    sprite_.drawRoundRect(deny.x, deny.y, deny.w, deny.h, 5, kRed);
    sprite_.setTextColor(kGreen, kBg); sprite_.setCursor(allow.x + 130, allow.y + 13); sprite_.print("ALLOW");
    sprite_.setTextColor(kRed, kBg); sprite_.setCursor(deny.x + 132, deny.y + 13); sprite_.print("DENY");
    return;
  }
  sprite_.setTextSize(1);
  sprite_.setTextColor(kInk, kBg); sprite_.setCursor(8, 30); sprite_.print("THE DECK");
  sprite_.setTextColor(kDim, kBg); sprite_.setCursor(90, 30);
  sprite_.print((state_.jobState + ": " + state_.jobLabel).substring(0, 65));
  if (!state_.deckCount) {
    sprite_.setTextColor(kDim, kBg); sprite_.setCursor(8, 64); sprite_.print("no buttons from host yet");
    return;
  }
  for (uint8_t i = 0; i < state_.deckCount; ++i) {
    const Rect r = deckRect(i);
    const uint16_t color = kDeckColors[state_.deck[i].color & 3];
    const bool running = state_.runningDeck == i;
    const bool armed = state_.armedDeck == i && static_cast<int32_t>(nowMs_ - state_.armedUntilMs) < 0;
    if (running) sprite_.fillRoundRect(r.x, r.y, r.w, r.h, 5, color);
    else sprite_.drawRoundRect(r.x, r.y, r.w, r.h, 5, armed ? kAmber : color);
    sprite_.setTextColor(running ? kBg : armed ? kAmber : color, running ? color : kBg);
    sprite_.setCursor(r.x + 8, r.y + 11);
    const String label = armed ? "SURE?" : state_.deck[i].label;
    sprite_.print(label.substring(0, (r.w - 16) / 6));
    sprite_.setTextColor(running ? kBg : kDim, running ? color : kBg);
    sprite_.setCursor(r.x + r.w - 38, r.y + 35);
    sprite_.print(running ? "STOP" : state_.deck[i].confirm && !armed ? "2TAP" : "");
  }
}

void FamiliarUi::drawHostPage(const protocol::HostPage& page, const char* fallback) {
  sprite_.setTextSize(1);
  sprite_.setTextColor(kInk, kBg); sprite_.setCursor(8, 31);
  sprite_.print(page.set ? page.title.substring(0, 90) : String(fallback));
  sprite_.drawFastHLine(8, 44, 624, kDim);
  if (!page.set) {
    sprite_.setTextColor(kDim, kBg); sprite_.setCursor(8, 54); sprite_.print("no data from host yet");
    return;
  }
  const int16_t rowY[] = {52, 88, 124};
  for (uint8_t i = 0; i < 3; ++i) {
    sprite_.setTextColor(kGreen, kBg);
    drawWrapped(page.lines[i], 8, rowY[i], 624, 32, kGreen);
  }
  sprite_.setTextColor(kDim, kBg);
  sprite_.setCursor(8, 160);
  sprite_.print("tap a line to read full text");
}

void FamiliarUi::drawModal() {
  sprite_.setTextSize(1);
  sprite_.setTextColor(kInk, kBg); sprite_.setCursor(8, 31);
  sprite_.print(state_.modal.title.substring(0, 90));
  sprite_.drawFastHLine(8, 44, 624, kDim);
  if (state_.modalBody.length()) {
    drawWrapped(state_.modalBody, 8, 50, 624, 102, kGreen, 1, modalScroll_);
    sprite_.setTextColor(kDim, kBg); sprite_.setCursor(8, 158); sprite_.print("swipe up/down to read - tap to return");
  } else {
    for (uint8_t i = 0; i < 3; ++i) {
      sprite_.setTextColor(kGreen, kBg);
      drawWrapped(state_.modal.lines[i], 8, 52 + i * 34, 624, 30, kGreen);
    }
    sprite_.setTextColor(kDim, kBg); sprite_.setCursor(8, 158); sprite_.print("tap to return");
  }
}

void FamiliarUi::drawWrapped(const String& text, int16_t x, int16_t y, int16_t width,
                             int16_t height, uint16_t color, uint8_t scale,
                             uint16_t skipLines) {
  const int16_t charsByWidth = width / (6 * scale);
  const int16_t maxChars = charsByWidth > 0 ? charsByWidth : 1;
  const int16_t lineHeight = 8 * scale + 3;
  const int16_t linesByHeight = height / lineHeight;
  const int16_t maxLines = linesByHeight > 0 ? linesByHeight : 1;
  int16_t lineNo = 0;
  uint16_t offset = 0;
  while (offset < text.length()) {
    int16_t take = min<int16_t>(maxChars, text.length() - offset);
    if (offset + take < text.length()) {
      int16_t space = text.lastIndexOf(' ', offset + take);
      if (space >= offset + maxChars / 2) take = space - offset;
    }
    if (lineNo >= skipLines && lineNo < skipLines + maxLines) {
      sprite_.setTextSize(scale); sprite_.setTextColor(color, kBg);
      sprite_.setCursor(x, y + (lineNo - skipLines) * lineHeight);
      sprite_.print(text.substring(offset, offset + take));
    }
    offset += take;
    while (offset < text.length() && text[offset] == ' ') ++offset;
    ++lineNo;
  }
}

void FamiliarUi::selectPage(uint8_t index) {
  if (index >= 7) return;
  state_.page = static_cast<protocol::Page>(index);
  state_.modalActive = false;
  state_.modalBody = "";
  state_.toastUntilMs = 0;
  state_.historyOffset = 0;
  state_.dirty = true;
}

void FamiliarUi::decideApproval(const char* decision) {
  if (!state_.connected || nowMs_ - state_.lastSeenMs >= 30000 ||
      !state_.approval.active || !state_.approval.id.length()) {
    state_.message = "Awaiting a current approval request from Hermes";
    state_.dirty = true;
    return;
  }
  JsonDocument document;
  document["cmd"] = "permission";
  document["decision"] = decision;
  document["id"] = state_.approval.id;
  String line;
  serializeJson(document, line);
  emit(line);
  state_.approval.active = false;
  state_.waiting = 0;
  state_.message = String("decision: ") + decision;
  state_.dirty = true;
}

void FamiliarUi::handleTap(int16_t x, int16_t y) {
  emit(String("{\"cmd\":\"touch\",\"x\":") + x + ",\"y\":" + y + "}");
  if (state_.toastUntilMs) { state_.toastUntilMs = 0; state_.toast = ""; }
  if (state_.modalActive) {
    state_.modalActive = false;
    state_.modalBody = "";
    state_.page = state_.modalReturn;
    state_.dirty = true;
    return;
  }
  for (uint8_t i = 0; i < 7; ++i) if (tabRect(i).contains(x, y)) { selectPage(i); return; }
  if (state_.page == protocol::Page::Operations) {
    if (state_.approval.active || state_.waiting > 0) {
      // Exact regions alone resolve an approval. Its text, title, border,
      // surrounding whitespace and the gap between buttons never do.
      if (allowRect().contains(x, y)) decideApproval("once");
      else if (denyRect().contains(x, y)) decideApproval("deny");
      else if (approvalTextRect().contains(x, y)) {
        state_.modal.title = "APPROVAL DETAIL";
        state_.modalBody = state_.approval.detail.length()
                             ? state_.approval.text + "\n\n" + state_.approval.detail
                             : state_.approval.text;
        state_.modalReturn = protocol::Page::Operations;
        state_.modalActive = true;
        modalScroll_ = 0;
      }
    } else {
      for (uint8_t i = 0; i < state_.deckCount; ++i) if (deckRect(i).contains(x, y)) {
        if (state_.deck[i].confirm && state_.runningDeck != i && state_.armedDeck != i) {
          state_.armedDeck = i; state_.armedUntilMs = nowMs_ + 2500;
          state_.message = state_.deck[i].label + ": tap again";
        } else {
          state_.armedDeck = -1;
          emit(String("{\"cmd\":\"deck\",\"i\":") + i + "}");
          state_.message = "deck: " + state_.deck[i].label;
        }
        state_.dirty = true;
        return;
      }
    }
  } else if (state_.page == protocol::Page::Face && x < artRect().x + artRect().w) {
    state_.message = "face tapped";
  } else if (state_.page == protocol::Page::Face) {
    emit("{\"cmd\":\"stats\"}");
  } else if (state_.page == protocol::Page::Fleet ||
             state_.page == protocol::Page::Cron ||
             state_.page == protocol::Page::Network) {
    const protocol::HostPage* page = state_.page == protocol::Page::Fleet
        ? &state_.hostPages[2]
        : state_.page == protocol::Page::Cron ? &state_.hostPages[0]
                                               : &state_.hostPages[1];
    if (page->set && x >= 8 && x < 632 && y >= 52 && y < 156) {
      const uint8_t row = static_cast<uint8_t>((y - 52) / 36);
      state_.modal.title = page->title;
      state_.modalBody = page->lines[row];
      state_.modalReturn = state_.page;
      state_.modalActive = true;
      modalScroll_ = 0;
    } else if (state_.page == protocol::Page::Network) {
      emit("{\"cmd\":\"net\"}");
    }
  }
  state_.dirty = true;
}

void FamiliarUi::touchGesture(int16_t sx, int16_t sy, int16_t ex, int16_t ey,
                              uint32_t durationMs, uint32_t nowMs) {
  tick(nowMs);
  const int16_t dx = ex - sx, dy = ey - sy;
  if (state_.modalActive && abs(dy) >= 36 && abs(dx) <= 55) {
    if (state_.modalBody.length()) {
      modalScroll_ = dy < 0 ? modalScroll_ + 3 : modalScroll_ > 3 ? modalScroll_ - 3 : 0;
      state_.dirty = true;
    }
    return;
  }
  if (state_.modalActive) {
    state_.modalActive = false;
    state_.modalBody = "";
    state_.page = state_.modalReturn;
    state_.dirty = true;
    return;
  }
  const bool horizontal = abs(dx) >= 45 && abs(dy) <= 55;
  const bool vertical = abs(dy) >= 45 && abs(dx) <= 55;
  if (horizontal) {
    int next = (static_cast<int>(state_.page) + (dx < 0 ? 1 : 6)) % 7;
    selectPage(next);
    state_.message = dx < 0 ? "swipe: next" : "swipe: prev";
    emit(String("{\"cmd\":\"swipe\",\"dx\":") + dx + ",\"dy\":" + dy + "}");
    return;
  }
  if (vertical && !state_.modalActive && state_.page == protocol::Page::Messages) {
    int next = static_cast<int>(state_.historyOffset) + (dy < 0 ? 5 : -5);
    if (next <= 0) {
      state_.historyOffset = 0;
      state_.dirty = true;
    } else emit(String("{\"cmd\":\"msgs\",\"off\":") + next + "}");
    return;
  }
  if (durationMs < 1200 && abs(dx) < 24 && abs(dy) < 24) handleTap(sx, sy);
}

}  // namespace ui
