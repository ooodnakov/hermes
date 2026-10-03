#pragma once
#include <cstddef>
#include <cstdint>

class Print {
 public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t value) = 0;
  virtual size_t write(const uint8_t* data, size_t size) {
    size_t written = 0;
    while (written < size && write(data[written])) ++written;
    return written;
  }
};

class Stream : public Print {
 public:
  virtual int read() = 0;
  virtual size_t readBytes(char* buffer, size_t length) = 0;
};
