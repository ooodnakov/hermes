#pragma once

#include <cstdint>

constexpr uint8_t ADC_11db = 0;
inline uint32_t arduinoMillis = 0;
inline uint32_t analogReadCount = 0;
inline uint32_t millis() { return ++arduinoMillis; }
inline void delay(uint32_t) {}
inline void delayMicroseconds(uint32_t) {}
inline uint32_t analogReadMilliVolts(int) { ++analogReadCount; return 1200; }
inline void analogSetPinAttenuation(int, uint8_t) {}
