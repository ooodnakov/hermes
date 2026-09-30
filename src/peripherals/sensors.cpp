#include "sensors.h"

#include <cstring>

namespace peripherals {
namespace {
constexpr int kBatteryAdcPin = 4;
constexpr float kBatteryDividerRatio = 3.0f;
constexpr uint8_t kRtcAddress = 0x51;
constexpr uint8_t kRtcControl1 = 0x00;
constexpr uint8_t kRtcSeconds = 0x04;
constexpr uint8_t kRtcStopBit = 1u << 5;
constexpr uint8_t kRtc12HourBit = 1u << 1;
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

uint8_t toBcd(uint8_t value) {
  return static_cast<uint8_t>(((value / 10) << 4) | (value % 10));
}

bool leapYear(int year) {
  return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

uint8_t daysInMonth(int year, int month) {
  constexpr uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12) return 0;
  if (month == 2 && leapYear(year)) return 29;
  return days[month - 1];
}

// PCF85063A weekday encoding is 0..6. Use Sunday=0 (Gregorian convention).
uint8_t weekdayForDate(int year, int month, int day) {
  static constexpr int offsets[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (month < 3) --year;
  return static_cast<uint8_t>((year + year / 4 - year / 100 + year / 400 +
                               offsets[month - 1] + day) % 7);
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
  if (bus_->endTransmission(false) != 0 || bus_->requestFrom(static_cast<int>(address), 1, true) != 1 ||
      bus_->available() < 1) {
    while (bus_->available()) bus_->read();
    return false;
  }
  const int received = bus_->read();
  if (received < 0) return false;
  value = static_cast<uint8_t>(received);
  return true;
}

bool Sensors::readRegisters(uint8_t address, uint8_t reg, uint8_t* data, size_t length) {
  if (bus_ == nullptr || data == nullptr || length == 0 || length > 16) return false;
  bus_->beginTransmission(address);
  bus_->write(reg);
  if (bus_->endTransmission(false) != 0 ||
      bus_->requestFrom(static_cast<int>(address), static_cast<int>(length), true) != length ||
      bus_->available() < static_cast<int>(length)) {
    while (bus_->available()) bus_->read();
    return false;
  }
  for (size_t i = 0; i < length; ++i) {
    const int received = bus_->read();
    if (received < 0) return false;
    data[i] = static_cast<uint8_t>(received);
  }
  return true;
}

bool Sensors::writeRegister(uint8_t address, uint8_t reg, uint8_t value) {
  if (bus_ == nullptr) return false;
  bus_->beginTransmission(address);
  const bool registerWritten = bus_->write(reg) == 1;
  const bool valueWritten = bus_->write(value) == 1;
  const bool transactionOk = bus_->endTransmission(true) == 0;
  return registerWritten && valueWritten && transactionOk;
}

bool Sensors::setRtcDateTime(int year, int month, int day, int hour, int minute, int second) {
  // Reject the complete request before touching I2C.
  if (bus_ == nullptr || year < 2000 || year > 2099 || month < 1 || month > 12 ||
      day < 1 || day > daysInMonth(year, month) || hour < 0 || hour > 23 ||
      minute < 0 || minute > 59 || second < 0 || second > 59) {
    return false;
  }

  // A failed attempt must not let the periodic sampler bless potentially
  // partial calendar contents. Only a fully verified set clears this latch.
  rtcSetFailed_ = true;
  state_.rtcTimeValid = false;
  uint8_t control = 0;
  if (!readRegister(kRtcAddress, kRtcControl1, control)) {
    state_.rtcReady = false;
    state_.rtcTimeValid = false;
    incrementSaturated(state_.rtcErrors);
    return false;
  }
  // Preserve unrelated Control_1 bits, select 24-hour mode, and stop the
  // prescaler so the calendar registers can be changed as one coherent value.
  // Per the PCF85063A data sheet (https://www.nxp.com/docs/en/data-sheet/PCF85063A.pdf),
  // STOP freezes the calendar and OS is cleared by writing seconds with bit 7 zero.
  // STOP does not make an interrupted I2C transaction atomic, so a power loss
  // during this write can leave an uncertain calendar; the next sample validates it.
  const uint8_t stoppedControl = static_cast<uint8_t>((control | kRtcStopBit) & ~kRtc12HourBit);
  if (!writeRegister(kRtcAddress, kRtcControl1, stoppedControl)) {
    // Transaction outcome may be uncertain; restore the original control byte.
    (void)writeRegister(kRtcAddress, kRtcControl1, control);
    state_.rtcTimeValid = false;
    incrementSaturated(state_.rtcErrors);
    return false;
  }

  const uint8_t weekday = weekdayForDate(year, month, day);
  const uint8_t requested[] = {
      toBcd(static_cast<uint8_t>(second)), toBcd(static_cast<uint8_t>(minute)),
      toBcd(static_cast<uint8_t>(hour)), toBcd(static_cast<uint8_t>(day)), weekday,
      toBcd(static_cast<uint8_t>(month)), toBcd(static_cast<uint8_t>(year - 2000)),
  };
  bus_->beginTransmission(kRtcAddress);
  const bool addressWritten = bus_->write(kRtcSeconds) == 1;
  const bool dataWritten = bus_->write(requested, sizeof(requested)) == sizeof(requested);
  const bool transactionOk = bus_->endTransmission(true) == 0;
  const bool written = addressWritten && dataWritten && transactionOk;
  uint8_t readback[sizeof(requested)] = {};
  const bool verified = written && readRegisters(kRtcAddress, kRtcSeconds, readback, sizeof(readback)) &&
                        !(readback[0] & 0x80) &&
                        memcmp(readback, requested, sizeof(requested)) == 0;
  // Restore the original STOP/mode state after failure. A fully verified set
  // deliberately restarts in 24-hour mode.
  const uint8_t runningControl = static_cast<uint8_t>(stoppedControl & ~kRtcStopBit);
  const uint8_t finalControl = verified ? runningControl : control;
  bool restarted = writeRegister(kRtcAddress, kRtcControl1, finalControl);
  if (!restarted) restarted = writeRegister(kRtcAddress, kRtcControl1, finalControl);
  uint8_t controlReadback = 0;
  const bool restartVerified = restarted && readRegister(kRtcAddress, kRtcControl1, controlReadback) &&
      (verified ? controlReadback == runningControl
                : controlReadback == control);
  if (!verified || !restartVerified) {
    if (verified) {
      // Calendar data checked out, but clock restart did not. Restore the
      // caller's original STOP/mode state as a final best-effort cleanup.
      bool restored = writeRegister(kRtcAddress, kRtcControl1, control);
      if (!restored) (void)writeRegister(kRtcAddress, kRtcControl1, control);
    }
    state_.rtcTimeValid = false;
    incrementSaturated(state_.rtcErrors);
    return false;
  }

  state_.rtcReady = true;
  state_.rtcTimeValid = true;
  state_.rtcYear = static_cast<uint16_t>(year);
  state_.rtcMonth = static_cast<uint8_t>(month);
  state_.rtcDay = static_cast<uint8_t>(day);
  state_.rtcWeekday = weekday;
  state_.rtcHour = static_cast<uint8_t>(hour);
  state_.rtcMinute = static_cast<uint8_t>(minute);
  state_.rtcSecond = static_cast<uint8_t>(second);
  state_.rtcSampledAtMs = millis();
  rtcSetFailed_ = false;
  return true;
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
  uint8_t control = 0;
  uint8_t time[7] = {};
  if (!readRegister(kRtcAddress, kRtcControl1, control) ||
      !readRegisters(kRtcAddress, kRtcSeconds, time, sizeof(time))) {
    state_.rtcReady = false;
    state_.rtcTimeValid = false;
    incrementSaturated(state_.rtcErrors);
    return;
  }
  state_.rtcReady = true;
  state_.rtcSampledAtMs = millis();
  // The seconds OS flag means oscillator integrity is not guaranteed until
  // cleared by an explicit valid time set.
  const uint8_t seconds = time[0] & 0x7f;
  const uint8_t minutes = time[1] & 0x7f;
  const bool twelveHourMode = (control & kRtc12HourBit) != 0;
  const uint8_t hours = time[2] & (twelveHourMode ? 0x1f : 0x3f);
  const uint8_t day = time[3] & 0x3f;
  const uint8_t weekday = time[4] & 0x07;
  const uint8_t month = time[5] & 0x1f;
  const uint8_t year = time[6];
  const uint8_t hourValue = validBcd(hours, twelveHourMode ? 12 : 23)
      ? fromBcd(hours) : 0;
  const bool hourValid = twelveHourMode ? hourValue >= 1 : validBcd(hours, 23);
  const uint8_t monthValue = validBcd(month, 12) ? fromBcd(month) : 0;
  const uint8_t dayValue = validBcd(day, 31) ? fromBcd(day) : 0;
  const uint8_t yearValue = validBcd(year, 99) ? fromBcd(year) : 0;
  bool valid = !(control & kRtcStopBit) && !(time[0] & 0x80) && validBcd(seconds, 59) &&
      validBcd(minutes, 59) && hourValid && dayValue >= 1 && weekday <= 6 &&
      monthValue >= 1 && validBcd(year, 99);
  valid = valid && dayValue <= daysInMonth(2000 + yearValue, monthValue);
  state_.rtcTimeValid = valid && !rtcSetFailed_;
  if (state_.rtcTimeValid) {
    state_.rtcSecond = fromBcd(seconds);
    state_.rtcMinute = fromBcd(minutes);
    state_.rtcHour = hourValue;
    if (twelveHourMode) {
      state_.rtcHour %= 12;
      if (time[2] & 0x20) state_.rtcHour += 12;
    }
    state_.rtcYear = static_cast<uint16_t>(2000 + yearValue);
    state_.rtcMonth = monthValue;
    state_.rtcDay = dayValue;
    state_.rtcWeekday = weekday;
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
  state_.imuReady = true;

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
