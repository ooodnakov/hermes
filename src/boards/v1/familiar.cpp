#include "board_config.h"

#include "../../audio/v1_es8311.h"
#include "../../display/axs15231b_display.h"
#include "../../input/axs15231b_touch.h"
#include "../../peripherals/sensors.h"
#include "../../protocol/serial_line_framer.h"
#include "../../protocol/ui_state.h"
#include "../../storage/v1_assets.h"
#include "../../ui/familiar_ui.h"

#include <ArduinoJson.h>
#include <LovyanGFX.hpp>
#include <SD_MMC.h>
#include <Wire.h>
#include <esp_heap_caps.h>
#include <esp_memory_utils.h>
#include <esp_system.h>
#include <cstring>

namespace {
constexpr uint32_t kTouchIntervalMs = 20;
constexpr uint32_t kRenderIntervalMs = 220;
constexpr uint32_t kTelemetryIntervalMs = 60000;
constexpr uint32_t kPowerLongPressMs = 2000;
constexpr uint8_t kHoldBit = board::kExpanderHoldBit;
constexpr size_t kArtworkPixels = storage::kCharacterPixels;

display::Axs15231bDisplay lcd;
input::Axs15231bTouch touch(Wire);
LGFX_Sprite canvas;
protocol::UiState state;
ui::FamiliarUi familiarUi(canvas, state, [](void*, const String& line) {
  Serial.println(line);
});
storage::V1Assets assets;
peripherals::Sensors sensors;
audio::V1Es8311& playback = audio::v1Playback;
storage::FrameInfo lastFrameInfo;
uint16_t* artworkScratch = nullptr;
uint16_t artworkFrameCount = 1;
uint16_t artworkFrameMs = 180;
String artworkMood;
uint32_t artworkMoodStartedAt = 0;
uint32_t lastTouchAt = 0;
uint32_t lastRenderAt = 0;
uint32_t lastTelemetryAt = 0;
uint32_t hostLastInputAt = 0;
uint32_t powerPressedAt = 0;
bool powerWasPressed = false;
bool powerLongReported = false;
bool displayReady = false;
bool canvasReady = false;
bool systemHoldReady = false;
bool touchActive = false;
protocol::SerialLineFramer<> serialLineFramer;
int16_t touchStartX = 0, touchStartY = 0, touchLastX = 0, touchLastY = 0;
uint32_t touchStartedAt = 0;
uint32_t lastStatusUpdateAt = 0;
uint32_t touchGestureCount = 0;

bool readExpander(uint8_t reg, uint8_t& value) {
  Wire1.beginTransmission(board::kExpanderAddress);
  Wire1.write(reg);
  if (Wire1.endTransmission(false) != 0 ||
      Wire1.requestFrom(board::kExpanderAddress, static_cast<uint8_t>(1)) != 1) return false;
  value = static_cast<uint8_t>(Wire1.read());
  return true;
}

bool writeExpander(uint8_t reg, uint8_t value) {
  Wire1.beginTransmission(board::kExpanderAddress);
  Wire1.write(reg);
  Wire1.write(value);
  return Wire1.endTransmission(true) == 0;
}

bool setSystemHold(bool enabled) {
  uint8_t output = 0, config = 0;
  if (!readExpander(0x01, output) || !readExpander(0x03, config)) return false;
  output = enabled ? static_cast<uint8_t>(output | kHoldBit)
                   : static_cast<uint8_t>(output & ~kHoldBit);
  config = static_cast<uint8_t>(config & ~kHoldBit);
  if (!writeExpander(0x01, output) || !writeExpander(0x03, config)) return false;
  uint8_t verifyOutput = 0, verifyConfig = 0;
  return readExpander(0x01, verifyOutput) && readExpander(0x03, verifyConfig) &&
         ((verifyOutput & kHoldBit) != 0) == enabled && !(verifyConfig & kHoldBit);
}

void sendJson(JsonDocument& document) {
  serializeJson(document, Serial);
  Serial.write('\n');
}

void sendHello(const char* transport) {
  JsonDocument hello;
  hello["hello"] = "hermes-buddy";
  hello["transport"] = transport;
  hello["board"] = board::kId;
  hello["firmware"] = "familiar-v1";
  sendJson(hello);
}

void sendDiagnostic() {
  const auto& sensor = sensors.snapshot();
  const auto& touchDiag = touch.diagnostics();
  JsonDocument diagnostic;
  diagnostic["cmd"] = "diag";
  diagnostic["board"] = board::kId;
  diagnostic["firmware"] = "familiar-v1";
  diagnostic["framework"] = ESP_ARDUINO_VERSION_STR;
  diagnostic["display_ready"] = displayReady;
  diagnostic["canvas_ready"] = canvasReady;
  diagnostic["canvas_psram"] = canvas.getBuffer() && esp_ptr_external_ram(canvas.getBuffer());
  diagnostic["display_frame_psram_bytes"] = lcd.psramFrameBytes();
  diagnostic["psram_bytes"] = ESP.getPsramSize();
  diagnostic["psram_free_bytes"] = ESP.getFreePsram();
  diagnostic["heap_free_bytes"] = ESP.getFreeHeap();
  diagnostic["sd_ready"] = assets.sdReady();
  diagnostic["sd_error"] = storage::errorName(assets.lastError());
  diagnostic["face_asset_source"] = lastFrameInfo.source == storage::AssetSource::Sd ? "sd" :
      lastFrameInfo.source == storage::AssetSource::CompiledFallback ? "compiled-fallback" : "not-rendered";
  diagnostic["face_asset_frame_count"] = lastFrameInfo.frameCount;
  diagnostic["face_asset_error"] = storage::errorName(lastFrameInfo.sdError);
  diagnostic["touch_bus"] = "GPIO17/18";
  diagnostic["power_hold"] = systemHoldReady;
  JsonObject touchObject = diagnostic["touch"].to<JsonObject>();
  touchObject["samples"] = touchDiag.samples;
  touchObject["gestures"] = touchGestureCount;
  touchObject["pressed_samples"] = touchDiag.pressedSamples;
  touchObject["released_samples"] = touchDiag.releasedSamples;
  touchObject["bus_errors"] = touchDiag.busErrors;
  touchObject["short_reads"] = touchDiag.shortReads;
  touchObject["high_count_empty_samples"] = touchDiag.highCountSamples;
  touchObject["out_of_range"] = touchDiag.outOfRange;
  touchObject["wire_error"] = touchDiag.lastWireError;
  touchObject["count"] = touchDiag.lastCount;
  touchObject["raw_long"] = touchDiag.rawLongAxis;
  touchObject["raw_short"] = touchDiag.rawShortAxis;
  touchObject["ui_x"] = touchDiag.x;
  touchObject["ui_y"] = touchDiag.y;
  touchObject["status"] = touchDiag.lastStatus == input::TouchStatus::Pressed ? "pressed" :
      touchDiag.lastStatus == input::TouchStatus::Released ? "released" : "error";
  JsonArray lastPacket = touchObject["last_packet"].to<JsonArray>();
  JsonArray lastTouchPacket = touchObject["last_touch_packet"].to<JsonArray>();
  for (size_t i = 0; i < sizeof(touchDiag.lastResponse); ++i) lastPacket.add(touchDiag.lastResponse[i]);
  for (size_t i = 0; i < sizeof(touchDiag.lastTouchResponse); ++i) lastTouchPacket.add(touchDiag.lastTouchResponse[i]);
  diagnostic["battery_v"] = sensor.batterySampleReady ? sensor.batteryVolts : 0.0f;
  diagnostic["rtc_ready"] = sensor.rtcReady;
  diagnostic["rtc_time_valid"] = sensor.rtcTimeValid;
  diagnostic["imu_ready"] = sensor.imuReady;
  diagnostic["audio"] = playback.ready() ? "ready" : "unavailable";
  const audio::Diagnostics audioDiag = playback.diagnostics();
  JsonObject audioStats = diagnostic["audio_stats"].to<JsonObject>();
  audioStats["chirp_requests"] = audioDiag.chirpRequests;
  audioStats["chirp_successes"] = audioDiag.chirpSuccesses;
  audioStats["chirp_failures"] = audioDiag.chirpFailures;
  audioStats["chirp_skips"] = audioDiag.chirpSkips;
  audioStats["last_error"] = audio::diagnosticErrorName(audioDiag.lastError);
  audioStats["i2s_write_failures"] = audioDiag.i2sWriteFailures;
  audioStats["i2s_error"] = audioDiag.lastI2sError;
  audioStats["i2s_expected_bytes"] = audioDiag.lastI2sExpectedBytes;
  audioStats["i2s_written_bytes"] = audioDiag.lastI2sWrittenBytes;
  audioStats["dac_volume_register"] = audioDiag.dacVolumeRegister;
  audioStats["test_requests"] = audioDiag.testRequests;
  audioStats["last_test_gain_requested"] = audioDiag.lastTestGainRequested;
  audioStats["last_test_gain_readback"] = audioDiag.lastTestGainReadback;
  audioStats["baseline_restored"] = audioDiag.baselineRestored;
  diagnostic["wifi"] = "unsupported";
  diagnostic["ble"] = "unsupported";
  diagnostic["network"] = "unsupported";
  sendJson(diagnostic);
}

void sendPingReply() {
  JsonDocument reply;
  reply["ack"] = "ping";
  reply["ok"] = true;
  sendJson(reply);
  sendHello("usb");
  sendDiagnostic();
}

void setStatusFields(uint32_t now) {
  const auto& sensor = sensors.snapshot();
  state.sdStatus = assets.sdReady() ? "SD ready" : String("SD unavailable:") + storage::errorName(assets.lastError());
  state.touchStatus = "AXS15231B";
  state.batteryStatus = sensor.batterySampleReady
      ? String("BAT ") + String(sensor.batteryVolts, 2) + "V"
      : "BAT --";
  if (sensor.rtcTimeValid) {
    char clock[16];
    snprintf(clock, sizeof(clock), "RTC %02u:%02u:%02u", sensor.rtcHour, sensor.rtcMinute, sensor.rtcSecond);
    state.rtcStatus = clock;
  } else state.rtcStatus = "RTC invalid";
  state.wifiStatus = "WiFi unsupported";
  state.networkStatus = "network unsupported";
  state.audioStatus = playback.ready() ? "ES8311 16 kHz" : "audio unavailable";
  state.motionStatus = sensor.accelerationReady
      ? String("ACC ") + String(sensor.accelerationXG, 1) + "," +
          String(sensor.accelerationYG, 1) + "," + String(sensor.accelerationZG, 1) + "g"
      : "IMU unavailable";
  state.freeHeap = ESP.getFreeHeap();
  state.wifiConnected = false;
  state.bleConnected = false;
  state.linkStatus = hostLastInputAt && now - hostLastInputAt < 30000 ? "USB HOST" : "USB READY";
}

const char* moodName(const String& mood) {
  if (mood == "sleep") return "sleep";
  if (mood == "waiting") return "waiting";
  if (mood == "thinking") return "thinking";
  return "idle";
}

void paintFace(lgfx::LGFX_Sprite& sprite, const String& mood,
               const ui::Rect& rect, uint32_t nowMs, void*) {
  if (!artworkScratch) {
    sprite.drawRoundRect(rect.x + 30, rect.y + 42, 84, 60, 10, TFT_GREEN);
    sprite.setTextColor(TFT_GREEN, TFT_BLACK);
    sprite.setCursor(rect.x + 65, rect.y + 65);
    sprite.print("H");
    return;
  }
  const char* selectedMood = moodName(mood);
  if (artworkMood != selectedMood) {
    artworkMood = selectedMood;
    artworkMoodStartedAt = nowMs;
    artworkFrameCount = 1;
    artworkFrameMs = 180;
  }
  const uint16_t frame = static_cast<uint16_t>(((nowMs - artworkMoodStartedAt) / artworkFrameMs) % artworkFrameCount);
  if (assets.loadMoodFrame(selectedMood, frame, artworkScratch, kArtworkPixels, &lastFrameInfo)) {
    artworkFrameCount = lastFrameInfo.frameCount ? lastFrameInfo.frameCount : 1;
    artworkFrameMs = lastFrameInfo.frameMs ? lastFrameInfo.frameMs : 180;
    sprite.pushImage(rect.x, rect.y, storage::kCharacterWidth, storage::kCharacterHeight, artworkScratch);
  } else {
    sprite.fillRect(rect.x, rect.y, rect.w, rect.h, TFT_BLACK);
    sprite.drawRoundRect(rect.x + 24, rect.y + 38, 96, 68, 12, TFT_GREEN);
    sprite.setTextColor(TFT_GREEN, TFT_BLACK);
    sprite.setCursor(rect.x + 65, rect.y + 65);
    sprite.print("H");
  }
}

bool handleConfig(JsonDocument& request) {
  JsonDocument updates;
  for (const char* section : {"wifi", "host", "display"}) {
    if (request[section].is<JsonObjectConst>()) updates[section].set(request[section]);
  }
  const bool saved = updates.size() == 0 || assets.mergeConfiguration(updates.as<JsonObjectConst>());
  JsonDocument ack;
  ack["ack"] = "config";
  ack["ok"] = saved;
  ack["detail"] = saved ? (updates.size() ? "saved" : "no-op") : "sd-unavailable-or-write-failed";
  sendJson(ack);
  return saved;
}

void processLine(const char* line) {
  JsonDocument request;
  if (deserializeJson(request, line)) {
    JsonDocument error;
    error["type"] = "error";
    error["error"] = "invalid_json";
    sendJson(error);
    return;
  }
  const char* command = request["cmd"] | "";
  const char* type = request["type"] | "";
  const uint32_t now = millis();
  if (!strcmp(command, "ping")) {
    hostLastInputAt = now;
    sendPingReply();
    return;
  }
  if (!strcmp(command, "touchdiag")) {
    sendDiagnostic();
    return;
  }
  if (!strcmp(command, "audiotest")) {
    const auto gainField = request["gain"];
    const int requestedGain = gainField.is<int>() ? gainField.as<int>() :
        gainField.isNull() ? 0x60 : -1;
    const bool pathOk = playback.audioTest(requestedGain);
    const audio::Diagnostics audioDiag = playback.diagnostics();
    JsonDocument result;
    result["cmd"] = "audiotest";
    result["path_ok"] = pathOk;
    result["result"] = audio::diagnosticErrorName(audioDiag.lastError);
    result["gain_requested"] = audioDiag.lastTestGainRequested;
    result["gain_readback"] = audioDiag.lastTestGainReadback;
    result["baseline_restored"] = audioDiag.baselineRestored;
    result["idle_gain_register"] = audioDiag.dacVolumeRegister;
    result["duration_ms"] = requestedGain == 0xBA ? 200 : 75;
    result["i2s_error"] = audioDiag.lastI2sError;
    result["i2s_expected_bytes"] = audioDiag.lastI2sExpectedBytes;
    result["i2s_written_bytes"] = audioDiag.lastI2sWrittenBytes;
    sendJson(result);
    return;
  }
  if (!strcmp(command, "clear")) {
    state = protocol::UiState();
    return;
  }
  if (!strcmp(type, "config")) {
    handleConfig(request);
    state.connected = true;
    state.lastSeenMs = now;
    hostLastInputAt = now;
    return;
  }
  if (!protocol::applyJsonFrame(state, String(line), now)) return;
  hostLastInputAt = now;
  if (!strcmp(type, "say")) {
    const char* url = request["say"] | "";
    if (!playback.playUrl(url)) state.message = playback.ready() ? "Speech playback unavailable" : "Audio unavailable";
    else state.message = "Playing speech";
    state.dirty = true;
  }
  setStatusFields(now);
}

void pollSerial() {
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    const protocol::SerialLineFramer<>::Result result = serialLineFramer.push(c);
    if (result == protocol::SerialLineFramer<>::Result::LineReady) {
      processLine(serialLineFramer.line());
    } else if (result == protocol::SerialLineFramer<>::Result::LineTooLong) {
      JsonDocument error;
      error["type"] = "error";
      error["error"] = "line_too_long";
      sendJson(error);
    } else if (result == protocol::SerialLineFramer<>::Result::InvalidLine) {
      JsonDocument error;
      error["type"] = "error";
      error["error"] = "invalid_line";
      sendJson(error);
    }
  }
}

