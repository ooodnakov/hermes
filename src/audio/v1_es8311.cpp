#include "v1_es8311.h"

#include "../boards/v1/board_config.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <Wire.h>
#include <driver/i2s_std.h>
#include <cstring>
#include <new>

namespace audio {
V1Es8311 v1Playback;

const char* diagnosticErrorName(DiagnosticError error) {
  switch (error) {
    case DiagnosticError::None: return "none";
    case DiagnosticError::NotReady: return "not_ready";
    case DiagnosticError::Busy: return "busy";
    case DiagnosticError::I2sWrite: return "i2s_write";
    case DiagnosticError::CodecMute: return "codec_mute";
    case DiagnosticError::AmpEnable: return "amp_enable";
    case DiagnosticError::AmpDisable: return "amp_disable";
    case DiagnosticError::OwnerTimeout: return "owner_timeout";
    case DiagnosticError::GainRejected: return "gain_rejected";
    case DiagnosticError::GainWrite: return "gain_write";
    case DiagnosticError::GainReadback: return "gain_readback";
    default: return "unknown";
  }
}

namespace {
constexpr uint8_t kCodecAddress = 0x18;  // ES8311 7-bit address (vendor config: 0x30).
constexpr uint8_t kExpanderP7 = 1U << 7;
constexpr gpio_num_t kMclkPin = GPIO_NUM_7;
constexpr gpio_num_t kBclkPin = GPIO_NUM_15;
constexpr gpio_num_t kLrckPin = GPIO_NUM_46;
constexpr gpio_num_t kDoutPin = GPIO_NUM_45;
constexpr uint32_t kSampleRate = 16000;
constexpr size_t kMonoChunkSamples = 128;
constexpr uint8_t kDacVolumeRegister = 0x60;
constexpr uint8_t kStepGainRegister = 0x80;
// Espressif volume=100 with Waveshare's PA gain=6 dB and its default 5V/3.3V
// voltage assumptions maps to -2.3909 dB, or DAC register 0xBA.
constexpr uint8_t kVendorMaxGainRegister = 0xBA;
constexpr uint32_t kI2sDmaDescriptors = 6;
constexpr uint32_t kI2sDmaFrames = 240;
constexpr uint32_t kDrainGuardMs = 10;
constexpr uint32_t kTxDrainMs =
    (kI2sDmaDescriptors * kI2sDmaFrames * 1000 + kSampleRate - 1) / kSampleRate + kDrainGuardMs;
static_assert(kTxDrainMs < 250, "manual audio feedback must remain bounded");
i2s_chan_handle_t txChannel = nullptr;
SemaphoreHandle_t audioMutex = nullptr;
Diagnostics audioDiagnostics;
portMUX_TYPE audioDiagnosticsMux = portMUX_INITIALIZER_UNLOCKED;

void setDiagnosticError(DiagnosticError error) {
  portENTER_CRITICAL(&audioDiagnosticsMux);
  audioDiagnostics.lastError = error;
  portEXIT_CRITICAL(&audioDiagnosticsMux);
}

void beginChirpDiagnostic() {
  portENTER_CRITICAL(&audioDiagnosticsMux);
  ++audioDiagnostics.chirpRequests;
  audioDiagnostics.lastError = DiagnosticError::None;
  audioDiagnostics.lastI2sError = 0;
  audioDiagnostics.lastI2sExpectedBytes = 0;
  audioDiagnostics.lastI2sWrittenBytes = 0;
  portEXIT_CRITICAL(&audioDiagnosticsMux);
}

void finishChirpDiagnostic(bool played) {
  portENTER_CRITICAL(&audioDiagnosticsMux);
  if (played) {
    ++audioDiagnostics.chirpSuccesses;
    audioDiagnostics.lastError = DiagnosticError::None;
  } else {
    ++audioDiagnostics.chirpFailures;
    if (audioDiagnostics.lastError == DiagnosticError::None) {
      audioDiagnostics.lastError = DiagnosticError::Unknown;
    }
  }
  portEXIT_CRITICAL(&audioDiagnosticsMux);
}

void skipChirpDiagnostic(DiagnosticError error) {
  portENTER_CRITICAL(&audioDiagnosticsMux);
  ++audioDiagnostics.chirpSkips;
  audioDiagnostics.lastError = error;
  portEXIT_CRITICAL(&audioDiagnosticsMux);
}

void recordI2sWrite(esp_err_t error, size_t expected, size_t written) {
  portENTER_CRITICAL(&audioDiagnosticsMux);
  audioDiagnostics.lastI2sError = error;
  audioDiagnostics.lastI2sExpectedBytes = expected;
  audioDiagnostics.lastI2sWrittenBytes = written;
  if (error != ESP_OK || written != expected) {
    ++audioDiagnostics.i2sWriteFailures;
    audioDiagnostics.lastError = DiagnosticError::I2sWrite;
  }
  portEXIT_CRITICAL(&audioDiagnosticsMux);
}

void recordDacGainReadback(uint8_t gain) {
  portENTER_CRITICAL(&audioDiagnosticsMux);
  audioDiagnostics.dacVolumeRegister = gain;
  portEXIT_CRITICAL(&audioDiagnosticsMux);
}

void beginGainTestDiagnostic(int32_t requested) {
  portENTER_CRITICAL(&audioDiagnosticsMux);
  ++audioDiagnostics.testRequests;
  audioDiagnostics.lastTestGainRequested = requested;
  audioDiagnostics.lastTestGainReadback = 0;
  audioDiagnostics.baselineRestored = false;
  audioDiagnostics.lastError = DiagnosticError::None;
  audioDiagnostics.lastI2sError = 0;
  audioDiagnostics.lastI2sExpectedBytes = 0;
  audioDiagnostics.lastI2sWrittenBytes = 0;
  portEXIT_CRITICAL(&audioDiagnosticsMux);
}

void finishGainTestDiagnostic(uint8_t testReadback, bool restored) {
  portENTER_CRITICAL(&audioDiagnosticsMux);
  audioDiagnostics.lastTestGainReadback = testReadback;
  audioDiagnostics.baselineRestored = restored;
  if (!restored && audioDiagnostics.lastError == DiagnosticError::None) {
    audioDiagnostics.lastError = DiagnosticError::GainWrite;
  }
  portEXIT_CRITICAL(&audioDiagnosticsMux);
}

bool codecWrite(uint8_t reg, uint8_t value) {
  Wire1.beginTransmission(kCodecAddress);
  Wire1.write(reg);
  Wire1.write(value);
  return Wire1.endTransmission(true) == 0;
}

bool codecRead(uint8_t reg, uint8_t& value) {
  Wire1.beginTransmission(kCodecAddress);
  Wire1.write(reg);
  if (Wire1.endTransmission(false) != 0 || Wire1.requestFrom(kCodecAddress, uint8_t(1)) != 1) return false;
  value = static_cast<uint8_t>(Wire1.read());
  return true;
}

bool codecSetMuted(bool muted) {
  // This read/modify/write matches Espressif's es8311_set_mute implementation.
  uint8_t value = 0;
  if (!codecRead(0x31, value)) {
    setDiagnosticError(DiagnosticError::CodecMute);
    return false;
  }
  value &= 0x9F;
  if (muted) value |= 0x60;
  if (!codecWrite(0x31, value)) {
    setDiagnosticError(DiagnosticError::CodecMute);
    return false;
  }
  return true;
}

bool setExpanderP7(bool enabled) {
  uint8_t output = 0, config = 0;
  Wire1.beginTransmission(board::kExpanderAddress);
  Wire1.write(0x01);
  if (Wire1.endTransmission(false) != 0 || Wire1.requestFrom(board::kExpanderAddress, uint8_t(1)) != 1) return false;
  output = static_cast<uint8_t>(Wire1.read());
  Wire1.beginTransmission(board::kExpanderAddress);
  Wire1.write(0x03);
  if (Wire1.endTransmission(false) != 0 || Wire1.requestFrom(board::kExpanderAddress, uint8_t(1)) != 1) return false;
  config = static_cast<uint8_t>(Wire1.read());

  // Preload the output latch before enabling the pin as an output.
  output = enabled ? static_cast<uint8_t>(output | kExpanderP7)
                   : static_cast<uint8_t>(output & ~kExpanderP7);
  // P7 is the codec/amplifier enable. Keep it as an output in both states;
  // low explicitly disables the amp instead of leaving the pin floating.
  config = static_cast<uint8_t>(config & ~kExpanderP7);
  Wire1.beginTransmission(board::kExpanderAddress);
  Wire1.write(0x01);
  Wire1.write(output);
  if (Wire1.endTransmission(true) != 0) return false;
  Wire1.beginTransmission(board::kExpanderAddress);
  Wire1.write(0x03);
  Wire1.write(config);
  return Wire1.endTransmission(true) == 0;
}

bool configureCodec() {
  // ES8311 initialization and 16 kHz clock coefficients follow Espressif's
  // Apache-2.0 ES8311 driver used by Waveshare's V1 Audio Test example.
  static constexpr uint8_t init[][2] = {
      {0x44, 0x08}, {0x44, 0x08}, {0x01, 0x30}, {0x02, 0x00},
      {0x03, 0x10}, {0x16, 0x24}, {0x04, 0x10}, {0x05, 0x00},
      {0x0B, 0x00}, {0x0C, 0x00}, {0x10, 0x1F}, {0x11, 0x7F},
      {0x00, 0x80}, {0x00, 0x80}, {0x01, 0x3F}, {0x06, 0x00},
      {0x13, 0x10}, {0x1B, 0x0A}, {0x1C, 0x6A}, {0x44, 0x58},
      // 16 kHz, 2.048 MHz MCLK (128 x sample rate): official coefficient row.
      // Its pre-multiplier is x2; BCLK is 512 kHz for 16-bit stereo slots.
      {0x02, 0x08}, {0x05, 0x00}, {0x03, 0x10}, {0x04, 0x20},
      {0x07, 0x00}, {0x08, 0xFF}, {0x06, 0x03},
      // I2S format, 16-bit samples; DAC path only.
      {0x09, 0x0C}, {0x0A, 0x4C}, {0x17, 0xBF}, {0x0E, 0x02},
      {0x12, 0x00}, {0x14, 0x1A}, {0x0D, 0x01}, {0x15, 0x40},
      {0x37, 0x08}, {0x45, 0x00},
      // Conservative initial DAC gain. Mute is applied below through the
      // same register operation as Espressif's ES8311 driver.
      {0x32, kDacVolumeRegister},
  };
  for (const auto& reg : init) if (!codecWrite(reg[0], reg[1])) return false;
  uint8_t reset = 0;
  uint8_t volume = 0;
  if (!codecRead(0x00, reset) || (reset & 0x80) == 0 || !codecSetMuted(true) ||
      !codecRead(0x32, volume)) return false;
  recordDacGainReadback(volume);
  if (volume != kDacVolumeRegister) {
    setDiagnosticError(DiagnosticError::GainReadback);
    return false;
  }
  return true;
}

bool writeAndVerifyDacGain(uint8_t gain, uint8_t* actualReadback = nullptr) {
  if (!codecWrite(0x32, gain)) {
    setDiagnosticError(DiagnosticError::GainWrite);
    return false;
  }
  uint8_t readback = 0;
  if (!codecRead(0x32, readback)) {
    setDiagnosticError(DiagnosticError::GainReadback);
    return false;
  }
  recordDacGainReadback(readback);
  if (actualReadback) *actualReadback = readback;
  if (readback != gain) {
    setDiagnosticError(DiagnosticError::GainReadback);
    return false;
  }
  return true;
}

bool enableOutput() {
  if (!codecSetMuted(false)) return false;
  if (!setExpanderP7(true)) {
    codecSetMuted(true);
    setDiagnosticError(DiagnosticError::AmpEnable);
    return false;
  }
  return true;
}

bool disableOutput(bool drainTx) {
  // i2s_channel_write may return while samples still occupy the DMA ring.
  // Drain the configured six 240-frame descriptors (90 ms at 16 kHz) before muting.
  if (drainTx) vTaskDelay(pdMS_TO_TICKS(kTxDrainMs));
  const bool muted = codecSetMuted(true);
  const bool ampDisabled = setExpanderP7(false);
  if (!ampDisabled) setDiagnosticError(DiagnosticError::AmpDisable);
  return muted && ampDisabled;
}

bool writeSamples(const int16_t* mono, size_t count, uint32_t timeoutMs) {
  int16_t stereo[kMonoChunkSamples * 2];
  for (size_t offset = 0; offset < count;) {
    const size_t n = min(kMonoChunkSamples, count - offset);
    for (size_t i = 0; i < n; ++i) stereo[i * 2] = stereo[i * 2 + 1] = mono[offset + i];
    size_t written = 0;
    const size_t expected = n * 2 * sizeof(int16_t);
    const esp_err_t error = i2s_channel_write(txChannel, stereo, expected, &written,
                                               pdMS_TO_TICKS(timeoutMs));
    recordI2sWrite(error, expected, written);
    if (error != ESP_OK || written != expected) return false;
    offset += n;
  }
  return true;
}
}  // namespace

bool V1Es8311::begin() {
  if (!audioMutex) audioMutex = xSemaphoreCreateMutex();
  if (!audioMutex || !setExpanderP7(false)) return false;

  i2s_chan_config_t channelConfig = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  channelConfig.dma_desc_num = kI2sDmaDescriptors;
  channelConfig.dma_frame_num = kI2sDmaFrames;
  channelConfig.auto_clear = true;
  if (i2s_new_channel(&channelConfig, &txChannel, nullptr) != ESP_OK) return false;
  i2s_std_config_t standardConfig = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kSampleRate),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
      .gpio_cfg = {
          .mclk = kMclkPin,
          .bclk = kBclkPin,
          .ws = kLrckPin,
          .dout = kDoutPin,
          .din = I2S_GPIO_UNUSED,
          .invert_flags = {.mclk_inv = false, .bclk_inv = false, .ws_inv = false},
      },
  };
  standardConfig.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_16BIT;
  standardConfig.slot_cfg.ws_width = 16;
  standardConfig.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_128;
  if (i2s_channel_init_std_mode(txChannel, &standardConfig) != ESP_OK ||
      i2s_channel_enable(txChannel) != ESP_OK) {
    i2s_del_channel(txChannel);
    txChannel = nullptr;
    setExpanderP7(false);
    return false;
  }

  ready_ = configureCodec();
  if (!ready_) {
    i2s_channel_disable(txChannel);
    i2s_del_channel(txChannel);
    txChannel = nullptr;
    setExpanderP7(false);
  }
  return ready_;
}

