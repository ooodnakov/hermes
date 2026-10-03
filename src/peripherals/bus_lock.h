#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace peripherals {

// Wire1 is shared by sensors, the ES8311 codec, and the TCA9554 expander.
// Recursive acquisition lets a public multi-register operation call the
// register helpers without exposing a partially protected sequence.
inline SemaphoreHandle_t wire1Mutex() {
  static SemaphoreHandle_t mutex = xSemaphoreCreateRecursiveMutex();
  return mutex;
}

class Wire1Lock {
 public:
  explicit Wire1Lock(TickType_t timeout = pdMS_TO_TICKS(100))
      : mutex_(wire1Mutex()), locked_(mutex_ && xSemaphoreTakeRecursive(mutex_, timeout) == pdTRUE) {}
  ~Wire1Lock() {
    if (locked_) xSemaphoreGiveRecursive(mutex_);
  }
  Wire1Lock(const Wire1Lock&) = delete;
  Wire1Lock& operator=(const Wire1Lock&) = delete;
  explicit operator bool() const { return locked_; }

 private:
  SemaphoreHandle_t mutex_;
  bool locked_;
};

}  // namespace peripherals
