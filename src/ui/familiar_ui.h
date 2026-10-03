#pragma once

#include <Arduino.h>
#include <LovyanGFX.hpp>

#include "../protocol/ui_state.h"
#include "markdown.h"

namespace ui {

struct Rect {
  int16_t x, y, w, h;
  bool contains(int16_t px, int16_t py) const {
    return px >= x && py >= y && px < x + w && py < y + h;
  }
};

class FamiliarUi {
 public:
  using Emit = void (*)(void* context, const String& newlineJson);
  using FacePainter = void (*)(lgfx::LGFX_Sprite& sprite, const String& group,
                               const Rect& artRect, uint32_t nowMs, void* context);

  FamiliarUi(lgfx::LGFX_Sprite& sprite, protocol::UiState& state,
             Emit emit, void* emitContext = nullptr);
  void setFacePainter(FacePainter painter, void* context = nullptr);
  void tick(uint32_t nowMs);
  void render(uint32_t nowMs);
  // Call on release with the original down point and final point. Invalid or
  // stale samples are ignored by the integration before they reach this API.
  void touchGesture(int16_t startX, int16_t startY, int16_t endX, int16_t endY,
                    uint32_t durationMs, uint32_t nowMs);

  static constexpr int16_t kWidth = 640, kHeight = 172;
  static constexpr int16_t kTabHeight = 24;
  static Rect tabRect(uint8_t index);
  static Rect deckRect(uint8_t index);
  static Rect allowRect();
  static Rect denyRect();
  static Rect approvalTextRect();
  static Rect artRect();

 private:
  lgfx::LGFX_Sprite& sprite_;
  protocol::UiState& state_;
  Emit emit_;
  void* emitContext_;
  FacePainter facePainter_ = nullptr;
  void* faceContext_ = nullptr;
  uint32_t nowMs_ = 0;
  uint16_t modalScroll_ = 0;
  bool agentResponseModal_ = false;
  uint32_t parsedResponseHash_ = 0;
  bool responseParseReady_ = false;
  bool agentResponseModalTruncated_ = false;
  markdown::Document responseDocument_;

  void emit(const String& json);
  void drawTabs(uint16_t accent);
  void drawPage();
  void drawFace();
  void drawMessages();
  void drawOperations();
  void drawHostPage(const protocol::HostPage& page, const char* fallback);
  void drawModal();
  void drawMarkdownResponse();
  void ensureResponseParsed();
  void drawText(const String& text, int16_t x, int16_t y, int16_t width,
                uint16_t color, float size = 1.0f, bool ellipsis = true,
                uint16_t background = 0x0000);
  void drawCenteredText(const String& text, const Rect& rect, uint16_t color,
                        float size = 1.0f, uint16_t background = 0x0000);
  void drawWrapped(const String& text, int16_t x, int16_t y, int16_t width,
                   int16_t height, uint16_t color, float scale = 1.0f,
                   uint16_t skipLines = 0);
  void selectPage(uint8_t index);
  void handleTap(int16_t x, int16_t y);
  void decideApproval(const char* decision);
  void openAgentResponseModal();
  const String& modalContent() const;
};

}  // namespace ui