void V1Es8311::disable() {
  ready_ = false;
  if (txChannel) {
    i2s_channel_disable(txChannel);
    i2s_del_channel(txChannel);
    txChannel = nullptr;
  }
  setExpanderP7(false);
}

bool V1Es8311::writeMonoSamples(const int16_t* samples, size_t count, uint32_t timeoutMs) {
  if (!ready_ || !samples || !count || !audioMutex) return false;
  if (xSemaphoreTake(audioMutex, pdMS_TO_TICKS(timeoutMs)) != pdTRUE) {
    setDiagnosticError(DiagnosticError::OwnerTimeout);
    return false;
  }
  const bool result = writeSamples(samples, count, timeoutMs);
  xSemaphoreGive(audioMutex);
  return result;
}

bool V1Es8311::playMonoPcm(const int16_t* samples, size_t count) {
  if (!ready_ || !samples || count == 0 || !claimPcm()) return false;
  const size_t firstCount = min(kMonoChunkSamples, count);
  if (!writeMonoSamples(samples, firstCount, 500)) {
    disableOutput(false);
    releasePcm();
    return false;
  }
  if (!enableOutput()) {
    disableOutput(false);
    releasePcm();
    return false;
  }
  const bool ok = count == firstCount ||
      writeMonoSamples(samples + firstCount, count - firstCount, 500);
  disableOutput(true);
  releasePcm();
  return ok;
}

