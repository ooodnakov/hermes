#pragma once

#include <stddef.h>

namespace protocol {

// Bounded newline framing for byte streams. Capacity includes the trailing
// NUL, so at most Capacity - 1 bytes are accepted in one line.
template <size_t Capacity = 4096>
class SerialLineFramer {
  static_assert(Capacity >= 2, "serial line capacity must include data and NUL");

 public:
  enum class Result { None, Empty, LineReady, LineTooLong, InvalidLine };

  Result push(char byte) {
    if (byte == '\n') {
      Result result = Result::Empty;
      if (overflow_) result = Result::LineTooLong;
      else if (invalid_) result = Result::InvalidLine;
      else if (length_ != 0) {
        if (buffer_[length_ - 1] == '\r') --length_;
        buffer_[length_] = '\0';
        result = length_ == 0 ? Result::Empty : Result::LineReady;
      }
      reset();
      return result;
    }

    if (overflow_ || invalid_) return Result::None;
    if (byte == '\0') {
      length_ = 0;
      invalid_ = true;
      return Result::None;
    }
    if (length_ == Capacity - 1) {
      length_ = 0;
      overflow_ = true;
      return Result::None;
    }
    buffer_[length_++] = byte;
    return Result::None;
  }

  const char* line() const { return buffer_; }

 private:
  void reset() {
    length_ = 0;
    overflow_ = false;
    invalid_ = false;
  }

  char buffer_[Capacity]{};
  size_t length_ = 0;
  bool overflow_ = false;
  bool invalid_ = false;
};

}  // namespace protocol
