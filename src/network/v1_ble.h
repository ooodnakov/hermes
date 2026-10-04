#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>

#include "v1_ble_transport.h"

namespace network {

class V1Ble {
 public:
  using LineHandler = void (*)(const char*, void*);

  bool begin();
  void poll(LineHandler handler, void* context);
  bool sendLine(const char* line);

  bool ready() const { return ready_; }
  bool connected() const { return connected_; }
  bool authenticated() const { return authenticated_; }
  bool subscribed() const { return subscribed_; }
  uint16_t mtu() const { return mtu_; }
  const char* status() const { return status_; }
  uint32_t connects() const { return connects_; }
  uint32_t disconnects() const { return disconnects_; }
  uint32_t authFailures() const { return authFailures_; }
  uint32_t rejectedWrites() const { return rejectedWrites_; }
  uint32_t framingErrors() const { return framingErrors_; }
  uint32_t notifyFailures() const { return notifyFailures_; }
  size_t inputHighWater() const { return transport_.inputHighWater(); }
  size_t outputHighWater() const { return transport_.outputHighWater(); }
  uint32_t inputQueueDrops() const { return transport_.inputQueueDrops(); }
  uint32_t outputQueueDrops() const { return transport_.outputQueueDrops(); }

 private:
  class ServerCallbacks;
  class RxCallbacks;
  class TxCallbacks;
  friend class ServerCallbacks;
  friend class RxCallbacks;
  friend class TxCallbacks;

  bool secure(const NimBLEConnInfo& info) const;
  void onConnect(NimBLEConnInfo& info);
  void onDisconnect();
  void onAuthentication(NimBLEConnInfo& info);
  void onMtu(uint16_t mtu);
  void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo& info);
  void onSubscribe(NimBLEConnInfo& info, uint16_t value);
  void resetSession();
  void flushOutput();
  void setStatus(const char* value);

  V1BleTransport transport_;
  NimBLEServer* server_ = nullptr;
  NimBLECharacteristic* tx_ = nullptr;
  portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  uint16_t connectionHandle_ = BLE_HS_CONN_HANDLE_NONE;
  uint16_t mtu_ = 23;
  uint32_t connects_ = 0, disconnects_ = 0, authFailures_ = 0;
  uint32_t rejectedWrites_ = 0, framingErrors_ = 0, notifyFailures_ = 0;
  bool ready_ = false, connected_ = false, authenticated_ = false, subscribed_ = false;
  char status_[24] = "not-started";
};

}  // namespace network
