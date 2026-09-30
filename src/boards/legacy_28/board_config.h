#pragma once

// Pin identity for the preserved 2.8-inch firmware environment. Its current
// implementation remains in src/main.cpp and is only selected by that env.
namespace board_legacy_28 {
constexpr int kPowerHold = 7;
constexpr int kSdEnable = 21;
constexpr int kPeripheralSda = 11;
constexpr int kPeripheralScl = 10;
constexpr int kPowerKey = 6;
}  // namespace board_legacy_28
