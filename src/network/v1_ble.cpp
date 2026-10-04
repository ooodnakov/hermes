#include "v1_ble.h"

#include <esp_system.h>
#include <cstring>

namespace network {
namespace {
constexpr char kNusService[] = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
constexpr char kNusRx[] = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";
constexpr char kNusTx[] = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";
constexpr size_t kPollLineBudget = 8;
}

class V1Ble::ServerCallbacks final : public NimBLEServerCallbacks {
 public:
  explicit ServerCallbacks(V1Ble& owner) : owner_(owner) {}
  void onConnect(NimBLEServer*, NimBLEConnInfo& info) override { owner_.onConnect(info); }
  void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int) override { owner_.onDisconnect(); }
  void onAuthenticationComplete(NimBLEConnInfo& info) override { owner_.onAuthentication(info); }
  void onMTUChange(uint16_t mtu, NimBLEConnInfo&) override { owner_.onMtu(mtu); }
  uint32_t onPassKeyDisplay() override { return NimBLEDevice::getSecurityPasskey(); }
 private:
  V1Ble& owner_;
};

class V1Ble::RxCallbacks final : public NimBLECharacteristicCallbacks {
 public:
  explicit RxCallbacks(V1Ble& owner) : owner_(owner) {}
  void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo& info) override {
    owner_.onWrite(characteristic, info);
  }
 private:
  V1Ble& owner_;
};

class V1Ble::TxCallbacks final : public NimBLECharacteristicCallbacks {
 public:
  explicit TxCallbacks(V1Ble& owner) : owner_(owner) {}
  void onSubscribe(NimBLECharacteristic*, NimBLEConnInfo& info, uint16_t value) override {
    owner_.onSubscribe(info, value);
  }
 private:
  V1Ble& owner_;
};

void V1Ble::setStatus(const char* value) {
  strncpy(status_, value, sizeof(status_) - 1);
  status_[sizeof(status_) - 1] = '\0';
}

bool V1Ble::begin() {
  if (ready_) return true;
  if (!NimBLEDevice::init("Hermes Familiar V1")) {
    setStatus("init-failed");
    return false;
  }
  // Display-only passkey pairing provides encryption, peer authentication,
  // secure-connections key exchange, and bonding. The per-boot passkey is
  // reported only on the physically attached USB console.
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
  NimBLEDevice::setSecurityAuth(true, true, true);
  const uint32_t passkey = 100000 + esp_random() % 900000;
  NimBLEDevice::setSecurityPasskey(passkey);
  Serial.printf("BLE pairing passkey: %06lu\n", static_cast<unsigned long>(passkey));

  server_ = NimBLEDevice::createServer();
  if (!server_) {
    setStatus("server-failed");
    return false;
  }
  server_->setCallbacks(new ServerCallbacks(*this));
  server_->advertiseOnDisconnect(true);
  NimBLEService* service = server_->createService(kNusService);
  constexpr uint16_t secureWrite = NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR |
      NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::WRITE_AUTHEN;
  constexpr uint16_t secureNotify = NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ |
      NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::READ_AUTHEN;
  NimBLECharacteristic* rx = service->createCharacteristic(kNusRx, secureWrite, 512);
  tx_ = service->createCharacteristic(kNusTx, secureNotify, 512);
  rx->setCallbacks(new RxCallbacks(*this));
  tx_->setCallbacks(new TxCallbacks(*this));
  if (!server_->start()) {
    setStatus("service-failed");
    return false;
  }
  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  advertising->addServiceUUID(kNusService);
  advertising->setName("Hermes Familiar V1");
  ready_ = advertising->start();
  setStatus(ready_ ? "advertising" : "advertise-failed");
  return ready_;
}

bool V1Ble::secure(const NimBLEConnInfo& info) const {
  return info.isEncrypted() && info.isAuthenticated() && info.getSecKeySize() >= 16;
}

void V1Ble::onConnect(NimBLEConnInfo& info) {
  portENTER_CRITICAL(&mux_);
  resetSession();
  connected_ = true;
  connectionHandle_ = info.getConnHandle();
  mtu_ = info.getMTU();
  ++connects_;
  setStatus("pairing");
  portEXIT_CRITICAL(&mux_);
}

