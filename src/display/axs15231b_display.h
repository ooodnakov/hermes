#pragma once

#include <stddef.h>
#include <stdint.h>

namespace display {

constexpr int kWidth = 640;
constexpr int kHeight = 172;

class Axs15231bDisplay {
 public:
  Axs15231bDisplay() = default;
  ~Axs15231bDisplay();
  Axs15231bDisplay(const Axs15231bDisplay&) = delete;
  Axs15231bDisplay& operator=(const Axs15231bDisplay&) = delete;

  bool begin();
  // Input is a complete 640x172 RGB565 frame. Partial rectangles are not
  // supported by the AXS15231B sequential QSPI stream used by this board.
  bool present(const uint16_t* logicalFrame, size_t pixelCount);
  bool ready() const { return panel_ != nullptr && !failed_ && !transferInFlight_; }
  size_t psramFrameBytes() const;

 private:
  void cleanup();
  void* io_ = nullptr;
  void* panel_ = nullptr;
  uint16_t* nativeFrame_ = nullptr;
  uint8_t* dmaChunk_ = nullptr;
  void* transferDone_ = nullptr;
  bool busInitialized_ = false;
  bool failed_ = false;
  bool transferInFlight_ = false;
};

}  // namespace display
