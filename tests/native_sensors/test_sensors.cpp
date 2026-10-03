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

void setAccel(TwoWire& bus, int16_t x, int16_t y, int16_t z) {
  const int16_t values[] = {x, y, z};
  for (size_t axis = 0; axis < 3; ++axis) {
    const uint16_t raw = static_cast<uint16_t>(values[axis]);
    bus.set(kQmi, static_cast<uint8_t>(0x35 + axis * 2), static_cast<uint8_t>(raw));
    bus.set(kQmi, static_cast<uint8_t>(0x36 + axis * 2), static_cast<uint8_t>(raw >> 8));
  }
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

void testLearnedOrientationGesturesAndShake() {
  TwoWire bus;
  setValidRtc(bus);
  bus.set(kQmi, 0x00, 0x05);
  bus.set(kQmi, 0x4d, 0x80);
  setAccel(bus, 0, 0, 8192);
  peripherals::Sensors sensors;
  sensors.begin(bus, 0);
  for (uint32_t now = 300; now <= 3300; now += 300) sensors.update(now);

  setAccel(bus, 0, 0, -8192);
  for (uint32_t now = 3600; now <= 5100; now += 300) sensors.update(now);
  assert(sensors.snapshot().gesture == peripherals::Gesture::FaceDown);
  const uint32_t faceDownSequence = sensors.snapshot().gestureSequence;
  assert(faceDownSequence == 1);

  setAccel(bus, 0, 0, 8192);
  for (uint32_t now = 5400; now <= 6300; now += 300) sensors.update(now);
  assert(sensors.snapshot().gesture == peripherals::Gesture::Upright);
  assert(sensors.snapshot().gestureSequence == faceDownSequence + 1);

  setAccel(bus, 0, 0, 16384);
  sensors.update(6600);
  assert(sensors.snapshot().gesture == peripherals::Gesture::Shake);
  assert(sensors.snapshot().gestureSequence == faceDownSequence + 2);

  setAccel(bus, 0, 0, 8192);
  sensors.update(6900);
  setAccel(bus, 0, 0, 10240);
  sensors.update(9000);
  setAccel(bus, 0, 0, 8192);
  sensors.update(9300);
  assert(sensors.snapshot().gesture != peripherals::Gesture::Tap2);  // One knock plus its return edge.
  setAccel(bus, 0, 0, 10240);
  sensors.update(9600);
  setAccel(bus, 0, 0, 8192);
  sensors.update(9900);
  assert(sensors.snapshot().gesture == peripherals::Gesture::Tap2);
  assert(sensors.snapshot().gestureSequence == faceDownSequence + 3);
}

void testShortKnockPulsesBetweenLegacyPollsAndNoReturnEdgeDoubleCount() {
  TwoWire bus;
  setValidRtc(bus);
  bus.set(kQmi, 0x00, 0x05);
  bus.set(kQmi, 0x4d, 0x80);
  setAccel(bus, 0, 0, 8192);
  peripherals::Sensors sensors;
  sensors.begin(bus, 0);
  for (uint32_t now = 20; now <= 3020; now += 20) sensors.update(now);
  assert(sensors.snapshot().gestureSequence == 0);

  // Both 20ms pulses fall wholly between the former 300ms poll times
  // (3000/3300 and 3600/3900), so the previous scheduler could not see them.
  for (uint32_t now = 3040; now < 3100; now += 20) sensors.update(now);
  setAccel(bus, 0, 0, 10650);
  sensors.update(3100);
  setAccel(bus, 0, 0, 8192);
  sensors.update(3120);
  assert(sensors.snapshot().gesture == peripherals::Gesture::None);
  assert(sensors.snapshot().gestureSequence == 0);

  for (uint32_t now = 3140; now < 3700; now += 20) sensors.update(now);
  setAccel(bus, 0, 0, 10650);
  sensors.update(3700);
  assert(sensors.snapshot().gesture == peripherals::Gesture::Tap2);
  assert(sensors.snapshot().gestureSequence == 1);
  setAccel(bus, 0, 0, 8192);
  sensors.update(3720);
  assert(sensors.snapshot().gesture == peripherals::Gesture::Tap2);
  assert(sensors.snapshot().gestureSequence == 1);  // Falling edge is not another knock.
}

void testQualificationDurationsAndMillisWrap() {
  constexpr uint32_t start = 0xfffff000u;
  TwoWire bus;
  setValidRtc(bus);
  bus.set(kQmi, 0x00, 0x05);
  bus.set(kQmi, 0x4d, 0x80);
  setAccel(bus, 0, 0, 8192);
  peripherals::Sensors sensors;
  sensors.begin(bus, start);
  for (uint32_t elapsed = 20; elapsed <= 3020; elapsed += 20)
    sensors.update(start + elapsed);
  assert(sensors.snapshot().gestureSequence == 0);

  setAccel(bus, 0, 0, -8192);
  sensors.update(start + 3040);
  for (uint32_t elapsed = 3060; elapsed < 4520; elapsed += 20)
    sensors.update(start + elapsed);
  assert(sensors.snapshot().gestureSequence == 0);  // 1480ms is short of face-down qualification.
  sensors.update(start + 4540);
  assert(sensors.snapshot().gesture == peripherals::Gesture::FaceDown);
  assert(sensors.snapshot().gestureSequence == 1);

  setAccel(bus, 0, 0, 8192);
  for (uint32_t elapsed = 4560; elapsed < 5440; elapsed += 20)
    sensors.update(start + elapsed);
  assert(sensors.snapshot().gesture == peripherals::Gesture::FaceDown);
  assert(sensors.snapshot().gestureSequence == 1);  // 880ms is short of upright qualification.
  sensors.update(start + 5460);
  assert(sensors.snapshot().gesture == peripherals::Gesture::Upright);
  assert(sensors.snapshot().gestureSequence == 2);
}

void testBaseQualificationRestartsAfterDisturbance() {
  TwoWire bus;
  setValidRtc(bus);
  bus.set(kQmi, 0x00, 0x05);
  bus.set(kQmi, 0x4d, 0x80);
  setAccel(bus, 0, 0, 8192);
  peripherals::Sensors sensors;
  sensors.begin(bus, 0);
  for (uint32_t now = 20; now <= 2980; now += 20) sensors.update(now);

  // A magnitude disturbance just before the 3s mark invalidates that run.
  setAccel(bus, 0, 0, 10650);
  sensors.update(3000);
  setAccel(bus, 0, 0, 8192);
  sensors.update(3020);
  for (uint32_t now = 3040; now < 3500; now += 20) sensors.update(now);

  // Tilt before a fresh 3s stable baseline has elapsed. An early poll-count
  // calibration would already be set and would incorrectly emit FaceDown.
  setAccel(bus, 0, 0, -8192);
  for (uint32_t now = 3500; now <= 5000; now += 20) sensors.update(now);
  assert(sensors.snapshot().gestureSequence == 0);
}

void testImuOutageCancelsTapAndReanchorsMotion() {
  TwoWire bus;
  setValidRtc(bus);
  bus.set(kQmi, 0x00, 0x05);
  bus.set(kQmi, 0x4d, 0x80);
  setAccel(bus, 0, 0, 8192);
  peripherals::Sensors sensors;
  sensors.begin(bus, 0);
  for (uint32_t now = 300; now <= 3300; now += 300) sensors.update(now);
  setAccel(bus, 0, 0, 10240);
  sensors.update(3600);  // First half of a real tap pair.
  bus.failTransaction = bus.transactionCount + 1;  // Only the IMU burst is due at this update.
  sensors.update(3900);  // Drop the IMU burst.
  assert(!sensors.snapshot().accelerationReady);
  setAccel(bus, 0, 0, 16384);
  sensors.update(4200);  // Discontinuous post-outage sample is only an anchor.
  assert(sensors.snapshot().gesture == peripherals::Gesture::None);
  sensors.update(4500);
  assert(sensors.snapshot().gesture == peripherals::Gesture::None);
}

void testPickupCanFollowTwoMinutesOfStillnessFromBoot() {
  TwoWire bus;
  setValidRtc(bus);
  bus.set(kQmi, 0x00, 0x05);
  bus.set(kQmi, 0x4d, 0x80);
  setAccel(bus, 0, 0, 8192);
  peripherals::Sensors sensors;
  sensors.begin(bus, 0);
  for (uint32_t now = 300; now <= 3000; now += 300) sensors.update(now);
  sensors.update(120000);
  setAccel(bus, 0, 0, 10650);
  sensors.update(120300);
  assert(sensors.snapshot().gesture == peripherals::Gesture::Pickup);
}

void testImuOutagePreservesLearnedOrientationAndQuietRecovery() {
  TwoWire bus;
  setValidRtc(bus);
  bus.set(kQmi, 0x00, 0x05);
  bus.set(kQmi, 0x4d, 0x80);
  setAccel(bus, 0, 0, 8192);
  peripherals::Sensors sensors;
  sensors.begin(bus, 0);
  for (uint32_t now = 300; now <= 3300; now += 300) sensors.update(now);

  setAccel(bus, 0, 0, -8192);
  for (uint32_t now = 3600; now <= 5100; now += 300) sensors.update(now);
  assert(sensors.snapshot().gesture == peripherals::Gesture::FaceDown);

  bus.failTransaction = bus.transactionCount + 1;  // Only the IMU burst is due at this update.
  sensors.update(5400);
  assert(!sensors.snapshot().accelerationReady);
  sensors.update(5700);  // First recovered sample reanchors magnitude only.
  for (uint32_t now = 6000; now <= 7200; now += 300) sensors.update(now);
  assert(sensors.snapshot().gesture == peripherals::Gesture::FaceDown);

  setAccel(bus, 0, 0, 8192);
  for (uint32_t now = 7500; now <= 8400; now += 300) sensors.update(now);
  assert(sensors.snapshot().gesture == peripherals::Gesture::Upright);
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
  testLearnedOrientationGesturesAndShake();
  testShortKnockPulsesBetweenLegacyPollsAndNoReturnEdgeDoubleCount();
  testQualificationDurationsAndMillisWrap();
  testBaseQualificationRestartsAfterDisturbance();
  testImuOutageCancelsTapAndReanchorsMotion();
  testPickupCanFollowTwoMinutesOfStillnessFromBoot();
  testImuOutagePreservesLearnedOrientationAndQuietRecovery();
  std::cout << "native sensor tests passed\n";
}
