#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>

#include "../protocol/serial_line_framer.h"

namespace network {

// V1's secondary Hermes transport. USB remains the primary link whenever it
// has received a host frame within the previous 30 seconds.
class V1Network {
 public:
  using LineHandler = void (*)(const char*, void*);

  void configure(JsonObjectConst wifi, JsonObjectConst host);
  void begin();
  void poll(bool usbAlive, LineHandler handler, void* context);
  bool sendLine(const char* line, bool usbAlive);

  bool wifiReady() const;
  bool tcpConnected();
  bool wifiConfigured() const { return ssid_.length() > 0; }
  bool tcpConfigured() const { return host_.length() > 0; }
  const char* wifiStatus() const;
  const char* tcpStatus();
  const String& tcpHost() const { return host_; }
  uint16_t tcpPort() const { return port_; }
  const IPAddress& localIp() const { return localIp_; }
  uint32_t tcpConnects() const { return tcpConnects_; }
  uint32_t tcpDisconnects() const { return tcpDisconnects_; }
  uint32_t framingErrors() const { return framingErrors_; }

 private:
  void startWifi();
  void maybeConnect(bool usbAlive, uint32_t now);
  void consumeTcp(bool usbAlive, LineHandler handler, void* context);
  bool queueFrame(const uint8_t* bytes, size_t length);
  void flushOutput();
  void resetTcpState();

  WiFiClient tcp_;
  protocol::SerialLineFramer<> framer_;
  static constexpr size_t kOutputCapacity = 8192;
  uint8_t output_[kOutputCapacity]{};
  size_t outputHead_ = 0;
  size_t outputSize_ = 0;
  String ssid_;
  String password_;
  String host_;
  String token_;
  IPAddress hostAddress_;
  uint16_t port_ = 8767;
  uint32_t retryAt_ = 0;
  uint32_t lastTcpAttemptAt_ = 0;
  uint32_t tcpConnects_ = 0;
  uint32_t tcpDisconnects_ = 0;
  uint32_t framingErrors_ = 0;
  IPAddress localIp_;
  bool started_ = false;
  bool wifiAttempted_ = false;
  bool wifiWasReady_ = false;
  bool tcpWasConnected_ = false;
  char wifiStatus_[24] = "no-config";
  char tcpStatus_[24] = "not-configured";
};

}  // namespace network
