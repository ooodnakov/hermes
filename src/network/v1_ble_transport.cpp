#include "v1_ble_transport.h"

#include <cstring>

namespace network {

bool V1BleTransport::enqueueInput(const uint8_t* bytes, size_t length) {
  if (!bytes || !length) return length == 0;
  if (length > kInputCapacity - inputSize_) {
    ++inputQueueDrops_;
    // A partial callback payload must never become a valid command later.
    inputHead_ = inputSize_ = lineSize_ = 0;
    invalid_ = false;
    discarding_ = true;
    return false;
  }
  for (size_t i = 0; i < length; ++i) {
    input_[(inputHead_ + inputSize_) % kInputCapacity] = bytes[i];
    ++inputSize_;
  }
  if (inputSize_ > inputHighWater_) inputHighWater_ = inputSize_;
  return true;
}

V1BleTransport::InputResult V1BleTransport::nextLine() {
  while (inputSize_) {
    const uint8_t byte = input_[inputHead_];
    inputHead_ = (inputHead_ + 1) % kInputCapacity;
    --inputSize_;
    if (byte == '\n') {
      if (discarding_) {
        discarding_ = false;
        lineSize_ = 0;
        return InputResult::LineTooLong;
      }
      if (invalid_) {
        invalid_ = false;
        lineSize_ = 0;
        return InputResult::InvalidLine;
      }
      if (lineSize_ && line_[lineSize_ - 1] == '\r') --lineSize_;
      if (!lineSize_) continue;
      line_[lineSize_] = '\0';
      lineSize_ = 0;
      return InputResult::LineReady;
    }
    if (discarding_) continue;
    if (byte == 0) {
      invalid_ = true;
      continue;
    }
    if (lineSize_ == kMaxLine) {
      discarding_ = true;
      invalid_ = false;
      lineSize_ = 0;
      continue;
    }
    line_[lineSize_++] = static_cast<char>(byte);
  }
  return InputResult::None;
}

bool V1BleTransport::enqueueOutput(const char* line) {
  if (!line) return false;
  const size_t length = strnlen(line, kMaxLine + 1);
  if (length > kMaxLine || length + 1 > kOutputCapacity - outputSize_) {
    ++outputQueueDrops_;
    return false;
  }
  for (size_t i = 0; i < length; ++i) {
    output_[(outputHead_ + outputSize_) % kOutputCapacity] = static_cast<uint8_t>(line[i]);
    ++outputSize_;
  }
  output_[(outputHead_ + outputSize_) % kOutputCapacity] = '\n';
  ++outputSize_;
  if (outputSize_ > outputHighWater_) outputHighWater_ = outputSize_;
  return true;
}

size_t V1BleTransport::peekOutput(uint8_t* target, size_t capacity) const {
  if (!target || !capacity) return 0;
  const size_t count = outputSize_ < capacity ? outputSize_ : capacity;
  for (size_t i = 0; i < count; ++i) target[i] = output_[(outputHead_ + i) % kOutputCapacity];
  return count;
}

void V1BleTransport::consumeOutput(size_t length) {
  if (length > outputSize_) length = outputSize_;
  outputHead_ = (outputHead_ + length) % kOutputCapacity;
  outputSize_ -= length;
}

void V1BleTransport::reset() {
  inputHead_ = inputSize_ = outputHead_ = outputSize_ = lineSize_ = 0;
  discarding_ = invalid_ = false;
}

}  // namespace network
