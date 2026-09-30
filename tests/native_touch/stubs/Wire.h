#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

class TwoWire {
 public:
  void beginTransmission(uint8_t) {}
  size_t write(const uint8_t*, size_t size) {
    return writeCount < 0 ? size : static_cast<size_t>(writeCount);
  }
  uint8_t endTransmission(bool) { return wireError; }
  size_t requestFrom(uint8_t, size_t, bool) {
    rx.assign(response.begin(), response.begin() + response.size());
    return reportedBytes < 0 ? rx.size() : static_cast<size_t>(reportedBytes);
  }
  int available() const { return static_cast<int>(rx.size()); }
  int read() {
    if (rx.empty()) return -1;
    const int value = rx.front();
    rx.pop_front();
    return value;
  }

  int writeCount = -1;
  uint8_t wireError = 0;
  int reportedBytes = -1;
  std::vector<uint8_t> response = std::vector<uint8_t>(8, 0);

 private:
  std::deque<uint8_t> rx;
};
