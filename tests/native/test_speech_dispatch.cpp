#include "../../src/protocol/speech_dispatch.h"
#include "../../src/protocol/notification_sound.h"

#include <cassert>

int main() {
  const char* currentUrl = "http://host/current.pcm";
  const char* legacyUrl = "http://host/legacy.pcm";
  const char* notifyUrl = "http://host/notify.pcm";

  assert(protocol::speechUrlForFrame("say", currentUrl, legacyUrl) == currentUrl);
  assert(protocol::speechUrlForFrame("say", "", legacyUrl) == legacyUrl);
  assert(protocol::speechUrlForFrame("notify", currentUrl, notifyUrl) == notifyUrl);
  assert(protocol::speechUrlForFrame("notify", currentUrl, "") == nullptr);
  assert(protocol::speechUrlForFrame("state", currentUrl, notifyUrl) == nullptr);
  assert(protocol::speechUrlForFrame(nullptr, currentUrl, notifyUrl) == nullptr);
  assert(protocol::speechUrlForFrame("say", nullptr, "") == nullptr);

  using protocol::NotificationSound;
  assert(protocol::notificationSoundForFrame("notify", "alert", true, false) ==
         NotificationSound::Alert);
  assert(protocol::notificationSoundForFrame("notify", "ack", true, false) ==
         NotificationSound::Ack);
  assert(protocol::notificationSoundForFrame("notify", "tap", true, false) ==
         NotificationSound::Tap);
  assert(protocol::notificationSoundForFrame("notify", nullptr, false, false) ==
         NotificationSound::Alert);
  assert(protocol::notificationSoundForFrame("notify", "none", true, false) ==
         NotificationSound::None);
  assert(protocol::notificationSoundForFrame("notify", "invalid", true, false) ==
         NotificationSound::None);
  assert(protocol::notificationSoundForFrame("notify", nullptr, true, false) ==
         NotificationSound::None);
  assert(protocol::notificationSoundForFrame("say", "alert", true, false) ==
         NotificationSound::None);
  assert(protocol::notificationSoundForFrame("notify", "alert", true, true) ==
         NotificationSound::None);
  assert(protocol::notificationSoundForFrame(nullptr, "alert", true, false) ==
         NotificationSound::None);
}
