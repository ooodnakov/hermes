#include "../../src/network/transport_policy.h"

#include <cassert>

int main() {
  using network::shouldConnectTcp;
  using network::isIpv4Literal;
  using network::HostActivity;
  using network::claimsUsbPriority;
  assert(!shouldConnectTcp(false, false, true, false));  // Wi-Fi offline
  assert(!shouldConnectTcp(true, true, true, false));     // USB host has priority
  assert(!shouldConnectTcp(true, false, false, false));  // no configured host
  assert(!shouldConnectTcp(true, false, true, true));    // already connected
  assert(shouldConnectTcp(true, false, true, false));    // dial home

  assert(isIpv4Literal("192.0.2.123"));
  assert(isIpv4Literal("0.0.0.0"));
  assert(!isIpv4Literal(nullptr));
  assert(!isIpv4Literal(""));
  assert(!isIpv4Literal("gateway.local"));
  assert(!isIpv4Literal("192.168.1"));
  assert(!isIpv4Literal("192.168.1.256"));
  assert(!isIpv4Literal("192.168..1"));
  assert(!isIpv4Literal("192.168.1.1:8767"));

  assert(!claimsUsbPriority(HostActivity::LocalCommand));
  assert(!claimsUsbPriority(HostActivity::UnknownJson));
  assert(claimsUsbPriority(HostActivity::Ping));
  assert(claimsUsbPriority(HostActivity::Clear));
  assert(claimsUsbPriority(HostActivity::Config));
  assert(claimsUsbPriority(HostActivity::ProtocolFrame));
}