void pollTouch(uint32_t now) {
  if (now - lastTouchAt < kTouchIntervalMs) return;
  lastTouchAt = now;
  input::TouchPoint point{};
  const input::TouchStatus sample = touch.readSample(point);
  if (sample == input::TouchStatus::Error) {
    // An I2C/protocol failure cancels an in-progress gesture. Only a later,
    // explicit Released sample can complete a valid press/release pair.
    touchActive = false;
    return;
  }
  if (sample == input::TouchStatus::Pressed) {
    if (!touchActive) {
      touchActive = true;
      touchStartX = point.x;
      touchStartY = point.y;
      touchLastX = point.x;
      touchLastY = point.y;
      touchStartedAt = now;
    } else {
      touchLastX = point.x;
      touchLastY = point.y;
    }
    return;
  }
  if (touchActive) {
    touchActive = false;
    ++touchGestureCount;
    familiarUi.touchGesture(touchStartX, touchStartY, touchLastX, touchLastY,
                            now - touchStartedAt, now);
    playback.chirp("tap");
  }
}

void pollPowerKey(uint32_t now) {
  const bool pressed = digitalRead(board::kPowerKey) == LOW;
  if (pressed && !powerWasPressed) {
    powerPressedAt = now;
    powerLongReported = false;
  }
  if (!pressed) {
    powerPressedAt = 0;
    powerLongReported = false;
  } else if (!powerLongReported && powerPressedAt && now - powerPressedAt >= kPowerLongPressMs) {
    powerLongReported = true;
    const bool released = setSystemHold(false);
    JsonDocument event;
    event["type"] = "shutdown";
    event["reason"] = "power_key_long_press";
    event["hold_released"] = released;
    sendJson(event);
  }
  powerWasPressed = pressed;
}