void V1Ble::onDisconnect() {
  portENTER_CRITICAL(&mux_);
  ++disconnects_;
  resetSession();
  setStatus("advertising");
  portEXIT_CRITICAL(&mux_);
}

void V1Ble::onAuthentication(NimBLEConnInfo& info) {
  portENTER_CRITICAL(&mux_);
  authenticated_ = secure(info);
  if (!authenticated_) ++authFailures_;
  setStatus(authenticated_ ? "secure" : "auth-failed");
  portEXIT_CRITICAL(&mux_);
  if (!secure(info) && server_) server_->disconnect(info);
}

void V1Ble::onMtu(uint16_t mtu) {
  portENTER_CRITICAL(&mux_);
  mtu_ = mtu < 23 ? 23 : mtu;
  portEXIT_CRITICAL(&mux_);
}

void V1Ble::onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo& info) {
  const std::string& value = characteristic->getValue();
  portENTER_CRITICAL(&mux_);
  if (!authenticated_ || !secure(info) || info.getConnHandle() != connectionHandle_) {
    ++rejectedWrites_;
  } else {
    transport_.enqueueInput(reinterpret_cast<const uint8_t*>(value.data()), value.size());
  }
  portEXIT_CRITICAL(&mux_);
}

void V1Ble::onSubscribe(NimBLEConnInfo& info, uint16_t value) {
  portENTER_CRITICAL(&mux_);
  subscribed_ = authenticated_ && secure(info) && (value & 1);
  portEXIT_CRITICAL(&mux_);
}

void V1Ble::resetSession() {
  ++sessionResetCalls_;
  transport_.reset();
  connected_ = authenticated_ = subscribed_ = false;
  connectionHandle_ = BLE_HS_CONN_HANDLE_NONE;
  mtu_ = 23;
}

V1Ble::QueueStats V1Ble::queueStats() {
  portENTER_CRITICAL(&mux_);
  const QueueStats stats{
      transport_.inputSize(), transport_.outputSize(), sessionResetCalls_};
  portEXIT_CRITICAL(&mux_);
  return stats;
}

void V1Ble::poll(LineHandler handler, void* context) {
  for (size_t count = 0; count < kPollLineBudget; ++count) {
    portENTER_CRITICAL(&mux_);
    const V1BleTransport::InputResult result = transport_.nextLine();
    char line[V1BleTransport::kMaxLine + 1];
    if (result == V1BleTransport::InputResult::LineReady) strcpy(line, transport_.line());
    portEXIT_CRITICAL(&mux_);
    if (result == V1BleTransport::InputResult::None) break;
    if (result == V1BleTransport::InputResult::LineReady) {
      if (handler) handler(line, context);
    } else {
      ++framingErrors_;
    }
  }
  flushOutput();
}

bool V1Ble::sendLine(const char* line) {
  portENTER_CRITICAL(&mux_);
  const bool queued = connected_ && authenticated_ && subscribed_ && transport_.enqueueOutput(line);
  portEXIT_CRITICAL(&mux_);
  return queued;
}

void V1Ble::flushOutput() {
  uint8_t chunk[509];
  portENTER_CRITICAL(&mux_);
  if (!connected_ || !authenticated_ || !subscribed_ || !tx_) {
    portEXIT_CRITICAL(&mux_);
    return;
  }
  const size_t payload = mtu_ > 3 ? min(sizeof(chunk), static_cast<size_t>(mtu_ - 3)) : 20;
  const size_t length = transport_.peekOutput(chunk, payload);
  const uint16_t handle = connectionHandle_;
  portEXIT_CRITICAL(&mux_);
  if (!length) return;
  if (!tx_->notify(chunk, length, handle)) {
    ++notifyFailures_;
    return;
  }
  portENTER_CRITICAL(&mux_);
  if (connected_ && authenticated_ && handle == connectionHandle_) transport_.consumeOutput(length);
  portEXIT_CRITICAL(&mux_);
}

}  // namespace network
