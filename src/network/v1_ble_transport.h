#pragma once

#include <stddef.h>
#include <stdint.h>

namespace network {

// Fixed-storage queues shared by the NimBLE callbacks and the application loop.
// Callers provide synchronization: this type deliberately has no Arduino/RTOS
// dependency so its framing and backpressure behavior can be tested natively.
class V1BleTransport {
 public:
  static constexpr size_t kMaxLine = 4095;
  static constexpr size_t kInputCapacity = 8192;
  static constexpr size_t kOutputCapacity = 8192;

  enum class InputResult { None, LineReady, InvalidLine, LineTooLong };

  bool enqueueInput(const uint8_t* bytes, size_t length);
  InputResult nextLine();
  const char* line() const { return line_; }
  bool enqueueOutput(const char* line);
  size_t peekOutput(uint8_t* target, size_t capacity) const;
  void consumeOutput(size_t length);
  void reset();

  size_t inputSize() const { return inputSize_; }
  size_t outputSize() const { return outputSize_; }
  size_t inputHighWater() const { return inputHighWater_; }
  size_t outputHighWater() const { return outputHighWater_; }
  uint32_t inputQueueDrops() const { return inputQueueDrops_; }
  uint32_t outputQueueDrops() const { return outputQueueDrops_; }

 private:
  uint8_t input_[kInputCapacity]{};
  uint8_t output_[kOutputCapacity]{};
  char line_[kMaxLine + 1]{};
  size_t inputHead_ = 0, inputSize_ = 0;
  size_t outputHead_ = 0, outputSize_ = 0;
  size_t lineSize_ = 0;
  size_t inputHighWater_ = 0, outputHighWater_ = 0;
  uint32_t inputQueueDrops_ = 0, outputQueueDrops_ = 0;
  bool discarding_ = false, invalid_ = false;
};

}  // namespace network
