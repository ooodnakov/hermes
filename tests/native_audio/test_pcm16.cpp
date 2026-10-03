#include "../../src/audio/pcm16.h"

#include <cassert>
#include <cstdint>

int main() {
  const std::uint8_t pcm[] = {0x00, 0x00, 0xFF, 0x7F, 0x00, 0x80, 0x34, 0xF2};
  std::int16_t samples[4] = {};
  audio::Pcm16LeDecoder decoder;
  std::size_t written = 0;
  assert(decoder.push(pcm, 1, samples, 4, written) && written == 0);
  assert(!decoder.complete());
  assert(decoder.push(pcm + 1, 2, samples, 4, written) && written == 1);
  assert(samples[0] == 0);
  assert(decoder.push(pcm + 3, 3, samples + 1, 3, written) && written == 2);
  assert(samples[1] == 32767);
  assert(samples[2] == -32768);
  assert(decoder.push(pcm + 6, 2, samples + 3, 1, written) && written == 1);
  assert(decoder.complete());
  assert(samples[0] == 0);
  assert(samples[3] == -3532);
  assert(!decoder.push(pcm, sizeof(pcm), samples, 3, written));
  audio::Pcm16LeDecoder truncated;
  assert(truncated.push(pcm, 1, samples, 4, written) && written == 0);
  assert(!truncated.complete());
  assert(!truncated.push(nullptr, 1, samples, 4, written));
  audio::Pcm16LeDecoder capacityFailure;
  const std::uint8_t low = 0xFF;
  const std::uint8_t high = 0x7F;
  assert(capacityFailure.push(&low, 1, samples, 1, written) && written == 0);
  const std::uint8_t twoBytes[] = {0x7F, 0x00};
  assert(!capacityFailure.push(twoBytes, sizeof(twoBytes), samples, 0, written));
  assert(!capacityFailure.complete());
  assert(capacityFailure.push(&high, 1, samples, 1, written) && written == 1);
  assert(samples[0] == 32767 && capacityFailure.complete());
  return 0;
}
