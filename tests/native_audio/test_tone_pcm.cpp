#include "../../src/audio/tone_pcm.h"

#include <cassert>
#include <cstdint>

int main() {
  assert(audio::kAlertToneProfile.frequencyHz == 880);
  assert(audio::kAlertToneProfile.durationMs == 200);

  int16_t priming[128];
  audio::fillSilenceSamples(priming, 128);
  for (const int16_t sample : priming) assert(sample == 0);

  int16_t firstToneChunk[128];
  audio::fillSquareToneSamples(firstToneChunk, 128, 0, 560, 9);
  assert(firstToneChunk[0] == 0);
  assert(firstToneChunk[1] == -75);
  assert(firstToneChunk[9] == 675);
  assert(firstToneChunk[24] == -1800);
  assert(firstToneChunk[0] != priming[0] || firstToneChunk[1] != priming[1]);

  int16_t endingTone[24];
  audio::fillSquareToneSamples(endingTone, 24, 536, 560, 9);
  assert(endingTone[23] == -75);
  return 0;
}
