#include "axs15231b_touch.h"

#include <cstring>

namespace input {
namespace {
constexpr uint8_t kAddress = 0x3B;
constexpr uint8_t kReadCommand[] = {0xB5, 0xAB, 0xA5, 0x5A, 0, 0, 0, 0x08, 0, 0, 0};
constexpr size_t kResponseBytes = 8;
constexpr uint16_t kRawLongAxisMax = 639;
constexpr uint16_t kRawShortAxisMax = 171;
constexpr uint8_t kReleaseConfirmSamples = 2;

void increment(uint32_t& value) {
  if (value != UINT32_MAX) ++value;
}

void invalidateTouch(bool& touchActive, uint8_t& emptyTouchSamples, TouchPoint& lastPoint) {
  touchActive = false;
  emptyTouchSamples = 0;
  lastPoint = {};
}
}

bool Axs15231bTouch::read(TouchPoint& point) {
  return readSample(point) == TouchStatus::Pressed;
}

TouchStatus Axs15231bTouch::readSample(TouchPoint& point) {
  increment(diagnostics_.samples);
  uint8_t response[kResponseBytes] = {};

  bus_.beginTransmission(kAddress);
  if (bus_.write(kReadCommand, sizeof(kReadCommand)) != sizeof(kReadCommand)) {
    increment(diagnostics_.busErrors);
    invalidateTouch(touchActive_, emptyTouchSamples_, lastPoint_);
    diagnostics_.lastStatus = TouchStatus::Error;
    return TouchStatus::Error;
  }
  diagnostics_.lastWireError = bus_.endTransmission(false);
  if (diagnostics_.lastWireError != 0) {
    increment(diagnostics_.busErrors);
    invalidateTouch(touchActive_, emptyTouchSamples_, lastPoint_);
    diagnostics_.lastStatus = TouchStatus::Error;
    return TouchStatus::Error;
  }
  const size_t received = bus_.requestFrom(kAddress, static_cast<size_t>(kResponseBytes), true);
  if (received != kResponseBytes) {
    while (bus_.available()) bus_.read();
    increment(diagnostics_.shortReads);
    invalidateTouch(touchActive_, emptyTouchSamples_, lastPoint_);
    diagnostics_.lastStatus = TouchStatus::Error;
    return TouchStatus::Error;
  }
  for (size_t i = 0; i < sizeof(response); ++i) {
    if (!bus_.available()) {
      increment(diagnostics_.shortReads);
      invalidateTouch(touchActive_, emptyTouchSamples_, lastPoint_);
      diagnostics_.lastStatus = TouchStatus::Error;
      return TouchStatus::Error;
    }
    response[i] = static_cast<uint8_t>(bus_.read());
  }
  std::memcpy(diagnostics_.lastResponse, response, sizeof(response));

  // Match the known-working board handler: use the first point for controller
  // counts 1..4, treating >=5 as empty, and confirm two empty samples before
  // ending a press so one transient zero packet cannot lose a gesture.
  diagnostics_.lastCount = response[1];
  if (response[1] == 0 || response[1] >= 5) {
    if (response[1] >= 5) increment(diagnostics_.highCountSamples);
    increment(diagnostics_.releasedSamples);
    if (touchActive_ && ++emptyTouchSamples_ < kReleaseConfirmSamples) {
      point = lastPoint_;
      diagnostics_.lastStatus = TouchStatus::Pressed;
      return TouchStatus::Pressed;
    }
    touchActive_ = false;
    emptyTouchSamples_ = 0;
    diagnostics_.lastStatus = TouchStatus::Released;
    return TouchStatus::Released;
  }
  emptyTouchSamples_ = 0;
  const uint16_t rawLongAxis = static_cast<uint16_t>(((response[2] & 0x0F) << 8) | response[3]);
  const uint16_t rawShortAxis = static_cast<uint16_t>(((response[4] & 0x0F) << 8) | response[5]);
  if (rawLongAxis > kRawLongAxisMax || rawShortAxis > kRawShortAxisMax) {
    increment(diagnostics_.outOfRange);
    invalidateTouch(touchActive_, emptyTouchSamples_, lastPoint_);
    diagnostics_.lastStatus = TouchStatus::Error;
    return TouchStatus::Error;
  }

  // The panel presenter maps logical (x, y) to native
  // (nativeX=171-y, nativeY=x). Based on the reported touch behavior, the
  // controller's horizontal polarity is reversed relative to the image, so
  // mirror x and keep the existing vertical mapping.
  point.x = static_cast<uint16_t>(kRawLongAxisMax - rawLongAxis);
  point.y = static_cast<uint16_t>(171 - rawShortAxis);
  touchActive_ = true;
  lastPoint_ = point;
  increment(diagnostics_.pressedSamples);
  std::memcpy(diagnostics_.lastTouchResponse, response, sizeof(response));
  diagnostics_.rawLongAxis = rawLongAxis;
  diagnostics_.rawShortAxis = rawShortAxis;
  diagnostics_.x = point.x;
  diagnostics_.y = point.y;
  diagnostics_.lastStatus = TouchStatus::Pressed;
  return TouchStatus::Pressed;
}

}  // namespace input