bool V1Es8311::emitTone(uint16_t hz, uint16_t ms, bool& outputActive) {
  const uint32_t count = kSampleRate * ms / 1000;
  int16_t samples[kMonoChunkSamples];
  const uint32_t halfPeriod = max<uint32_t>(1, kSampleRate / (hz * 2));
  for (uint32_t done = 0; done < count;) {
    const size_t n = min<uint32_t>(kMonoChunkSamples, count - done);
    for (size_t i = 0; i < n; ++i) {
      const uint32_t s = done + i;
      int32_t amp = (((s / halfPeriod) & 1) ? 1800 : -1800);
      if (s < 24) amp = amp * static_cast<int32_t>(s) / 24;
      if (count - s < 24) amp = amp * static_cast<int32_t>(count - s) / 24;
      samples[i] = static_cast<int16_t>(amp);
    }
    if (!outputActive) {
      if (!writeMonoSamples(samples, n, 20) || !enableOutput()) return false;
      outputActive = true;
    } else if (!writeMonoSamples(samples, n, 20)) {
      return false;
    }
    done += n;
  }
  return true;
}

bool V1Es8311::chirp(const char* kind) {
  beginChirpDiagnostic();
  if (!ready_) {
    setDiagnosticError(DiagnosticError::NotReady);
    return false;
  }
  if (!kind || !claimChirp()) {
    skipChirpDiagnostic(DiagnosticError::Busy);
    return false;
  }
  bool outputActive = false;
  bool played = false;
  if (!strcmp(kind, "boot")) played = emitTone(740, 35, outputActive) && emitTone(988, 45, outputActive);
  else if (!strcmp(kind, "tap")) played = emitTone(1200, 20, outputActive);
  else if (!strcmp(kind, "alert")) played = emitTone(988, 35, outputActive) && emitTone(740, 60, outputActive);
  else if (!strcmp(kind, "ack")) played = emitTone(880, 25, outputActive) && emitTone(1320, 35, outputActive);
  else if (!strcmp(kind, "test")) played = emitTone(880, 75, outputActive);
  else played = emitTone(880, 25, outputActive);
  played = disableOutput(outputActive) && played;
  releaseChirp();
  finishChirpDiagnostic(played);
  return played;
}

