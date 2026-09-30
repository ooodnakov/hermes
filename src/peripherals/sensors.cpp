#include "sensors.h"

namespace peripherals {
namespace {
constexpr int kBatteryAdcPin = 4;
constexpr float kBatteryDividerRatio = 3.0f;
constexpr uint8_t kRtcAddress = 0x51;
constexpr uint8_t kQmiAddressPrimary = 0x6B;
constexpr uint8_t kQmiAddressAlternate = 0x6A;
constexpr uint8_t kQmiWhoAmI = 0x05;
constexpr uint8_t kQmiResetRegister = 0x60;
constexpr uint8_t kQmiResetValue = 0xb0;
constexpr uint8_t kQmiResetResultRegister = 0x4d;
constexpr uint8_t kQmiResetComplete = 0x80;
constexpr uint32_t kQmiResetTimeoutMs = 2000;
constexpr uint32_t kBatteryIntervalMs = 1000;
constexpr uint32_t kRtcIntervalMs = 1000;
constexpr uint32_t kImuIntervalMs = 300;
constexpr uint32_t kImuRecoveryIntervalMs = 5000;

bool validBcd(uint8_t value, uint8_t max) {
  const uint8_t low = value & 0x0f;
  const uint8_t high = (value >> 4) & 0x0f;
  return low <= 9 && high <= 9 && high * 10 + low <= max;
}

uint8_t fromBcd(uint8_t value) {
  return static_cast<uint8_t>((value >> 4) * 10 + (value & 0x0f));
}

void incrementSaturated(uint16_t& value) {
  if (value != UINT16_MAX) ++value;
}
}  // namespace

bool Sensors::begin(TwoWire& peripheralBus, uint32_t nowMs) {
  bus_ = &peripheralBus;
  // Arduino-ESP32 3.x attaches a GPIO to ADC oneshot lazily on its first
  // calibrated read. Prime and discard one conversion before applying the
  // per-pin setting; setting attenuation before this emits a warning and has
  // no effect. The discarded conversion uses the core's 11 dB default.
  (void)analogReadMilliVolts(kBatteryAdcPin);
  analogSetPinAttenuation(kBatteryAdcPin, ADC_11db);
  batteryAdcReady_ = true;

  uint8_t probe = 0;
  state_.rtcReady = readRegister(kRtcAddress, 0x04, probe);
  state_.imuReady = readRegister(kQmiAddressPrimary, 0x00, probe) && probe == kQmiWhoAmI;
  uint8_t address = kQmiAddressPrimary;
  if (!state_.imuReady) {
    state_.imuReady = readRegister(kQmiAddressAlternate, 0x00, probe) && probe == kQmiWhoAmI;
    address = kQmiAddressAlternate;
  }
  activeImuAddress_ = address;
  imuDetected_ = state_.imuReady;
  if (imuDetected_) state_.imuConfigReady = configureImu();

  lastBatteryMs_ = nowMs - kBatteryIntervalMs;
  lastRtcMs_ = nowMs - kRtcIntervalMs;
  lastImuMs_ = nowMs - kImuIntervalMs;
  lastImuRecoveryMs_ = nowMs;
  update(nowMs);
  return state_.rtcReady || state_.imuReady;
}

bool Sensors::readRegister(uint8_t address, uint8_t reg, uint8_t& value) {
  if (bus_ == nullptr) return false;
  bus_->beginTransmission(address);
  bus_->write(reg);
  if (bus_->endTransmission(false) != 0 || bus_->requestFrom(static_cast<int>(address), 1, true) != 1) {
    while (bus_->available()) bus_->read();
    return false;
  }
  value = static_cast<uint8_t>(bus_->read());
  return true;
}

bool Sensors::readRegisters(uint8_t address, uint8_t reg, uint8_t* data, size_t length) {
  if (bus_ == nullptr || data == nullptr || length == 0 || length > 16) return false;
  bus_->beginTransmission(address);
  bus_->write(reg);
  if (bus_->endTransmission(false) != 0 ||
      bus_->requestFrom(static_cast<int>(address), static_cast<int>(length), true) !=
          static_cast<int>(length)) {
    while (bus_->available()) bus_->read();
    return false;
  }
  for (size_t i = 0; i < length; ++i) data[i] = static_cast<uint8_t>(bus_->read());
  return true;
}

bool Sensors::writeRegister(uint8_t address, uint8_t reg, uint8_t value) {
  if (bus_ == nullptr) return false;
  bus_->beginTransmission(address);
  bus_->write(reg);
  bus_->write(value);
  return bus_->endTransmission(true) == 0;
}

bool Sensors::configureImu() {
  // The V1 device is LiPo-powered, so an ESP32 warm reboot does not power-cycle
  // the QMI. Match the verified Hermes setup: request soft reset and wait for
  // its explicit completion marker instead of relying on a fixed delay.
  if (!writeRegister(activeImuAddress_, kQmiResetRegister, kQmiResetValue)) return false;
  bool resetComplete = false;
  const uint32_t resetStartedMs = millis();
  while (millis() - resetStartedMs < kQmiResetTimeoutMs) {
    uint8_t result = 0;
    if (readRegister(activeImuAddress_, kQmiResetResultRegister, result) &&
        result == kQmiResetComplete) {
      resetComplete = true;
      break;
    }
    delay(10);
  }
  if (!resetComplete) return false;

  // Byte-for-byte configuration from the current Hermes qmiConfig(), with
  // write/readback verification and the vendor-proven enable-first order.
  uint8_t ctrl1 = 0;
  if (!readRegister(activeImuAddress_, 0x02, ctrl1) ||
      !writeRegister(activeImuAddress_, 0x02, static_cast<uint8_t>((ctrl1 & 0xfe) | 0x40))) {
    return false;
  }
  constexpr uint8_t config[][2] = {
      {0x08, 0x43},  // CTRL7: enable clock and accelerometer.
      {0x07, 0x00},  // CTRL6: attitude engine off.
      {0x03, 0x10},  // CTRL2: accelerometer ±4 g.
      {0x04, 0x20},  // CTRL3: gyro setup from the existing demo.
      {0x06, 0x71},  // CTRL5: low-pass filters.
  };
  for (const auto& item : config) {
    if (!writeRegister(activeImuAddress_, item[0], item[1])) return false;
    uint8_t verify = 0;
    if (!readRegister(activeImuAddress_, item[0], verify) || verify != item[1]) return false;
  }
  return true;
}

void Sensors::sampleBattery() {
  // analogReadMilliVolts() uses the core's calibrated ADC conversion. The
  // divider is the vendor V1 3:1 ratio; no empirical correction or expander
  // P1 gate is applied until that path is confirmed against board evidence.
  if (!batteryAdcReady_) {
    state_.batterySampleReady = false;
    return;
  }
  if (!batteryAdcReady_) {
    state_.batterySampleReady = false;
    return;
  }
  constexpr uint8_t kSamples = 8;
  uint32_t sumMv = 0;
  uint8_t validSamples = 0;
  for (uint8_t i = 0; i < kSamples; ++i) {
    const uint32_t mv = analogReadMilliVolts(kBatteryAdcPin);
    if (mv != 0) {
      sumMv += mv;
      ++validSamples;
    }
    delayMicroseconds(250);
  }
  if (validSamples == 0) {
    state_.batterySampleReady = false;
    return;
  }
  state_.batteryAdcMillivolts = static_cast<uint16_t>(sumMv / validSamples);
  state_.batteryVolts = (static_cast<float>(state_.batteryAdcMillivolts) / 1000.0f) *
                        kBatteryDividerRatio;
  state_.batterySampleReady = true;
}

void Sensors::sampleRtc() {
  uint8_t time[3] = {};
  if (!readRegisters(kRtcAddress, 0x04, time, sizeof(time))) {
    state_.rtcReady = false;
    state_.rtcTimeValid = false;
    incrementSaturated(state_.rtcErrors);
    return;
  }
  state_.rtcReady = true;
  // PCF85063 seconds bit 7 is VL (voltage-low), so never expose that time as
  // valid. The chip stores seconds, minutes, hours at 0x04..0x06.
  const uint8_t seconds = time[0] & 0x7f;
  const uint8_t minutes = time[1] & 0x7f;
  const uint8_t hours = time[2] & 0x3f;
  state_.rtcTimeValid = !(time[0] & 0x80) && validBcd(seconds, 59) &&
                        validBcd(minutes, 59) && validBcd(hours, 23);
  if (state_.rtcTimeValid) {
    state_.rtcSecond = fromBcd(seconds);
    state_.rtcMinute = fromBcd(minutes);
    state_.rtcHour = fromBcd(hours);
  }
}

void Sensors::sampleImu(uint32_t nowMs) {
  if (!imuDetected_ || !state_.imuConfigReady) return;
  uint8_t raw[6] = {};
  if (!readRegisters(activeImuAddress_, 0x35, raw, sizeof(raw))) {
    state_.accelerationReady = false;
    state_.imuReady = false;
    if (consecutiveImuErrors_ != UINT8_MAX) ++consecutiveImuErrors_;
    incrementSaturated(state_.imuErrors);
    return;
  }
  consecutiveImuErrors_ = 0;
  const int16_t ax = static_cast<int16_t>((static_cast<uint16_t>(raw[1]) << 8) | raw[0]);
  const int16_t ay = static_cast<int16_t>((static_cast<uint16_t>(raw[3]) << 8) | raw[2]);
  const int16_t az = static_cast<int16_t>((static_cast<uint16_t>(raw[5]) << 8) | raw[4]);
  // Existing V1 QMI config selects ±4 g, corresponding to 8192 LSB/g.
  state_.accelerationXG = static_cast<float>(ax) / 8192.0f;
  state_.accelerationYG = static_cast<float>(ay) / 8192.0f;
  state_.accelerationZG = static_cast<float>(az) / 8192.0f;
  state_.accelerationReady = true;

  (void)nowMs;
}

void Sensors::update(uint32_t nowMs) {
  if (bus_ == nullptr) return;
  if (nowMs - lastBatteryMs_ >= kBatteryIntervalMs) {
    lastBatteryMs_ = nowMs;
    sampleBattery();
  }
  if (nowMs - lastRtcMs_ >= kRtcIntervalMs) {
    lastRtcMs_ = nowMs;
    sampleRtc();
  }
  if (nowMs - lastImuMs_ >= kImuIntervalMs) {
    lastImuMs_ = nowMs;
    sampleImu(nowMs);
  }
  // Recovery is limited to a WHO_AM_I probe and local sensor reconfiguration;
  // the shared bus/controller is never ended, restarted, or clock-reset.
  if (nowMs - lastImuRecoveryMs_ >= kImuRecoveryIntervalMs &&
      (!imuDetected_ || ((!state_.imuReady || !state_.imuConfigReady) &&
                         (consecutiveImuErrors_ >= 3 || !state_.imuConfigReady)))) {
    lastImuRecoveryMs_ = nowMs;
    uint8_t who = 0;
    bool found = imuDetected_ && readRegister(activeImuAddress_, 0x00, who) && who == kQmiWhoAmI;
    if (!found) {
      if (readRegister(kQmiAddressPrimary, 0x00, who) && who == kQmiWhoAmI) {
        activeImuAddress_ = kQmiAddressPrimary;
        found = true;
      } else if (readRegister(kQmiAddressAlternate, 0x00, who) && who == kQmiWhoAmI) {
        activeImuAddress_ = kQmiAddressAlternate;
        found = true;
      }
    }
    if (found && configureImu()) {
      imuDetected_ = true;
      state_.imuReady = true;
      state_.imuConfigReady = true;
      state_.accelerationReady = false;
      consecutiveImuErrors_ = 0;
      incrementSaturated(state_.imuRecoveries);
    }
  }
}

}  // namespace peripherals
