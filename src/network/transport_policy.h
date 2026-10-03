#pragma once

namespace network {

enum class HostActivity : unsigned char {
  LocalCommand,
  UnknownJson,
  Ping,
  Clear,
  Config,
  ProtocolFrame,
};

constexpr bool claimsUsbPriority(HostActivity activity) {
  return activity == HostActivity::Ping || activity == HostActivity::Clear ||
      activity == HostActivity::Config || activity == HostActivity::ProtocolFrame;
}

inline bool isIpv4Literal(const char* text) {
  if (!text || !*text) return false;
  unsigned octet = 0;
  unsigned digits = 0;
  unsigned octets = 0;
  for (const char* p = text; ; ++p) {
    const char c = *p;
    if (c >= '0' && c <= '9') {
      if (++digits > 3) return false;
      octet = octet * 10 + static_cast<unsigned>(c - '0');
      if (octet > 255) return false;
    } else if (c == '.' || c == '\0') {
      if (!digits || ++octets > 4) return false;
      if (c == '\0') return octets == 4;
      octet = digits = 0;
    } else {
      return false;
    }
  }
}

constexpr bool shouldConnectTcp(bool wifiReady, bool usbAlive,
                                bool hostConfigured, bool tcpConnected) {
  return wifiReady && !usbAlive && hostConfigured && !tcpConnected;
}

}  // namespace network