Diagnostics V1Es8311::diagnostics() const {
  portENTER_CRITICAL(&audioDiagnosticsMux);
  const Diagnostics result = audioDiagnostics;
  portEXIT_CRITICAL(&audioDiagnosticsMux);
  return result;
}

bool V1Es8311::audioTest(int gainRegister) {
  beginGainTestDiagnostic(gainRegister);
  if (gainRegister != kDacVolumeRegister && gainRegister != kStepGainRegister &&
      gainRegister != kVendorMaxGainRegister) {
    setDiagnosticError(DiagnosticError::GainRejected);
    finishGainTestDiagnostic(0, true);
    return false;
  }
  if (!ready_) {
    skipChirpDiagnostic(DiagnosticError::NotReady);
    finishGainTestDiagnostic(0, false);
    return false;
  }
  if (!claimPcm()) {
    setDiagnosticError(DiagnosticError::Busy);
    finishGainTestDiagnostic(0, true);
    return false;
  }

  uint8_t testReadback = 0;
  bool outputActive = false;
  const bool startSafe = disableOutput(false);
  const bool testGainSet = startSafe && writeAndVerifyDacGain(static_cast<uint8_t>(gainRegister), &testReadback);
  const uint16_t durationMs = gainRegister == kVendorMaxGainRegister ? 200 : 75;
  const bool tonePlayed = testGainSet && emitTone(880, durationMs, outputActive);
  const bool stopped = disableOutput(outputActive);
  const bool baselineWritten = writeAndVerifyDacGain(kDacVolumeRegister);
  const bool idleSafe = disableOutput(false);
  const bool restored = stopped && baselineWritten && idleSafe &&
      diagnostics().dacVolumeRegister == kDacVolumeRegister;
  finishGainTestDiagnostic(testReadback, restored);
  releasePcm();
  return tonePlayed && restored;
}

