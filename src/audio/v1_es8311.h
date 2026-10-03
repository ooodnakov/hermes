#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/portmacro.h>

namespace audio {

enum class DiagnosticError : uint8_t {
  None = 0,
  NotReady,
  Busy,
  I2sWrite,
  CodecMute,
  AmpEnable,
  AmpDisable,
  OwnerTimeout,
  GainRejected,
  GainWrite,
  GainReadback,
  WifiUnavailable,
  UrlRejected,
  TaskCreate,
  HttpBegin,
  HttpStatus,
  HttpEncoding,
  HttpTimeout,
  HttpTooLarge,
  PcmMalformed,
  OutputCleanup,
  QuietMode,
  Unknown,
};

struct Diagnostics {
  uint32_t chirpRequests = 0;
  uint32_t chirpSuccesses = 0;
  uint32_t chirpFailures = 0;
  uint32_t chirpSkips = 0;
  uint32_t i2sWriteFailures = 0;
  int32_t lastI2sError = 0;
  uint32_t lastI2sExpectedBytes = 0;
  uint32_t lastI2sWrittenBytes = 0;
  uint8_t dacVolumeRegister = 0x60;
  uint32_t testRequests = 0;
  int32_t lastTestGainRequested = 0;
  uint8_t lastTestGainReadback = 0;
  bool baselineRestored = true;
  uint32_t speechRequests = 0;
  uint32_t speechSuccesses = 0;
  uint32_t speechFailures = 0;
  uint32_t speechSkips = 0;
  uint32_t speechBytesReceived = 0;
  uint32_t lastSpeechBytes = 0;
  int32_t lastHttpStatus = 0;
  DiagnosticError lastError = DiagnosticError::None;
};

const char* diagnosticErrorName(DiagnosticError error);

class V1Es8311 {
 public:
  bool begin();
  void disable();
  void setQuiet(bool quiet);
  bool quiet() const;
  bool ready() const { return ready_; }
  bool playMonoPcm(const int16_t* samples, size_t count);
  bool chirp(const char* kind);
  bool audioTest(int gainRegister);
  bool playUrl(const char* url);
  Diagnostics diagnostics() const;

 private:
  bool writeMonoSamples(const int16_t* samples, size_t count, uint32_t timeoutMs);
  bool emitTone(uint16_t hz, uint16_t ms, bool& outputActive);
  bool playUrlWorker(const String& url);
  bool claimSpeech();
  void releaseSpeech();
  bool claimChirp();
  void releaseChirp();
  bool claimPcm();
  void releasePcm();
  static void urlTask(void* context);
  bool ready_ = false;
  bool speechBusy_ = false;
  bool chirpBusy_ = false;
  bool pcmBusy_ = false;
  bool quiet_ = false;
  mutable portMUX_TYPE stateMux_ = portMUX_INITIALIZER_UNLOCKED;
};

extern V1Es8311 v1Playback;

}  // namespace audio