void pollBootButton() {
  static bool wasPressed = false;
  static uint32_t changedAt = 0;
  const bool pressed = digitalRead(board::kBootKey) == LOW;
  const uint32_t now = millis();
  if (pressed != wasPressed && now - changedAt >= 35) {
    changedAt = now;
    wasPressed = pressed;
    if (pressed) {
      if (state.approval.active && state.page == protocol::Page::Operations) {
        const ui::Rect allow = ui::FamiliarUi::allowRect();
        const int16_t x = allow.x + allow.w / 2, y = allow.y + allow.h / 2;
        familiarUi.touchGesture(x, y, x, y, 1, now);
      } else if (state.page == protocol::Page::Operations) {
        Serial.println("{\"cmd\":\"action\",\"action\":\"start\"}");
      }
    }
  }
}

void sendTelemetry(uint32_t now) {
  if (now - lastTelemetryAt < kTelemetryIntervalMs) return;
  lastTelemetryAt = now;
  const auto& sensor = sensors.snapshot();
  JsonDocument telemetry;
  telemetry["cmd"] = "telemetry";
  telemetry["usb"] = true;
  telemetry["quiet"] = false;
  telemetry["bat"] = sensor.batterySampleReady ? sensor.batteryVolts : 0.0f;
  telemetry["rtc_valid"] = sensor.rtcTimeValid;
  telemetry["imu_ready"] = sensor.imuReady && sensor.accelerationReady;
  telemetry["audio_ready"] = playback.ready();
  telemetry["wifi_ready"] = false;
  sendJson(telemetry);
}

