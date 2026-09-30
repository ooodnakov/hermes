#pragma once

#include <Arduino.h>

namespace protocol {

enum class Page : uint8_t { Face, Messages, Operations, Fleet, Cron, Network, Device, Count };

struct DeckAction {
  String label;
  uint8_t color = 0;
  bool confirm = false;
};

struct Approval {
  bool active = false;
  String id;
  String text;
  String detail;
  String choices[3];
  uint8_t choiceCount = 0;
};

struct HostPage {
  String title;
  String lines[3];
  bool set = false;
};

struct UiState {
  Page page = Page::Face;
  Page modalReturn = Page::Face;
  bool connected = false;
  uint32_t lastSeenMs = 0;
  int total = 0, running = 0, waiting = 0;
  uint32_t tokensToday = 0, toolsToday = 0;
  String jobState = "idle", jobLabel = "Status brief", message = "Awaiting Hermes stream";
  String entries[5];
  uint8_t entryCount = 0;
  String history[5];
  uint8_t historyCount = 0;
  uint16_t historyOffset = 0, historyTotal = 0;
  DeckAction deck[6];
  uint8_t deckCount = 0;
  int8_t armedDeck = -1, runningDeck = -1;
  uint32_t armedUntilMs = 0;
  Approval approval;
  HostPage hostPages[3];
  HostPage modal;
  String modalBody;
  bool modalActive = false;
  String toast;
  uint32_t toastUntilMs = 0;
  // Board diagnostics are updated by the board integration, not by JSON parsing.
  String sdStatus, touchStatus, batteryStatus, rtcStatus, wifiStatus, networkStatus;
  String audioStatus, motionStatus;
  String linkStatus = "USB --";
  uint32_t freeHeap = 0;
  uint8_t rotation = 0;
  bool bleConnected = false, wifiConnected = false;
  bool dirty = true;
};

// Applies the established newline-JSON host frame contract. Unknown/malformed
// frames do not mark the host live. Side-effect commands (config/say) remain
// the transport/board integration's responsibility.
bool applyJsonFrame(UiState& state, const String& line, uint32_t nowMs);

}  // namespace protocol
