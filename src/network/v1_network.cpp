#include "v1_network.h"
#include "transport_policy.h"

#include <cstring>
#include <errno.h>
#include <lwip/sockets.h>

namespace network {
namespace {
constexpr uint32_t kWifiRetryMs = 10000;
constexpr uint32_t kTcpRetryMs = 10000;
constexpr uint32_t kTcpConnectTimeoutMs = 250;
constexpr size_t kMaxSsid = 32;
constexpr size_t kMaxPassword = 63;
constexpr size_t kMaxHost = 253;
constexpr size_t kMaxToken = 128;

void setStatus(char* target, size_t capacity, const char* value) {
  if (!capacity) return;
  strncpy(target, value, capacity - 1);
  target[capacity - 1] = '\0';
}

String boundedString(JsonVariantConst value, size_t maximum) {
  if (!value.is<const char*>()) return String();
  const char* raw = value.as<const char*>();
  if (!raw || strnlen(raw, maximum + 1) > maximum) return String();
  return String(raw);
}
}  // namespace

void V1Network::configure(JsonObjectConst wifi, JsonObjectConst host) {
  const String nextSsid = boundedString(wifi["ssid"], kMaxSsid);
  const String nextPassword = boundedString(wifi["password"], kMaxPassword);
  const String nextHost = boundedString(host["ip"], kMaxHost);
  const String nextToken = boundedString(host["token"], kMaxToken);
  const JsonVariantConst portValue = host["port"];
  uint16_t nextPort = 8767;
  bool tcpConfigValid = true;
  if (!portValue.isNull()) {
    if (!portValue.is<uint16_t>() || portValue.as<uint16_t>() == 0) {
      tcpConfigValid = false;
    } else {
      nextPort = portValue.as<uint16_t>();
    }
  }
  IPAddress nextHostAddress;
  if (nextHost.length() &&
      (!isIpv4Literal(nextHost.c_str()) || !nextHostAddress.fromString(nextHost))) {
    tcpConfigValid = false;
  }

  // Empty values in a partial update mean "leave the existing setting". A
  // config merge may update Wi-Fi without replacing the TCP host or token.
  const bool wifiChanged = (nextSsid.length() && nextSsid != ssid_) ||
      (!wifi["password"].isNull() && nextPassword != password_);
  const bool tcpChanged = tcpConfigValid && ((nextHost.length() && nextHost != host_) ||
      (nextToken.length() && nextToken != token_) || (!portValue.isNull() && nextPort != port_));
  if (nextSsid.length()) ssid_ = nextSsid;
  if (nextPassword.length() || !wifi["password"].isNull()) password_ = nextPassword;
  if (tcpConfigValid && nextHost.length()) {
    host_ = nextHost;
    hostAddress_ = nextHostAddress;
  }
  // Empty auth input must not silently downgrade an authenticated connection.
  if (tcpConfigValid && nextToken.length()) token_ = nextToken;
  if (tcpConfigValid && !portValue.isNull()) port_ = nextPort;
  if (tcpChanged && tcp_.connected()) {
    tcp_.stop();
    tcpWasConnected_ = false;
    resetTcpState();
  }
  if (wifiChanged && started_) startWifi();

  setStatus(wifiStatus_, sizeof(wifiStatus_), ssid_.length() ? "configured" : "no-config");
  setStatus(tcpStatus_, sizeof(tcpStatus_), !tcpConfigValid ? "invalid-config" :
      host_.length() ? "configured" : "not-configured");
}

void V1Network::begin() {
  if (started_) return;
  started_ = true;
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  if (ssid_.length()) startWifi();
}

void V1Network::startWifi() {
  if (!ssid_.length()) {
    setStatus(wifiStatus_, sizeof(wifiStatus_), "no-config");
    return;
  }
  WiFi.begin(ssid_.c_str(), password_.c_str());
  wifiAttempted_ = true;
  retryAt_ = millis() + kWifiRetryMs;
  setStatus(wifiStatus_, sizeof(wifiStatus_), "connecting");
}

bool V1Network::wifiReady() const { return WiFi.status() == WL_CONNECTED; }
bool V1Network::tcpConnected() { return tcp_.connected(); }
const char* V1Network::wifiStatus() const {
  if (wifiReady()) return "connected";
  return wifiStatus_;
}
const char* V1Network::tcpStatus() {
  if (tcp_.connected()) return "connected";
  return tcpStatus_;
}

void V1Network::maybeConnect(bool usbAlive, uint32_t now) {
  const bool connected = wifiReady();
  if (connected) {
    localIp_ = WiFi.localIP();
    if (!wifiWasReady_) setStatus(wifiStatus_, sizeof(wifiStatus_), "connected");
    wifiWasReady_ = true;
  } else if (wifiWasReady_) {
    wifiWasReady_ = false;
    setStatus(wifiStatus_, sizeof(wifiStatus_), "disconnected");
    if (tcp_.connected()) tcp_.stop();
  } else if (ssid_.length() && static_cast<int32_t>(now - retryAt_) >= 0) {
    startWifi();
  }
  if (tcpWasConnected_ && !tcp_.connected()) {
    ++tcpDisconnects_;
    tcpWasConnected_ = false;
    setStatus(tcpStatus_, sizeof(tcpStatus_), "disconnected");
    resetTcpState();
  }
  if (tcp_.connected() && usbAlive) {
    tcp_.stop();
    ++tcpDisconnects_;
    tcpWasConnected_ = false;
    setStatus(tcpStatus_, sizeof(tcpStatus_), "usb-priority");
    resetTcpState();
  }
  if (!shouldConnectTcp(connected, usbAlive, host_.length() > 0, tcp_.connected()) ||
      static_cast<int32_t>(now - lastTcpAttemptAt_) < static_cast<int32_t>(kTcpRetryMs)) return;
  lastTcpAttemptAt_ = now;
  tcp_.setTimeout(kTcpConnectTimeoutMs);
  if (!tcp_.connect(hostAddress_, port_, kTcpConnectTimeoutMs)) {
    setStatus(tcpStatus_, sizeof(tcpStatus_), "connect-failed");
    tcp_.stop();
    return;
  }
  tcp_.setNoDelay(true);
  tcpWasConnected_ = true;
  resetTcpState();
  ++tcpConnects_;
  setStatus(tcpStatus_, sizeof(tcpStatus_), "connected");
  // The Hermes host expects authentication to be the first socket frame.
  if (token_.length()) {
    JsonDocument auth;
    auth["type"] = "auth";
    auth["token"] = token_;
    char line[256];
    const size_t length = measureJson(auth);
    if (length >= sizeof(line) || serializeJson(auth, line, sizeof(line)) != length ||
        !queueFrame(reinterpret_cast<const uint8_t*>(line), length)) {
      tcp_.stop();
      setStatus(tcpStatus_, sizeof(tcpStatus_), "auth-queue-failed");
      resetTcpState();
      return;
    }
  }
  JsonDocument hello;
  hello["hello"] = "hermes-buddy";
  hello["transport"] = "tcp";
  char line[128];
  const size_t length = measureJson(hello);
  if (length >= sizeof(line) || serializeJson(hello, line, sizeof(line)) != length ||
      !queueFrame(reinterpret_cast<const uint8_t*>(line), length)) {
    tcp_.stop();
    setStatus(tcpStatus_, sizeof(tcpStatus_), "hello-queue-failed");
    resetTcpState();
  }
}

void V1Network::consumeTcp(bool usbAlive, LineHandler handler, void* context) {
  if (!tcp_.connected()) return;
  size_t consumed = 0;
  while (tcp_.available() > 0 && consumed++ < 512) {
    const protocol::SerialLineFramer<>::Result result =
        framer_.push(static_cast<char>(tcp_.read()));
    if (result == protocol::SerialLineFramer<>::Result::LineReady) {
      if (!usbAlive && handler) handler(framer_.line(), context);
    } else if (result == protocol::SerialLineFramer<>::Result::LineTooLong ||
               result == protocol::SerialLineFramer<>::Result::InvalidLine) {
      ++framingErrors_;
    }
  }
}

void V1Network::poll(bool usbAlive, LineHandler handler, void* context) {
  if (!started_) return;
  maybeConnect(usbAlive, millis());
  consumeTcp(usbAlive, handler, context);
  flushOutput();
}

bool V1Network::sendLine(const char* line, bool usbAlive) {
  if (!line || usbAlive || !tcp_.connected()) return false;
  const size_t length = strlen(line);
  if (length > 4095 || !queueFrame(reinterpret_cast<const uint8_t*>(line), length)) {
    tcp_.stop();
    tcpWasConnected_ = false;
    setStatus(tcpStatus_, sizeof(tcpStatus_), "write-queue-failed");
    resetTcpState();
    return false;
  }
  return true;
}

bool V1Network::queueFrame(const uint8_t* bytes, size_t length) {
  const size_t frameLength = length + 1;
  if (!tcp_.connected() || frameLength > 4096 ||
      frameLength > kOutputCapacity - outputSize_) return false;
  for (size_t i = 0; i < length; ++i) {
    output_[(outputHead_ + outputSize_) % kOutputCapacity] = bytes[i];
    ++outputSize_;
  }
  output_[(outputHead_ + outputSize_) % kOutputCapacity] = '\n';
  ++outputSize_;
  return true;
}

void V1Network::flushOutput() {
  if (!tcp_.connected() || !outputSize_) return;
  const int socket = tcp_.fd();
  if (socket < 0) return;
  const size_t contiguous = min(outputSize_, kOutputCapacity - outputHead_);
  const size_t budget = min(contiguous, static_cast<size_t>(512));
  const ssize_t written = ::send(socket, output_ + outputHead_, budget, MSG_DONTWAIT);
  if (written > 0) {
    outputHead_ = (outputHead_ + static_cast<size_t>(written)) % kOutputCapacity;
    outputSize_ -= static_cast<size_t>(written);
    return;
  }
  if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return;
  tcp_.stop();
  tcpWasConnected_ = false;
  outputHead_ = outputSize_ = 0;
  framer_ = protocol::SerialLineFramer<>();
  setStatus(tcpStatus_, sizeof(tcpStatus_), "write-failed");
}

void V1Network::resetTcpState() {
  framer_ = protocol::SerialLineFramer<>();
  outputHead_ = outputSize_ = 0;
}

}  // namespace network
