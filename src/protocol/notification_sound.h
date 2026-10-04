#pragma once

#include <cstdint>
#include <cstring>

namespace protocol {

enum class NotificationSound : uint8_t {
  None,
  Alert,
  Ack,
  Tap,
};

inline NotificationSound notificationSoundForFrame(const char* type,
                                                   const char* sound,
                                                   bool soundProvided,
                                                   bool speechAvailable) {
  if (!type || strcmp(type, "notify") || speechAvailable) return NotificationSound::None;
  if (!soundProvided) return NotificationSound::Alert;
  if (!sound) return NotificationSound::None;
  if (!strcmp(sound, "alert")) return NotificationSound::Alert;
  if (!strcmp(sound, "ack")) return NotificationSound::Ack;
  if (!strcmp(sound, "tap")) return NotificationSound::Tap;
  return NotificationSound::None;
}

}  // namespace protocol