bool V1Es8311::claimSpeech() {
  portENTER_CRITICAL(&stateMux_);
  const bool busy = speechBusy_ || chirpBusy_ || pcmBusy_;
  if (!busy) speechBusy_ = true;
  portEXIT_CRITICAL(&stateMux_);
  return !busy;
}

void V1Es8311::releaseSpeech() {
  portENTER_CRITICAL(&stateMux_);
  speechBusy_ = false;
  portEXIT_CRITICAL(&stateMux_);
}

bool V1Es8311::claimChirp() {
  portENTER_CRITICAL(&stateMux_);
  const bool busy = speechBusy_ || chirpBusy_ || pcmBusy_;
  if (!busy) chirpBusy_ = true;
  portEXIT_CRITICAL(&stateMux_);
  return !busy;
}

void V1Es8311::releaseChirp() {
  portENTER_CRITICAL(&stateMux_);
  chirpBusy_ = false;
  portEXIT_CRITICAL(&stateMux_);
}

bool V1Es8311::claimPcm() {
  portENTER_CRITICAL(&stateMux_);
  const bool busy = speechBusy_ || chirpBusy_ || pcmBusy_;
  if (!busy) pcmBusy_ = true;
  portEXIT_CRITICAL(&stateMux_);
  return !busy;
}

