#pragma once

#include <Arduino.h>
#include <Wire.h>

namespace peripherals {

enum class Gesture : uint8_t { None, FaceDown, Upright, Shake, Tap2, Pickup };

// Sensor access for the V1 peripheral bus. The caller owns and initializes
// the bus; this module never calls TwoWire::begin/end.
struct SensorSnapshot {
  bool batterySampleReady = false;
  uint16_t batteryAdcMillivolts = 0;  // Voltage at GPIO4, from calibrated ADC API.
  float batteryVolts = 0.0f;         // GPIO4 reading multiplied by the V1 3:1 divider.

  bool rtcReady = false;
  bool rtcTimeValid = false;  // False for oscillator-stop/invalid/out-of-range data.
  uint16_t rtcYear = 0;
  uint8_t rtcMonth = 0;
  uint8_t rtcDay = 0;
  uint8_t rtcWeekday = 0;
  uint8_t rtcHour = 0;
  uint8_t rtcMinute = 0;
  uint8_t rtcSecond = 0;
  // millis() at the last complete control/calendar read or verified clock set,
  // even if its data was invalid. Failed bus reads leave it unchanged.
  uint32_t rtcSampledAtMs = 0;

  bool imuReady = false;
  bool imuConfigReady = false;
  bool accelerationReady = false;
  float accelerationXG = 0.0f;
  float accelerationYG = 0.0f;
  float accelerationZG = 0.0f;
  // Latest physical gesture and a monotonic event sequence. Consumers compare
  // the sequence to avoid losing events between their UI ticks.
  Gesture gesture = Gesture::None;
  uint32_t gestureSequence = 0;

  uint16_t rtcErrors = 0;
  uint16_t imuErrors = 0;
  uint16_t imuRecoveries = 0;
};

class Sensors {
 public:
  // Call after the board layer has initialized the shared peripheral TwoWire.
  bool begin(TwoWire& peripheralBus, uint32_t nowMs);
  // Explicitly set UTC calendar time. Invalid fields are rejected before bus I/O.
  bool setRtcDateTime(int year, int month, int day, int hour, int minute, int second);
  // Nonblocking scheduler entry. Sampling is internally rate-limited.
  void update(uint32_t nowMs);
  const SensorSnapshot& snapshot() const { return state_; }

 private:
  TwoWire* bus_ = nullptr;
  uint8_t activeImuAddress_ = 0x6b;
  bool imuDetected_ = false;
  bool rtcSetFailed_ = false;
  bool batteryAdcReady_ = false;
  SensorSnapshot state_;
  uint32_t lastBatteryMs_ = 0;
  uint32_t lastRtcMs_ = 0;
  uint32_t lastImuMs_ = 0;
  uint32_t lastImuRecoveryMs_ = 0;
  uint8_t consecutiveImuErrors_ = 0;
  float lastAccelerationMagnitude_ = 1.0f;
  float baseX_ = 0.0f, baseY_ = 0.0f, baseZ_ = 0.0f;
  bool baseSet_ = false;
  bool quietMode_ = false;
  uint8_t basePolls_ = 0, faceDownPolls_ = 0, uprightPolls_ = 0;
  uint32_t lastShakeMs_ = 0, previousTapPulseMs_ = 0, lastMotionMs_ = 0;
  uint32_t imuStartedMs_ = 0;
  bool tapArmed_ = true;
  bool haveLastAccelerationMagnitude_ = false;

  bool readRegister(uint8_t address, uint8_t reg, uint8_t& value);
  bool readRegisters(uint8_t address, uint8_t reg, uint8_t* data, size_t length);
  bool writeRegister(uint8_t address, uint8_t reg, uint8_t value);
  bool configureImu();
  void sampleBattery();
  void sampleRtc();
  void sampleImu(uint32_t nowMs);
  void detectGesture(uint32_t nowMs);
  void emitGesture(Gesture gesture);
};

}  // namespace peripherals
