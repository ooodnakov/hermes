#pragma once

#include <Arduino.h>

#if !defined(ESPHERM_BOARD_WAVESHARE_349_V1)
#error "The V1 diagnostic source must only be built by the V1 PlatformIO environment"
#endif

namespace board {
constexpr char kId[] = "waveshare-esp32-s3-touch-lcd-349-v1";
constexpr int kPowerKey = 16;
constexpr int kBootKey = 0;

constexpr int kPeripheralSda = 47;
constexpr int kPeripheralScl = 48;
constexpr int kTouchSda = 17;
constexpr int kTouchScl = 18;
constexpr uint8_t kTouchAddress = 0x3B;
constexpr uint8_t kExpanderAddress = 0x20;
constexpr uint8_t kExpanderHoldBit = 1U << 6;

constexpr int kDisplayCs = 9;
constexpr int kDisplayClock = 10;
constexpr int kDisplayData0 = 11;
constexpr int kDisplayData1 = 12;
constexpr int kDisplayData2 = 13;
constexpr int kDisplayData3 = 14;
constexpr int kDisplayReset = 21;
constexpr int kBacklight = 8;

constexpr int kSdClock = 41;
constexpr int kSdCommand = 39;
constexpr int kSdData0 = 40;
constexpr int kBatteryAdc = 4;
constexpr int kPowerHoldExpanderPin = 6;
}  // namespace board
