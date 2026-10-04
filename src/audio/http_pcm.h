#pragma once

#include <cstddef>

namespace audio {

// A server may close its socket as soon as it sends the response.  Arduino's
// WiFiClient can still contain unread body bytes after connected() turns false.
inline bool httpPcmHasInput(bool connected, std::size_t available, int contentLength,
                            std::size_t received) {
  const bool bodyIncomplete = contentLength < 0 || received < static_cast<std::size_t>(contentLength);
  return bodyIncomplete && (connected || available != 0);
}

enum class HttpPcmCompletion {
  Complete,
  Empty,
  Malformed,
  Truncated,
  StillConnected,
};

enum class PlaybackStop {
  None,
  Quiet,
  TaskTimeout,
  IdleTimeout,
};

inline PlaybackStop playbackStop(bool quiet, unsigned long elapsed,
                                 unsigned long idleElapsed,
                                 unsigned long taskLimit,
                                 unsigned long idleLimit) {
  if (quiet) return PlaybackStop::Quiet;
  if (elapsed >= taskLimit) return PlaybackStop::TaskTimeout;
  if (idleElapsed >= idleLimit) return PlaybackStop::IdleTimeout;
  return PlaybackStop::None;
}

inline HttpPcmCompletion finishHttpPcm(bool decoderComplete, std::size_t received,
                                       int contentLength, bool connected,
                                       std::size_t available) {
  if (!decoderComplete) return HttpPcmCompletion::Malformed;
  if (received == 0) return HttpPcmCompletion::Empty;
  if (contentLength >= 0) {
    return received == static_cast<std::size_t>(contentLength)
        ? HttpPcmCompletion::Complete : HttpPcmCompletion::Truncated;
  }
  return connected || available != 0
      ? HttpPcmCompletion::StillConnected : HttpPcmCompletion::Complete;
}

}  // namespace audio
