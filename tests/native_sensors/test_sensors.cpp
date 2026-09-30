#include "peripherals/sensors.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>

namespace {
constexpr uint8_t kRtc = 0x51;
constexpr uint8_t kQmi = 0x6b;

void setValidRtc(TwoWire& bus) {
  const uint8_t registers[] = {0x04, 0x56, 0x34, 0x12, 0x29, 0x04, 0x02, 0x24};
  for (size_t i = 1; i < sizeof(registers); ++i) bus.set(kRtc, registers[0] + i - 1, registers[i]);
}

void testBootReadsWithoutSettingRtc() {
  TwoWire bus;
  setValidRtc(bus);
  bus.set(kRtc, 0x00, 0x00);
  peripherals::Sensors sensors;
  sensors.begin(bus, 0);
  const auto& state = sensors.snapshot();
  assert(state.rtcTimeValid);
  assert(state.rtcYear == 2024 && state.rtcMonth == 2 && state.rtcDay == 29);
  assert(state.rtcWeekday == 4 && state.rtcHour == 12 && state.rtcMinute == 34 && state.rtcSecond == 56);
  assert(state.rtcSampledAtMs > 0);
  assert(bus.rtcWriteTransactions == 0);
}

void testRtcSampleTimestampTracksCompleteReadsOnly() {
  TwoWire bus;
  setValidRtc(bus);
  peripherals::Sensors sensors;
  sensors.begin(bus, 0);
  const uint32_t firstSample = sensors.snapshot().rtcSampledAtMs;
  bus.failTransaction = bus.transactionCount + 1;
  sensors.update(1000);
  assert(sensors.snapshot().rtcSampledAtMs == firstSample);
  sensors.update(2000);
  assert(sensors.snapshot().rtcSampledAtMs > firstSample);
}

void testInvalidBcdAndImpossibleCalendar() {
  TwoWire badBcd;
  setValidRtc(badBcd);
  badBcd.set(kRtc, 0x04, 0x80);  // OS means oscillator integrity is unknown.
  peripherals::Sensors sensors;
  sensors.begin(badBcd, 0);
  assert(!sensors.snapshot().rtcTimeValid);

  TwoWire malformedBcd;
  setValidRtc(malformedBcd);
  malformedBcd.set(kRtc, 0x04, 0x1a);
  peripherals::Sensors malformedSensors;
  malformedSensors.begin(malformedBcd, 0);
  assert(!malformedSensors.snapshot().rtcTimeValid);

  TwoWire invalidDate;
  setValidRtc(invalidDate);
  invalidDate.set(kRtc, 0x07, 0x30);  // 30 February.
  peripherals::Sensors invalidSensors;
  invalidSensors.begin(invalidDate, 0);
  assert(!invalidSensors.snapshot().rtcTimeValid);

  TwoWire stopped;
  setValidRtc(stopped);
  stopped.set(kRtc, 0x00, 0x20);
  peripherals::Sensors stoppedSensors;
  stoppedSensors.begin(stopped, 0);
  assert(!stoppedSensors.snapshot().rtcTimeValid);
}

void testTwelveHourDecoding() {
  TwoWire noon;
  setValidRtc(noon);
  noon.set(kRtc, 0x00, 0x02);  // 12-hour mode.
  noon.set(kRtc, 0x06, 0x32);  // 12 PM.
  peripherals::Sensors noonSensors;
  noonSensors.begin(noon, 0);
  assert(noonSensors.snapshot().rtcTimeValid && noonSensors.snapshot().rtcHour == 12);

  TwoWire midnight;
  setValidRtc(midnight);
  midnight.set(kRtc, 0x00, 0x02);
  midnight.set(kRtc, 0x06, 0x12);  // 12 AM.
  peripherals::Sensors midnightSensors;
  midnightSensors.begin(midnight, 0);
  assert(midnightSensors.snapshot().rtcTimeValid && midnightSensors.snapshot().rtcHour == 0);
}

void testExplicitSetAndStrictValidation() {
  TwoWire bus;
  setValidRtc(bus);
  bus.set(kRtc, 0x00, 0x83);  // Preserve EXT_TEST/CAP_SEL; clear 12-hour mode.
  peripherals::Sensors sensors;
  sensors.begin(bus, 0);
  const int beforeWrites = bus.rtcWriteTransactions;
  const int beforeTransactions = bus.transactionCount;
  assert(!sensors.setRtcDateTime(2023, 2, 29, 12, 0, 0));
  assert(!sensors.setRtcDateTime(2100, 1, 1, 0, 0, 0));
  assert(!sensors.setRtcDateTime(2024, 1, 1, 24, 0, 0));
  assert(bus.rtcWriteTransactions == beforeWrites);
  assert(bus.transactionCount == beforeTransactions);
  assert(sensors.snapshot().rtcTimeValid);  // A rejected request leaves prior trusted data intact.

  const uint32_t beforeVerifiedSet = sensors.snapshot().rtcSampledAtMs;
  bus.set(kRtc, 0x04, 0xd7);  // Prior OS flag is cleared by the explicit seconds write.
  assert(sensors.setRtcDateTime(2024, 2, 29, 23, 58, 57));
  assert(sensors.snapshot().rtcSampledAtMs > beforeVerifiedSet);
  assert((bus.get(kRtc, 0x00) & 0x20) == 0);
  assert((bus.get(kRtc, 0x00) & 0x02) == 0);
  assert((bus.get(kRtc, 0x00) & 0x81) == 0x81);
  assert(bus.get(kRtc, 0x04) == 0x57);  // Writing seconds clears OS.
  assert(bus.get(kRtc, 0x05) == 0x58 && bus.get(kRtc, 0x06) == 0x23);
  assert(bus.get(kRtc, 0x07) == 0x29 && bus.get(kRtc, 0x08) == 4);
  assert(bus.get(kRtc, 0x09) == 0x02 && bus.get(kRtc, 0x0a) == 0x24);
  assert(sensors.snapshot().rtcTimeValid);
}

void testFailedWriteRestartsAndNeverRevalidatesPartialCalendar() {
  TwoWire bus;
  setValidRtc(bus);
  peripherals::Sensors sensors;
  sensors.begin(bus, 0);
  // Set fails on the single calendar burst after the control STOP write.
  bus.failTransaction = bus.transactionCount + 3;
  assert(!sensors.setRtcDateTime(2025, 1, 2, 3, 4, 5));
  assert((bus.get(kRtc, 0x00) & 0x20) == 0);  // Best-effort cleanup restarted it.
  assert(!sensors.snapshot().rtcTimeValid);
  sensors.update(1000);
  assert(!sensors.snapshot().rtcTimeValid);  // The failure latch blocks partial-data promotion.
}

void testFailedSetPreservesOriginallyStoppedClock() {
  TwoWire bus;
  setValidRtc(bus);
  bus.set(kRtc, 0x00, 0x20);
  peripherals::Sensors sensors;
  sensors.begin(bus, 0);
  bus.failTransaction = bus.transactionCount + 3;  // Fail the calendar burst.
  assert(!sensors.setRtcDateTime(2025, 1, 2, 3, 4, 5));
  assert(bus.get(kRtc, 0x00) == 0x20);
  assert(!sensors.snapshot().rtcTimeValid);
}

void testShortReadbackCannotValidateSet() {
  TwoWire bus;
  setValidRtc(bus);
  peripherals::Sensors sensors;
  sensors.begin(bus, 0);
  bus.shortReadRequest = bus.requestCount + 2;
  assert(!sensors.setRtcDateTime(2025, 1, 2, 3, 4, 5));
  assert((bus.get(kRtc, 0x00) & 0x20) == 0);  // Cleanup still restarts original running clock.
  assert(!sensors.snapshot().rtcTimeValid);
}

void testRestartReadbackFailureRestoresOriginalStoppedControl() {
  TwoWire bus;
  setValidRtc(bus);
  bus.set(kRtc, 0x00, 0x20);
  peripherals::Sensors sensors;
  sensors.begin(bus, 0);
  bus.shortReadRequest = bus.requestCount + 3;  // Fail the post-restart Control_1 readback.
  assert(!sensors.setRtcDateTime(2025, 1, 2, 3, 4, 5));
  assert(bus.get(kRtc, 0x00) == 0x20);
  assert(!sensors.snapshot().rtcTimeValid);
}

void testShortRtcControlWriteIsReportedAsFailure() {
  TwoWire bus;
  setValidRtc(bus);
  peripherals::Sensors sensors;
  sensors.begin(bus, 0);
  bus.writeLimit_ = 1;  // Register address is accepted, value byte is rejected.
  assert(!sensors.setRtcDateTime(2025, 1, 2, 3, 4, 5));
  assert(!sensors.snapshot().rtcTimeValid);
  assert((bus.get(kRtc, 0x00) & 0x20) == 0);
}

void testImuSamplingRestoresReadyAfterTransientReadError() {
  TwoWire bus;
  setValidRtc(bus);
  bus.set(kQmi, 0x00, 0x05);
  bus.set(kQmi, 0x4d, 0x80);
  peripherals::Sensors sensors;
  sensors.begin(bus, 0);
  assert(sensors.snapshot().imuReady && sensors.snapshot().accelerationReady);
  bus.failTransaction = bus.transactionCount + 1;
  sensors.update(300);
  assert(!sensors.snapshot().imuReady && !sensors.snapshot().accelerationReady);
  sensors.update(600);
  assert(sensors.snapshot().imuReady && sensors.snapshot().accelerationReady);
  assert(sensors.snapshot().imuErrors == 1);
}
}  // namespace

int main() {
  testBootReadsWithoutSettingRtc();
  testRtcSampleTimestampTracksCompleteReadsOnly();
  testInvalidBcdAndImpossibleCalendar();
  testTwelveHourDecoding();
  testExplicitSetAndStrictValidation();
  testFailedWriteRestartsAndNeverRevalidatesPartialCalendar();
  testFailedSetPreservesOriginallyStoppedClock();
  testShortReadbackCannotValidateSet();
  testRestartReadbackFailureRestoresOriginalStoppedControl();
  testShortRtcControlWriteIsReportedAsFailure();
  testImuSamplingRestoresReadyAfterTransientReadError();
  std::cout << "native sensor tests passed\n";
}
