#pragma once

#include <cstdint>

constexpr uint8_t ADC_11db = 0;
inline uint32_t millis() { static uint32_t now = 0; return ++now; }
inline void delay(uint32_t) {}
inline void delayMicroseconds(uint32_t) {}
inline uint32_t analogReadMilliVolts(int) { return 1200; }
inline void analogSetPinAttenuation(int, uint8_t) {}
