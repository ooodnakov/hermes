#pragma once

#include <cstddef>
#include <cstdint>

namespace audio {

// Incrementally decode host-provided signed 16-bit little-endian mono PCM
// without relying on native byte order, alignment, or HTTP chunk boundaries.
class Pcm16LeDecoder {
 public:
  bool push(const std::uint8_t* input, std::size_t bytes, std::int16_t* output,
            std::size_t capacity, std::size_t& written) {
    written = 0;
    if ((!input && bytes) || (!output && bytes)) return false;
    const std::size_t sampleCount = (bytes + (hasPending_ ? 1U : 0U)) / 2U;
    if (sampleCount > capacity) return false;
    for (std::size_t i = 0; i < bytes; ++i) {
      if (hasPending_) {
        writeSample(pending_, input[i], output[written++]);
        hasPending_ = false;
      } else {
        pending_ = input[i];
        hasPending_ = true;
      }
    }
    return true;
  }

  bool complete() const { return !hasPending_; }

 private:
  static void writeSample(std::uint8_t low, std::uint8_t high, std::int16_t& output) {
    const std::uint16_t bits = static_cast<std::uint16_t>(low) |
        (static_cast<std::uint16_t>(high) << 8);
    const std::int32_t signedValue = bits <= 0x7FFFU
        ? static_cast<std::int32_t>(bits)
        : static_cast<std::int32_t>(bits) - 0x10000;
    output = static_cast<std::int16_t>(signedValue);
  }

  std::uint8_t pending_ = 0;
  bool hasPending_ = false;
};

}  // namespace audio
