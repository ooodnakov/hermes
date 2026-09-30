#pragma once

#include <Arduino.h>
#include <Wire.h>

namespace peripherals {

// Read-only sensor access for the V1 peripheral bus. The caller owns and
// initializes the bus; this module never calls TwoWire::begin/end.
struct SensorSnapshot {
  bool batterySampleReady = false;
  uint16_t batteryAdcMillivolts = 0;  // Voltage at GPIO4, from calibrated ADC API.
  float batteryVolts = 0.0f;         // GPIO4 reading multiplied by the V1 3:1 divider.

  bool rtcReady = false;
  bool rtcTimeValid = false;  // False for oscillator-stop/invalid/out-of-range data.
  uint8_t rtcHour = 0;
  uint8_t rtcMinute = 0;
  uint8_t rtcSecond = 0;

  bool imuReady = false;
  bool imuConfigReady = false;
  bool accelerationReady = false;
  float accelerationXG = 0.0f;
  float accelerationYG = 0.0f;
  float accelerationZG = 0.0f;

  uint16_t rtcErrors = 0;
  uint16_t imuErrors = 0;
  uint16_t imuRecoveries = 0;
};

class Sensors {
 public:
  // Call after the board layer has initialized the shared peripheral TwoWire.
  bool begin(TwoWire& peripheralBus, uint32_t nowMs);
  // Nonblocking scheduler entry. Sampling is internally rate-limited.
  void update(uint32_t nowMs);
  const SensorSnapshot& snapshot() const { return state_; }

 private:
  TwoWire* bus_ = nullptr;
  uint8_t activeImuAddress_ = 0x6b;
  bool imuDetected_ = false;
  bool batteryAdcReady_ = false;
  SensorSnapshot state_;
  uint32_t lastBatteryMs_ = 0;
  uint32_t lastRtcMs_ = 0;
  uint32_t lastImuMs_ = 0;
  uint32_t lastImuRecoveryMs_ = 0;
  uint8_t consecutiveImuErrors_ = 0;

  bool readRegister(uint8_t address, uint8_t reg, uint8_t& value);
  bool readRegisters(uint8_t address, uint8_t reg, uint8_t* data, size_t length);
  bool writeRegister(uint8_t address, uint8_t reg, uint8_t value);
  bool configureImu();
  void sampleBattery();
  void sampleRtc();
  void sampleImu(uint32_t nowMs);
};

}  // namespace peripherals
