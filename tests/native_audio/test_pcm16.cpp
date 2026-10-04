#include "../../src/audio/pcm16.h"
#include "../../src/audio/http_pcm.h"

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

  using audio::HttpPcmCompletion;
  // A peer that has closed may still leave its final PCM bytes buffered.
  assert(audio::httpPcmHasInput(false, 2, 4, 2));
  assert(!audio::httpPcmHasInput(false, 0, 4, 2));
  assert(!audio::httpPcmHasInput(true, 2, 4, 4));
  assert(audio::finishHttpPcm(true, 4, 4, false, 0) == HttpPcmCompletion::Complete);
  assert(audio::finishHttpPcm(true, 2, 4, false, 0) == HttpPcmCompletion::Truncated);
  assert(audio::finishHttpPcm(false, 3, 3, false, 0) == HttpPcmCompletion::Malformed);
  assert(audio::finishHttpPcm(true, 0, -1, false, 0) == HttpPcmCompletion::Empty);
  assert(audio::finishHttpPcm(true, 4, -1, true, 0) == HttpPcmCompletion::StillConnected);
  assert(audio::finishHttpPcm(true, 4, -1, false, 0) == HttpPcmCompletion::Complete);

  using audio::PlaybackStop;
  assert(audio::playbackStop(true, 1, 1, 65000, 3000) == PlaybackStop::Quiet);
  assert(audio::playbackStop(false, 65000, 1, 65000, 3000) == PlaybackStop::TaskTimeout);
  assert(audio::playbackStop(false, 1, 3000, 65000, 3000) == PlaybackStop::IdleTimeout);
  assert(audio::playbackStop(false, 64999, 2999, 65000, 3000) == PlaybackStop::None);

  // A failed/truncated response cannot retain decoder state in the next request.
  audio::Pcm16LeDecoder recovered;
  assert(recovered.push(pcm, sizeof(pcm), samples, 4, written));
  assert(written == 4 && recovered.complete());
  return 0;
}
