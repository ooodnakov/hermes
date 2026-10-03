#include "../../src/protocol/speech_dispatch.h"

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
}
