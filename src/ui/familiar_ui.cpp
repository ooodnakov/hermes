#include "familiar_ui.h"

#include <ArduinoJson.h>
#include <cstdio>
#include <stdlib.h>
#include <array>
#include <string_view>

#include "emoji_text.h"

namespace ui {
namespace {
constexpr uint16_t kBg = 0x0000, kPanel = 0x0841, kDim = 0x7BEF;
constexpr uint16_t kGreen = 0xAFE5, kInk = 0xDFFF, kAmber = 0xFEE0, kRed = 0xF965;
constexpr uint16_t kCyan = 0x07F5;
constexpr const char* kTabs[] = {"FACE", "MSGS", "OPS", "FLEET", "CRON", "NET", "DEV"};
constexpr uint16_t kDeckColors[] = {kGreen, kAmber, kRed, kCyan};
constexpr int16_t kContentX = 8, kContentRight = 632;
constexpr int16_t kApprovalTop = 130, kApprovalHeight = 36;

uint16_t pageIndex(protocol::Page page) { return static_cast<uint16_t>(page); }

std::string_view textView(const String& text) {
  return std::string_view(text.c_str(), text.length());
}

uint8_t utf8SequenceLength(uint8_t lead) {
  if ((lead & 0xe0) == 0xc0) return 2;
  if ((lead & 0xf0) == 0xe0) return 3;
  if ((lead & 0xf8) == 0xf0) return 4;
  return 1;
}

String utf8Glyph(const String& text, uint16_t offset, uint16_t& next) {
  const uint8_t lead = static_cast<uint8_t>(text[offset]);
  const uint8_t length = utf8SequenceLength(lead);
  next = static_cast<uint16_t>(offset + 1);
  if (length > 1) {
    while (next < text.length() && next < offset + length &&
           (static_cast<uint8_t>(text[next]) & 0xc0) == 0x80) ++next;
  }
  return text.substring(offset, next);
}

String plainText(const String& source) {
  String result;
  bool previousNewline = false;
  for (uint16_t i = 0; i < source.length();) {
    const unsigned char c = static_cast<unsigned char>(source[i]);
    if (c == '\r') { ++i; continue; }
    if (c < 0x20 && c != '\n' && c != '\t') { ++i; continue; }
    if (c == '\t') { result += " "; ++i; continue; }
    if (c == '\n') {
      if (!previousNewline) result += "\n";
      previousNewline = true;
      ++i;
      continue;
    }
    uint16_t next = i;
    const String glyph = utf8Glyph(source, i, next);
    result += glyph;
    i = next;
    previousNewline = false;
  }
  return result;
}

uint16_t countWrappedLines(lgfx::LGFX_Sprite& sprite, const String& source,
                           int16_t width, float size = 1.0f) {
  const String text = plainText(source);
  sprite.setFont(&fonts::efontJA_12);
  sprite.setTextWrap(false);
  sprite.setTextSize(size);
  uint16_t lines = 0;
  uint16_t offset = 0;
  while (offset < text.length()) {
    if (text[offset] == '\n') { ++lines; ++offset; continue; }
    while (offset < text.length() && text[offset] == ' ') ++offset;
    if (offset >= text.length()) break;
    const uint16_t start = offset;
    uint16_t fitEnd = start;
    uint16_t lastSpace = UINT16_MAX;
    uint16_t scan = start;
    while (scan < text.length() && text[scan] != '\n') {
      emoji_text::Token token;
      size_t tokenOffset = scan;
      if (!emoji_text::nextToken(textView(text), tokenOffset, token)) break;
      const uint16_t next = static_cast<uint16_t>(token.end);
      const String glyph = text.substring(scan, next);
      if (emoji_text::measure(sprite, textView(text).substr(start, next - start), size) > width) break;
      if (glyph == " ") lastSpace = scan;
      fitEnd = next;
      scan = next;
    }
    if (fitEnd == start) {
      size_t tokenOffset = start;
      emoji_text::Token token;
      if (emoji_text::nextToken(textView(text), tokenOffset, token))
        fitEnd = static_cast<uint16_t>(token.end);
    }
    offset = fitEnd;
    if (scan < text.length() && text[scan] != '\n' && lastSpace != UINT16_MAX && lastSpace > start)
      offset = static_cast<uint16_t>(lastSpace + 1);
    if (offset < text.length() && text[offset] == '\n') ++offset;
    while (offset < text.length() && text[offset] == ' ') ++offset;
    ++lines;
  }
  return lines;
}

std::array<Rect, 3> hostRowRects(lgfx::LGFX_Sprite& sprite, const protocol::HostPage& page) {
  std::array<Rect, 3> rects{};
  sprite.setFont(&fonts::efontJA_12);
  sprite.setTextWrap(false);
  sprite.setTextSize(1.0f);
  const int16_t lineHeight = static_cast<int16_t>(sprite.fontHeight()) + 1;
  int16_t top = 50;
  for (uint8_t i = 0; i < 3; ++i) {
    if (!page.lines[i].length()) continue;
    const uint16_t lineCount = std::max<uint16_t>(1, countWrappedLines(sprite, page.lines[i], 624));
    const uint16_t shownLines = min<uint16_t>(lineCount, 2);
    const int16_t rowHeight = static_cast<int16_t>(shownLines * lineHeight + 2);
    rects[i] = {8, top, 624, rowHeight};
    top = static_cast<int16_t>(top + rowHeight);
  }
  return rects;
}

int16_t messageRowHeight(uint8_t count) {
  constexpr int16_t top = 48, bottomPadding = 7;
  const int16_t available = FamiliarUi::kHeight - top - bottomPadding;
  return count ? std::max<int16_t>(14, available / count) : 20;
}
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
  // Face painters and the reusable sprite can leave global text state behind.
  // Set a known UTF-8 font and disable implicit edge wrapping every frame.
  sprite_.setFont(&fonts::efontJA_12);
  sprite_.setTextWrap(false);
  sprite_.setTextSize(1.0f);
  sprite_.fillScreen(kBg);
  const bool live = state_.connected && nowMs - state_.lastSeenMs < 30000;
  const uint16_t accent = state_.waiting ? (((nowMs / 400) & 1) ? kRed : kAmber)
                         : state_.running ? kAmber : live ? kGreen : kDim;
  drawTabs(accent);
  if (state_.modalActive) drawModal();
  else drawPage();
  if (!state_.modalActive && state_.toastUntilMs && !state_.approval.active) {
    sprite_.fillRoundRect(410, 26, 222, 20, 4, kPanel);
    drawText(state_.toast, 416, 30, 210, kAmber, 1.0f, true, kPanel);
  }
  if (!state_.modalActive && state_.runningDeck >= 0 &&
      state_.page != protocol::Page::Operations && state_.waiting == 0) {
    sprite_.fillRoundRect(504, 26, 128, 20, 4, kPanel);
    sprite_.drawRoundRect(504, 26, 128, 20, 4, kAmber);
    drawText("WORKING", 514, 30, 112, kAmber, 1.0f, true, kPanel);
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
    drawCenteredText(kTabs[i], {r.x, 2, r.w, 17}, selected ? kInk : kDim,
                     1.0f, selected ? kPanel : kBg);
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
      drawText("DEVICE STATUS", kContentX, 27, kContentRight - kContentX, kInk, 1.2f);
      sprite_.drawFastHLine(kContentX, 46, kContentRight - kContentX, kDim);
      const String rows[] = {
        state_.sdStatus + "   " + state_.touchStatus,
        state_.batteryStatus + "   " + state_.rtcStatus,
        state_.wifiStatus + "   " + state_.networkStatus + "  " + state_.linkStatus,
        state_.audioStatus,
        state_.motionStatus,
        "heap:" + String(state_.freeHeap) + "  rotation:" + String(state_.rotation)
      };
      for (uint8_t i = 0; i < 6; ++i) {
        drawText(rows[i], kContentX, 49 + i * 19, kContentRight - 2 * kContentX,
                 i == 2 && !state_.wifiConnected ? kAmber : kGreen, 1.0f);
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
  drawText(state_.waiting ? "WAITING" : state_.running ? "WORKING" : live ? "ONLINE" : "IDLE",
           164, 29, 338, state_.waiting ? kRed : state_.running ? kAmber : live ? kGreen : kDim,
           1.2f);
  char counts[48];
  snprintf(counts, sizeof(counts), "SESS %d   RUN %d   WAIT %d",
           state_.total, state_.running, state_.waiting);
  drawText(counts, 164, 47, 468, kInk, 1.0f, false);
  drawText("JOB", 164, 64, 36, kDim, 1.0f, false);
  drawText(state_.jobLabel, 200, 64, 432, kGreen, 1.0f);
  drawText("TOKENS", 164, 81, 55, kDim, 1.0f, false);
  drawText(String(state_.tokensToday), 222, 81, 180, kInk, 1.0f);
  sprite_.drawFastHLine(164, 94, 468, kDim);
  drawText("LATEST", 164, 98, 468, kDim, 1.0f, false);
  drawWrapped(state_.message, 164, 113, 468, 52, kGreen);
  drawText(live ? state_.linkStatus : "OFFLINE", 510, 31, 122, live ? kGreen : kDim, 1.0f);
}

void FamiliarUi::drawMessages() {
  const bool history = state_.historyOffset > 0 && state_.historyCount > 0;
  char heading[80];
  if (history) {
    snprintf(heading, sizeof(heading), "HISTORY %u-%u / %u  swipe down for newer",
             state_.historyOffset + 1, state_.historyOffset + state_.historyCount,
             state_.historyTotal);
  } else snprintf(heading, sizeof(heading), "RECENT TRAFFIC   swipe up for history");
  drawText(heading, kContentX, 28, kContentRight - 2 * kContentX, kInk, 1.0f);
  const uint8_t count = history ? state_.historyCount : state_.entryCount;
  const int16_t rowHeight = messageRowHeight(count);
  for (uint8_t i = 0; i < count; ++i) {
    const int16_t y = 48 + i * rowHeight;
    const String& line = history ? state_.history[i] : state_.entries[i];
    drawWrapped(line, kContentX, y, kContentRight - 2 * kContentX, rowHeight - 2,
                !history && i == 0 ? kInk : kGreen);
    if (i + 1 < count) sprite_.drawFastHLine(kContentX, y + rowHeight - 2,
                                             kContentRight - 2 * kContentX, kPanel);
  }
  if (!count) {
    drawText("no messages yet", kContentX, 55, kContentRight - 2 * kContentX, kDim);
  }
}

void FamiliarUi::drawOperations() {
  if (state_.approval.active || state_.waiting > 0) {
    drawText("APPROVAL REQUIRED - tap request for full text", 8, 27, 624, kRed, 1.0f);
    const Rect detail = approvalTextRect();
    sprite_.drawRoundRect(detail.x, detail.y, detail.w, detail.h, 4, kDim);
    drawWrapped(state_.approval.active ? state_.approval.text : state_.message,
                detail.x + 8, detail.y + 6, detail.w - 16, detail.h - 12, kInk);
    const Rect allow = allowRect(), deny = denyRect();
    sprite_.drawRoundRect(allow.x, allow.y, allow.w, allow.h, 5, kGreen);
    sprite_.drawRoundRect(deny.x, deny.y, deny.w, deny.h, 5, kRed);
    drawCenteredText("ALLOW", allow, kGreen, 1.2f);
    drawCenteredText("DENY", deny, kRed, 1.2f);
    return;
  }
  drawText("THE DECK", 8, 27, 76, kInk, 1.2f, false);
  drawText(state_.jobState + ": " + state_.jobLabel, 90, 29, 542, kDim, 1.0f);
  if (!state_.deckCount) {
    drawText("no buttons from host yet", 8, 62, 624, kDim);
    return;
  }
  for (uint8_t i = 0; i < state_.deckCount; ++i) {
    const Rect r = deckRect(i);
    const uint16_t color = kDeckColors[state_.deck[i].color & 3];
    const bool running = state_.runningDeck == i;
    const bool armed = state_.armedDeck == i && static_cast<int32_t>(nowMs_ - state_.armedUntilMs) < 0;
    if (running) sprite_.fillRoundRect(r.x, r.y, r.w, r.h, 5, color);
    else sprite_.drawRoundRect(r.x, r.y, r.w, r.h, 5, armed ? kAmber : color);
    const String label = armed ? "SURE?" : state_.deck[i].label;
    drawText(label, r.x + 8, r.y + 8, r.w - 16,
             running ? kBg : armed ? kAmber : color, 1.0f, true, running ? color : kBg);
    drawCenteredText(running ? "STOP" : state_.deck[i].confirm && !armed ? "2TAP" : "",
                     {static_cast<int16_t>(r.x + r.w - 52), static_cast<int16_t>(r.y + 31), 44, 16},
                     running ? kBg : kDim, 0.85f, running ? color : kBg);
  }
}

void FamiliarUi::drawHostPage(const protocol::HostPage& page, const char* fallback) {
  drawText(page.set ? page.title : String(fallback), 8, 27, 624, kInk, 1.2f);
  sprite_.drawFastHLine(8, 46, 624, kDim);
  if (!page.set) {
    drawText("no data from host yet", 8, 57, 624, kDim, 1.0f);
    return;
  }
  const auto rows = hostRowRects(sprite_, page);
  for (uint8_t i = 0; i < 3; ++i) {
    if (rows[i].h == 0) continue;
    sprite_.setTextColor(kGreen, kBg);
    drawWrapped(page.lines[i], rows[i].x, rows[i].y, rows[i].w, rows[i].h - 2, kGreen);
  }
  drawText("tap a line to read full text", 8, 159, 624, kDim, 0.9f, false);
}

void FamiliarUi::drawModal() {
  drawText(agentResponseModal_ ? "LATEST RESPONSE" : state_.modal.title,
           8, 27, 624, kInk, 1.2f);
  sprite_.drawFastHLine(8, 46, 624, kDim);
  if (modalContent().length()) {
    if (agentResponseModal_) drawMarkdownResponse();
    else drawWrapped(modalContent(), 8, 51, 624, 101, kGreen, 1.0f, modalScroll_);
    drawText(agentResponseModal_ && agentResponseModalTruncated_
                 ? "response shortened - swipe up/down, tap to return"
                 : "swipe up/down to read - tap to return",
             8, 159, 624, kDim, 0.9f, false);
  } else {
    sprite_.setFont(&fonts::efontJA_12);
    sprite_.setTextWrap(false);
    sprite_.setTextSize(1.0f);
    const int16_t lineHeight = static_cast<int16_t>(sprite_.fontHeight()) + 1;
    constexpr int16_t contentBottom = 152;
    int16_t top = 50;
    for (uint8_t i = 0; i < 3; ++i) {
      if (!state_.modal.lines[i].length()) continue;
      const uint16_t lines = std::max<uint16_t>(
          1, countWrappedLines(sprite_, state_.modal.lines[i], 624));
      uint8_t laterRows = 0;
      for (uint8_t next = i + 1; next < 3; ++next)
        if (state_.modal.lines[next].length()) ++laterRows;
      const int16_t reserved = static_cast<int16_t>(laterRows * (lineHeight + 2));
      const int16_t lineSlots = static_cast<int16_t>(
          (contentBottom - top - reserved) / lineHeight);
      if (lineSlots <= 0) break;
      const uint16_t shownLines = std::min<uint16_t>(lines, lineSlots);
      const int16_t height = static_cast<int16_t>(shownLines * lineHeight);
      drawWrapped(state_.modal.lines[i], 8, top, 624, height, kGreen);
      top = static_cast<int16_t>(top + height + 2);
    }
    drawText("tap to return", 8, 159, 624, kDim, 0.9f, false);
  }
}

void FamiliarUi::ensureResponseParsed() {
  uint32_t hash = 2166136261u;
  for (uint16_t i = 0; i < state_.modalBody.length(); ++i) {
    hash ^= static_cast<uint8_t>(state_.modalBody[i]);
    hash *= 16777619u;
  }
  hash ^= state_.modalBody.length();
  if (responseParseReady_ && hash == parsedResponseHash_) return;
  responseDocument_.clear();
  responseParseReady_ = markdown::parse(
      std::string_view(state_.modalBody.c_str(), state_.modalBody.length()),
      responseDocument_);
  parsedResponseHash_ = hash;
}

void FamiliarUi::drawMarkdownResponse() {
  ensureResponseParsed();
  if (!responseParseReady_) {
    drawWrapped(state_.modalBody, 8, 51, 624, 101, kGreen, 1.0f, modalScroll_);
    return;
  }
  constexpr int16_t kX = 8, kY = 51, kWidth = 624, kHeight = 101;
  sprite_.setFont(&fonts::efontJA_12);
  sprite_.setTextWrap(false);
  sprite_.setTextSize(1.0f);
  const int16_t lineHeight = static_cast<int16_t>(sprite_.fontHeight()) + 1;
  const uint16_t visibleLines = static_cast<uint16_t>(std::max<int16_t>(1, kHeight / lineHeight));

  for (uint8_t pass = 0; pass < 2; ++pass) {
    uint16_t lineNumber = 0;
    uint16_t totalLines = 0;
    bool hasBlock = false;
    bool atLineStart = true;
    bool currentCodeBlock = false;
    uint8_t currentHeadingLevel = 0;
    int16_t indentX = kX;
    int16_t cursorX = kX;
    int16_t runX = kX;
    int16_t runWidth = 0;
    uint16_t runLine = 0;
    uint8_t runStyle = markdown::Plain;
    bool runActive = false;
    String run;

    auto lineVisible = [&](uint16_t number) {
      return number >= modalScroll_ && number < modalScroll_ + visibleLines;
    };
    auto lineY = [&](uint16_t number) {
      return static_cast<int16_t>(kY + (number - modalScroll_) * lineHeight);
    };
    auto fontFor = [](uint8_t style) -> const lgfx::IFont* {
      const bool bold = (style & markdown::Bold) != 0;
      const bool italic = (style & markdown::Italic) != 0;
      if (bold && italic) return &fonts::efontJA_12_bi;
      if (bold) return &fonts::efontJA_12_b;
      if (italic) return &fonts::efontJA_12_i;
      return &fonts::efontJA_12;
    };
    auto flushRun = [&]() {
      if (!runActive || pass == 0 || !lineVisible(runLine)) {
        run = ""; runActive = false; runWidth = 0;
        return;
      }
      const int16_t y = lineY(runLine);
      const bool inlineCode = (runStyle & markdown::InlineCode) != 0;
      const bool link = (runStyle & markdown::Link) != 0;
      uint16_t foreground = currentCodeBlock || inlineCode ? kCyan : kGreen;
      if (link) foreground = kCyan;
      if (currentHeadingLevel) foreground = kAmber;
      if (inlineCode && !currentCodeBlock)
        sprite_.fillRoundRect(runX, y, runWidth, lineHeight, 2, kPanel);
      sprite_.setFont(fontFor(runStyle | (currentHeadingLevel ? markdown::Bold : markdown::Plain)));
      sprite_.setTextWrap(false);
      sprite_.setTextSize(1.0f);
      const uint16_t background = inlineCode || currentCodeBlock ? kPanel : kBg;
      emoji_text::draw(sprite_, textView(run), runX, y, foreground, background);
      if (link) sprite_.drawFastHLine(runX, y + lineHeight - 2, runWidth, kCyan);
      run = ""; runActive = false; runWidth = 0;
    };
    auto beginLine = [&]() {
      cursorX = indentX;
      atLineStart = true;
      if (pass == 1 && currentCodeBlock && lineVisible(lineNumber))
        sprite_.fillRoundRect(kX, lineY(lineNumber), kWidth, lineHeight, 2, kPanel);
    };
    auto nextLine = [&]() {
      flushRun();
      ++lineNumber;
      beginLine();
    };

    for (const markdown::Block& block : responseDocument_.blocks) {
      if (block.kind == markdown::BlockKind::Blank || block.spanCount == 0) continue;
      if (hasBlock && !atLineStart) {
        flushRun();
        ++lineNumber;
        cursorX = kX;
        atLineStart = true;
      }
      currentCodeBlock = block.kind == markdown::BlockKind::CodeBlock;
      currentHeadingLevel = block.kind == markdown::BlockKind::Heading ? block.headingLevel : 0;
      indentX = kX;
      if (block.kind == markdown::BlockKind::BulletItem ||
          block.kind == markdown::BlockKind::OrderedItem) {
        char marker[16];
        if (block.kind == markdown::BlockKind::OrderedItem)
          snprintf(marker, sizeof(marker), "%u. ", block.listNumber);
        else snprintf(marker, sizeof(marker), "- ");
        const String prefix(marker);
        sprite_.setFont(&fonts::efontJA_12_b);
        sprite_.setTextSize(1.0f);
        const int16_t prefixWidth = static_cast<int16_t>(sprite_.textWidth(prefix));
        if (pass == 1 && lineVisible(lineNumber)) {
          const int16_t y = lineY(lineNumber);
          sprite_.setTextColor(kAmber, kBg);
          sprite_.setCursor(kX, y);
          sprite_.print(prefix);
        }
        indentX = static_cast<int16_t>(kX + prefixWidth);
      }
      beginLine();

      for (uint16_t spanIndex = block.firstSpan;
           spanIndex < block.firstSpan + block.spanCount; ++spanIndex) {
        const markdown::Span& span = responseDocument_.spans[spanIndex];
        const std::string_view spanText(responseDocument_.text.data() + span.textBegin,
                                        span.textLength);
        for (size_t offset = 0; offset < spanText.size();) {
          size_t tokenOffset = offset;
          emoji_text::Token token;
          if (!emoji_text::nextToken(spanText, tokenOffset, token)) break;
          const size_t next = token.end;
          const std::string glyphBytes(spanText.substr(offset, next - offset));
          const String glyph(glyphBytes.c_str());
          offset = next;
          if (glyph == "\n") { nextLine(); continue; }
          if (atLineStart && glyph == " " && !currentCodeBlock) continue;
          const uint8_t style = static_cast<uint8_t>(span.style |
              (currentHeadingLevel ? markdown::Bold : markdown::Plain));
          sprite_.setFont(fontFor(style));
          sprite_.setTextSize(1.0f);
          const int16_t glyphWidth = emoji_text::tokenWidth(sprite_, spanText, token);
          if (cursorX + glyphWidth > kX + kWidth && cursorX > indentX) nextLine();
          if (atLineStart && glyph == " " && !currentCodeBlock) continue;
          if (!runActive || runStyle != span.style || runLine != lineNumber ||
              runX + runWidth != cursorX) {
            flushRun();
            runX = cursorX;
            runLine = lineNumber;
            runStyle = span.style;
            runActive = true;
          }
          run += glyph;
          runWidth = static_cast<int16_t>(runWidth + glyphWidth);
          cursorX = static_cast<int16_t>(cursorX + glyphWidth);
          atLineStart = false;
        }
      }
      flushRun();
      hasBlock = true;
    }
    flushRun();
    totalLines = hasBlock ? static_cast<uint16_t>(lineNumber + 1) : 0;
    if (pass == 0) {
      const uint16_t maxScroll = totalLines > visibleLines ? totalLines - visibleLines : 0;
      if (modalScroll_ > maxScroll) modalScroll_ = maxScroll;
    }
  }
}

void FamiliarUi::drawText(const String& source, int16_t x, int16_t y, int16_t width,
                          uint16_t color, float size, bool ellipsis, uint16_t background) {
  if (width <= 0) return;
  const String text = plainText(source);
  sprite_.setFont(&fonts::efontJA_12);
  sprite_.setTextWrap(false);
  sprite_.setTextSize(size);
  String fitted;
  const String suffix = "...";
  uint16_t firstLineEnd = 0;
  while (firstLineEnd < text.length() && text[firstLineEnd] != '\n') ++firstLineEnd;
  const String firstLine = text.substring(0, firstLineEnd);
  const bool hasRemainder = firstLineEnd < text.length();
  if (!hasRemainder && emoji_text::measure(sprite_, textView(firstLine), size) <= width) {
    fitted = firstLine;
  } else {
    const bool useSuffix = ellipsis && emoji_text::measure(sprite_, textView(suffix), size) <= width;
    uint16_t offset = 0;
    while (offset < firstLine.length()) {
      size_t tokenOffset = offset;
      emoji_text::Token token;
      if (!emoji_text::nextToken(textView(firstLine), tokenOffset, token)) break;
      const uint16_t next = static_cast<uint16_t>(token.end);
      const String glyph = firstLine.substring(offset, next);
      const String candidate = fitted + glyph;
      const String visible = useSuffix ? candidate + suffix : candidate;
      if (emoji_text::measure(sprite_, textView(visible), size) > width) break;
      fitted = candidate;
      offset = next;
    }
    if (useSuffix) fitted += suffix;
  }
  emoji_text::draw(sprite_, textView(fitted), x, y, color, background, size);
}

void FamiliarUi::drawCenteredText(const String& source, const Rect& rect,
                                  uint16_t color, float size, uint16_t background) {
  const String text = plainText(source);
  sprite_.setFont(&fonts::efontJA_12);
  sprite_.setTextWrap(false);
  sprite_.setTextSize(size);
  const int16_t textWidth = emoji_text::measure(sprite_, textView(text), size);
  const int16_t textHeight = static_cast<int16_t>(sprite_.fontHeight());
  const int16_t x = static_cast<int16_t>(rect.x + (rect.w - min<int16_t>(rect.w, textWidth)) / 2);
  const int16_t y = static_cast<int16_t>(rect.y + (rect.h - textHeight) / 2);
  drawText(text, x, y, rect.w, color, size, true, background);
}

void FamiliarUi::drawWrapped(const String& source, int16_t x, int16_t y, int16_t width,
                             int16_t height, uint16_t color, float scale,
                             uint16_t skipLines) {
  if (width <= 0 || height <= 0) return;
  const String text = plainText(source);
  sprite_.setFont(&fonts::efontJA_12);
  sprite_.setTextWrap(false);
  sprite_.setTextSize(scale);
  const int16_t lineHeight = static_cast<int16_t>(sprite_.fontHeight()) + 1;
  const int16_t maxLines = std::max<int16_t>(1, height / std::max<int16_t>(1, lineHeight));
  constexpr uint8_t kStoredLines = 16;
  std::array<String, kStoredLines> visible{};
  uint16_t totalLines = 0;
  uint16_t offset = 0;
  while (offset < text.length()) {
    if (text[offset] == '\n') {
      if (totalLines >= skipLines && totalLines < skipLines + maxLines &&
          totalLines - skipLines < kStoredLines)
        visible[totalLines - skipLines] = "";
      ++totalLines;
      ++offset;
      continue;
    }
    while (offset < text.length() && text[offset] == ' ') ++offset;
    if (offset >= text.length()) break;

    const uint16_t start = offset;
    uint16_t fitEnd = start;
    uint16_t lastSpace = UINT16_MAX;
    uint16_t scan = start;
    while (scan < text.length() && text[scan] != '\n') {
      size_t tokenOffset = scan;
      emoji_text::Token token;
      if (!emoji_text::nextToken(textView(text), tokenOffset, token)) break;
      const uint16_t next = static_cast<uint16_t>(token.end);
      const String glyph = text.substring(scan, next);
      if (emoji_text::measure(sprite_, textView(text).substr(start, next - start), scale) > width) break;
      if (glyph == " ") lastSpace = scan;
      fitEnd = next;
      scan = next;
    }
    if (fitEnd == start) {
      size_t tokenOffset = start;
      emoji_text::Token token;
      if (emoji_text::nextToken(textView(text), tokenOffset, token))
        fitEnd = static_cast<uint16_t>(token.end);
    }
    uint16_t lineEnd = fitEnd;
    uint16_t nextOffset = fitEnd;
    if (scan < text.length() && text[scan] != '\n' && lastSpace != UINT16_MAX && lastSpace > start) {
      lineEnd = lastSpace;
      nextOffset = static_cast<uint16_t>(lastSpace + 1);
    }
    String line = text.substring(start, lineEnd);
    if (totalLines >= skipLines && totalLines < skipLines + maxLines &&
        totalLines - skipLines < kStoredLines)
      visible[totalLines - skipLines] = line;
    ++totalLines;
    offset = nextOffset;
    if (offset < text.length() && text[offset] == '\n') ++offset;
    while (offset < text.length() && text[offset] == ' ') ++offset;
  }

  const uint16_t visibleLines = min<uint16_t>(maxLines, kStoredLines);
  const bool moreBelow = totalLines > skipLines + visibleLines;
  for (uint16_t i = 0; i < visibleLines && skipLines + i < totalLines; ++i) {
    String line = visible[i];
    const bool finalVisible = moreBelow && i + 1 == visibleLines;
    if (finalVisible) drawText(line + "...", x, y + i * lineHeight, width, color, scale, true);
    else drawText(line, x, y + i * lineHeight, width, color, scale, false);
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

const String& FamiliarUi::modalContent() const {
  return state_.modalBody;
}

void FamiliarUi::openAgentResponseModal() {
  state_.modalReturn = state_.page;
  state_.modalActive = true;
  agentResponseModal_ = true;
  state_.modalBody = state_.agentResponseMarkdown;
  agentResponseModalTruncated_ = state_.agentResponseMarkdownTruncated;
  responseParseReady_ = false;
  modalScroll_ = 0;
}

void FamiliarUi::handleTap(int16_t x, int16_t y) {
  emit(String("{\"cmd\":\"touch\",\"x\":") + x + ",\"y\":" + y + "}");
  if (state_.toastUntilMs) { state_.toastUntilMs = 0; state_.toast = ""; }
  if (state_.modalActive) {
    state_.modalActive = false;
    state_.modalBody = "";
    agentResponseModal_ = false;
    agentResponseModalTruncated_ = false;
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
        agentResponseModal_ = false;
        agentResponseModalTruncated_ = false;
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
    if (state_.agentResponseMarkdown.length() && x >= 164 && y >= 111)
      openAgentResponseModal();
    else emit("{\"cmd\":\"stats\"}");
  } else if (state_.page == protocol::Page::Messages) {
    const bool history = state_.historyOffset > 0 && state_.historyCount > 0;
    const uint8_t count = history ? state_.historyCount : state_.entryCount;
    const int16_t rowHeight = messageRowHeight(count);
    if (!history && state_.agentResponseMarkdown.length() && y >= 48 &&
        y < 48 + rowHeight && x >= kContentX && x < kContentRight - kContentX)
      openAgentResponseModal();
  } else if (state_.page == protocol::Page::Fleet ||
             state_.page == protocol::Page::Cron ||
             state_.page == protocol::Page::Network) {
    const protocol::HostPage* page = state_.page == protocol::Page::Fleet
        ? &state_.hostPages[2]
        : state_.page == protocol::Page::Cron ? &state_.hostPages[0]
                                               : &state_.hostPages[1];
    if (page->set) {
      const auto rows = hostRowRects(sprite_, *page);
      bool opened = false;
      for (uint8_t row = 0; row < rows.size(); ++row) {
        if (rows[row].contains(x, y)) {
          state_.modal.title = page->title;
          state_.modalBody = page->lines[row];
          state_.modalReturn = state_.page;
          state_.modalActive = true;
          modalScroll_ = 0;
          agentResponseModal_ = false;
          agentResponseModalTruncated_ = false;
          opened = true;
          break;
        }
      }
      if (!opened && state_.page == protocol::Page::Network) emit("{\"cmd\":\"net\"}");
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
    if (modalContent().length()) {
      modalScroll_ = dy < 0 ? modalScroll_ + 3 : modalScroll_ > 3 ? modalScroll_ - 3 : 0;
      state_.dirty = true;
    }
    return;
  }
  if (state_.modalActive) {
    state_.modalActive = false;
    state_.modalBody = "";
    agentResponseModal_ = false;
    agentResponseModalTruncated_ = false;
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