void V1Es8311::releasePcm() {
  portENTER_CRITICAL(&stateMux_);
  pcmBusy_ = false;
  portEXIT_CRITICAL(&stateMux_);
}

bool V1Es8311::playUrl(const char* url) {
  if (!ready_ || !url || !*url || WiFi.status() != WL_CONNECTED) return false;
  if (!claimSpeech()) return false;
  String* pending = new (std::nothrow) String(url);
  if (!pending) { releaseSpeech(); return false; }
  if (xTaskCreatePinnedToCore(urlTask, "audio-url", 4096, pending, 1, nullptr, 0) != pdPASS) {
    delete pending;
    releaseSpeech();
    return false;
  }
  return true;
}

void V1Es8311::urlTask(void* context) {
  String* pending = static_cast<String*>(context);
  // The instance is the one static board-owned playback object.
  const bool ok = v1Playback.playUrlWorker(*pending);
  if (!ok) Serial.println("{\"say\":\"playback_failed\"}");
  delete pending;
  v1Playback.releaseSpeech();
  vTaskDelete(nullptr);
}

bool V1Es8311::playUrlWorker(const String& url) {
  HTTPClient http;
  http.setConnectTimeout(3000);
  http.setTimeout(3000);
  bool ok = false;
  const bool begun = http.begin(url);
  if (begun && http.GET() == 200) {
    if (xSemaphoreTake(audioMutex, pdMS_TO_TICKS(500)) != pdTRUE) {
      setDiagnosticError(DiagnosticError::OwnerTimeout);
      http.end();
      return false;
    }
    WiFiClient* stream = http.getStreamPtr();
    const int total = http.getSize();
    int received = 0;
    uint32_t lastData = millis();
    bool timedOut = false;
    bool hasCarry = false;
    bool outputActive = false;
    uint8_t carry = 0;
    int16_t samples[kMonoChunkSamples];
    uint8_t raw[kMonoChunkSamples * sizeof(int16_t) + 1];
    while (http.connected() && (total < 0 || received < total)) {
      const size_t available = stream->available();
      if (available >= sizeof(int16_t)) {
        const size_t limit = kMonoChunkSamples * sizeof(int16_t) - (hasCarry ? 1 : 0);
        const size_t want = min(available, limit);
        size_t offset = 0;
        if (hasCarry) { raw[0] = carry; offset = 1; }
        const int bytes = stream->readBytes(raw + offset, want);
        if (bytes <= 0) break;
        received += bytes;
        const size_t combined = offset + static_cast<size_t>(bytes);
        const size_t evenBytes = combined & ~size_t(1);
        if (evenBytes) {
          memcpy(samples, raw, evenBytes);
          if (!outputActive) {
            // Queue one complete PCM chunk while the codec is muted and P7 is low.
            if (!writeSamples(samples, evenBytes / sizeof(int16_t), 500)) break;
            if (!enableOutput()) break;
            outputActive = true;
          } else if (!writeSamples(samples, evenBytes / sizeof(int16_t), 500)) {
            break;
          }
        }
        hasCarry = (combined != evenBytes);
        if (hasCarry) carry = raw[combined - 1];
        lastData = millis();
      } else {
        if (millis() - lastData >= 3000) { timedOut = true; break; }
        vTaskDelay(pdMS_TO_TICKS(5));
      }
    }
    ok = !timedOut && !hasCarry && (total >= 0 ? received == total : !http.connected());
    disableOutput(outputActive);
    xSemaphoreGive(audioMutex);
  }
  if (begun) http.end();
  return ok;
}

}  // namespace audio
