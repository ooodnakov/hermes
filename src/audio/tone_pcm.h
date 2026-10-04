#pragma once

#include <cstddef>
#include <cstdint>

namespace audio {

struct ToneProfile {
  uint16_t frequencyHz;
  uint16_t durationMs;
};

constexpr uint16_t kAlertToneFrequencyHz = 880;
constexpr uint16_t kAlertToneDurationMs = 200;
constexpr ToneProfile kAlertToneProfile{
    kAlertToneFrequencyHz, kAlertToneDurationMs};

inline void fillSilenceSamples(int16_t* samples, size_t count) {
  if (!samples) return;
  for (size_t i = 0; i < count; ++i) samples[i] = 0;
}

inline void fillSquareToneSamples(int16_t* samples, size_t count,
                                  uint32_t firstSample, uint32_t totalSamples,
                                  uint32_t halfPeriod) {
  if (!samples || halfPeriod == 0) return;
  for (size_t i = 0; i < count; ++i) {
    const uint32_t sample = firstSample + static_cast<uint32_t>(i);
    int32_t amplitude = ((sample / halfPeriod) & 1U) ? 1800 : -1800;
    if (sample < 24) amplitude = amplitude * static_cast<int32_t>(sample) / 24;
    if (totalSamples - sample < 24) {
      amplitude = amplitude * static_cast<int32_t>(totalSamples - sample) / 24;
    }
    samples[i] = static_cast<int16_t>(amplitude);
  }
}

}  // namespace audio
