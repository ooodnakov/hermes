#include "input/axs15231b_touch.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

std::array<uint8_t, 8> packet(uint8_t count, uint16_t longAxis = 0, uint16_t shortAxis = 0) {
  return {0, count,
          static_cast<uint8_t>((longAxis >> 8) & 0x0F), static_cast<uint8_t>(longAxis & 0xFF),
          static_cast<uint8_t>((shortAxis >> 8) & 0x0F), static_cast<uint8_t>(shortAxis & 0xFF),
          0, 0};
}

void setResponse(TwoWire& bus, const std::array<uint8_t, 8>& bytes) {
  bus.response.assign(bytes.begin(), bytes.end());
}

void testMappingCorners() {
  TwoWire bus;
  input::Axs15231bTouch touch(bus);
  input::TouchPoint point{};
  setResponse(bus, packet(1, 639, 171));
  require(touch.readSample(point) == input::TouchStatus::Pressed, "raw upper-left corner sample is pressed");
  require(point.x == 0 && point.y == 0, "raw upper-left corner maps to logical upper-left");
  require(touch.diagnostics().x == 0 && touch.diagnostics().y == 0,
          "upper-left touch diagnostics report mirrored logical coordinates");

  setResponse(bus, packet(1, 0, 171));
  require(touch.readSample(point) == input::TouchStatus::Pressed, "raw upper-right corner sample is pressed");
  require(point.x == 639 && point.y == 0, "raw upper-right corner maps to logical upper-right");
  require(touch.diagnostics().x == 639 && touch.diagnostics().y == 0,
          "upper-right touch diagnostics report logical coordinates");

  setResponse(bus, packet(1, 639, 0));
  require(touch.readSample(point) == input::TouchStatus::Pressed, "raw lower-left corner sample is pressed");
  require(point.x == 0 && point.y == 171, "raw lower-left corner maps to logical lower-left");
  require(touch.diagnostics().x == 0 && touch.diagnostics().y == 171,
          "lower-left touch diagnostics report mirrored logical coordinates");

  setResponse(bus, packet(1, 0, 0));
  require(touch.readSample(point) == input::TouchStatus::Pressed, "raw lower-right corner sample is pressed");
  require(point.x == 639 && point.y == 171, "raw lower-right corner maps to logical lower-right");
  require(touch.diagnostics().x == 639 && touch.diagnostics().y == 171,
          "lower-right touch diagnostics report logical coordinates");
}

void testReleaseDebounce() {
  TwoWire bus;
  input::Axs15231bTouch touch(bus);
  const auto pressed = packet(1, 321, 72);
  input::TouchPoint held{};
  setResponse(bus, pressed);
  require(touch.readSample(held) == input::TouchStatus::Pressed, "initial press is reported");
  setResponse(bus, packet(0));
  require(touch.readSample(held) == input::TouchStatus::Pressed, "first empty sample is debounced");
  require(held.x == 318 && held.y == 99, "debounce repeats the last valid mirrored point");
  setResponse(bus, packet(0));
  require(touch.readSample(held) == input::TouchStatus::Released, "second empty sample releases");
}

enum class ErrorKind { ShortWrite, Wire, ShortRequest, ShortAvailable, Range };

void testErrorCancelsPriorTouch(ErrorKind kind, const std::string& name) {
  TwoWire bus;
  input::Axs15231bTouch touch(bus);
  input::TouchPoint point{};
  setResponse(bus, packet(1, 123, 45));
  require(touch.readSample(point) == input::TouchStatus::Pressed, name + ": establish active press");

  setResponse(bus, packet(1, 124, 45));
  if (kind == ErrorKind::ShortWrite) bus.writeCount = 10;
  if (kind == ErrorKind::Wire) bus.wireError = 2;
  if (kind == ErrorKind::ShortRequest) bus.reportedBytes = 7;
  if (kind == ErrorKind::ShortAvailable) bus.response.resize(7);
  if (kind == ErrorKind::Range) setResponse(bus, packet(1, 640, 45));
  require(touch.readSample(point) == input::TouchStatus::Error, name + ": injected failure is an error");

  setResponse(bus, packet(0));
  bus.writeCount = -1;
  bus.wireError = 0;
  bus.reportedBytes = -1;
  require(touch.readSample(point) == input::TouchStatus::Released, name + ": next empty sample releases without stale press");
  require(touch.readSample(point) == input::TouchStatus::Released, name + ": later empty remains released");
}

}  // namespace

int main() {
  testMappingCorners();
  testReleaseDebounce();
  testErrorCancelsPriorTouch(ErrorKind::ShortWrite, "short write");
  testErrorCancelsPriorTouch(ErrorKind::Wire, "wire error");
  testErrorCancelsPriorTouch(ErrorKind::ShortRequest, "short request");
  testErrorCancelsPriorTouch(ErrorKind::ShortAvailable, "short available");
  testErrorCancelsPriorTouch(ErrorKind::Range, "out of range");
  std::cout << "AXS15231B native touch tests passed\n";
  return 0;
}
