#pragma once

#include <Arduino.h>
#include <Wire.h>

namespace input {

struct TouchPoint {
  uint16_t x;
  uint16_t y;
};

enum class TouchStatus : uint8_t { Pressed, Released, Error };

struct TouchDiagnostics {
  uint32_t samples = 0;
  uint32_t pressedSamples = 0;
  uint32_t releasedSamples = 0;
  uint32_t busErrors = 0;
  uint32_t shortReads = 0;
  uint32_t highCountSamples = 0;
  uint32_t outOfRange = 0;
  uint8_t lastWireError = 0;
  uint8_t lastCount = 0;
  uint8_t lastResponse[8] = {};
  uint8_t lastTouchResponse[8] = {};
  uint16_t rawLongAxis = 0;
  uint16_t rawShortAxis = 0;
  uint16_t x = 0;
  uint16_t y = 0;
  TouchStatus lastStatus = TouchStatus::Error;
};

// Borrows an already initialized TwoWire instance; V1 uses Wire1 on GPIO17/18.
class Axs15231bTouch {
 public:
  explicit Axs15231bTouch(TwoWire& bus) : bus_(bus) {}

  // Returns false on bus/protocol/range errors or when no finger is present.
  bool read(TouchPoint& point);
  // Distinguish an actual finger release from a failed bus read. A failed
  // sample must never complete a tap on an approval/action control.
  TouchStatus readSample(TouchPoint& point);
  const TouchDiagnostics& diagnostics() const { return diagnostics_; }

 private:
  TwoWire& bus_;
  TouchDiagnostics diagnostics_;
  TouchPoint lastPoint_{};
  uint8_t emptyTouchSamples_ = 0;
  bool touchActive_ = false;
};

}  // namespace input