bool initializeHoldBus() {
  // Match the proven board profile: touch on Wire (GPIO17/18), peripheral
  // devices and the TCA9554 on Wire1 (GPIO47/48).
  Wire.begin(board::kTouchSda, board::kTouchScl, 100000);
  Wire.setClock(300000);
  Wire.setTimeOut(10);
  Wire1.begin(board::kPeripheralSda, board::kPeripheralScl, 100000);
  Wire1.setClock(300000);
  Wire1.setTimeOut(10);
  // Keep the already-powered board latched before display/SD/IMU startup.
  return setSystemHold(true);
}

}  // namespace

void setup() {
  Serial.setRxBufferSize(8192);
  Serial.begin(115200);
  delay(20);
  pinMode(board::kPowerKey, INPUT_PULLUP);
  pinMode(board::kBootKey, INPUT_PULLUP);
  systemHoldReady = initializeHoldBus();

  sensors.begin(Wire1, millis());
  playback.begin();  // Codec starts muted; P7 stays low until a playback request.
  displayReady = lcd.begin();
  if (displayReady) {
    canvas.setColorDepth(16);
    canvas.setPsram(true);
    canvasReady = canvas.createSprite(ui::FamiliarUi::kWidth, ui::FamiliarUi::kHeight);
  }
  artworkScratch = static_cast<uint16_t*>(heap_caps_malloc(kArtworkPixels * sizeof(uint16_t),
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  assets.begin();
  familiarUi.setFacePainter(paintFace);
  setStatusFields(millis());
  sendHello("usb");

  if (!systemHoldReady || !displayReady || !canvasReady || !artworkScratch) {
    JsonDocument error;
    error["type"] = "error";
    error["board"] = board::kId;
    error["subsystem"] = !systemHoldReady ? "power_expander" : !displayReady ? "display" : "canvas_or_artwork";
    error["error"] = "initialization_failed";
    sendJson(error);
  }
  sendDiagnostic();
}

void loop() {
  pollSerial();
  const uint32_t now = millis();
  pollPowerKey(now);
  pollBootButton();
  sensors.update(now);
  if (now - lastStatusUpdateAt >= 1000) {
    setStatusFields(now);
    lastStatusUpdateAt = now;
  }
  pollTouch(now);
  sendTelemetry(now);

  familiarUi.tick(now);
  if (displayReady && canvasReady && (state.dirty || now - lastRenderAt >= kRenderIntervalMs)) {
    familiarUi.render(now);
    if (!lcd.present(static_cast<const uint16_t*>(canvas.getBuffer()),
                     static_cast<size_t>(ui::FamiliarUi::kWidth * ui::FamiliarUi::kHeight))) {
      displayReady = false;
      JsonDocument error;
      error["type"] = "error";
      error["subsystem"] = "display";
      error["error"] = "frame_transfer_failed";
      sendJson(error);
    }
    lastRenderAt = now;
  }
  delay(3);
}
