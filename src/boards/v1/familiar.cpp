#include "board_config.h"

#include "../../audio/v1_es8311.h"
#include "../../display/axs15231b_display.h"
#include "../../input/axs15231b_touch.h"
#include "../../network/v1_network.h"
#include "../../network/transport_policy.h"
#include "../../peripherals/sensors.h"
#include "../../peripherals/bus_lock.h"
#include "../../protocol/serial_line_framer.h"
#include "../../protocol/speech_dispatch.h"
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
network::V1Network networkService;
uint32_t usbLastInputAt = 0;
bool processingUsbLine = false;
ui::FamiliarUi familiarUi(canvas, state, [](void*, const String& line) {
  Serial.println(line);
  networkService.sendLine(line.c_str(), usbLastInputAt && millis() - usbLastInputAt < 30000);
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
uint32_t acceptedProtocolFrames = 0;
uint32_t acceptedUsbFrames = 0;
uint32_t acceptedTcpFrames = 0;
uint32_t acceptedStateFrames = 0;
uint32_t acceptedUsbStateFrames = 0;
uint32_t acceptedTcpStateFrames = 0;
uint32_t lastStateFrameAt = 0;
uint32_t lastUsbStateFrameAt = 0;
uint32_t lastTcpStateFrameAt = 0;
uint32_t faceRenderCount = 0;
uint32_t faceOfflineRenderCount = 0;
uint32_t faceLiveTransitionCount = 0;
uint32_t lastFaceRenderAt = 0;
bool hasRenderedFace = false;
bool lastRenderedFaceLive = false;
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
uint32_t lastGestureSequence = 0;
bool quietMode = false;

const char* resetReasonName(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON: return "power_on";
    case ESP_RST_EXT: return "external";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "interrupt_watchdog";
    case ESP_RST_TASK_WDT: return "task_watchdog";
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_DEEPSLEEP: return "deep_sleep";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_SDIO: return "sdio";
    case ESP_RST_USB: return "usb";
    case ESP_RST_JTAG: return "jtag";
    case ESP_RST_EFUSE: return "efuse";
    case ESP_RST_PWR_GLITCH: return "power_glitch";
    case ESP_RST_CPU_LOCKUP: return "cpu_lockup";
    default: return "unknown";
  }
}

esp_reset_reason_t startupResetReason() {
  // Capture once on first hello during setup; later diagnostics use the same
  // immutable reason for the boot that produced this running firmware.
  static const esp_reset_reason_t reason = esp_reset_reason();
  return reason;
}

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
  peripherals::Wire1Lock lock;
  if (!lock) return false;
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
  char line[4096];
  if (measureJson(document) > sizeof(line) - 1) return;
  const size_t length = serializeJson(document, line, sizeof(line));
  if (length == 0 || length >= sizeof(line)) return;
  Serial.write(reinterpret_cast<const uint8_t*>(line), length);
  Serial.write('\n');
  networkService.sendLine(line, usbLastInputAt && millis() - usbLastInputAt < 30000);
}

void sendHello(const char* transport) {
  JsonDocument hello;
  hello["hello"] = "hermes-buddy";
  hello["transport"] = transport;
  hello["board"] = board::kId;
  hello["firmware"] = "familiar-v1";
  const esp_reset_reason_t resetReason = startupResetReason();
  hello["reset_reason"] = resetReasonName(resetReason);
  hello["reset_reason_code"] = static_cast<int>(resetReason);
  sendJson(hello);
}

void sendDiagnostic() {
  const uint32_t uptimeMs = millis();
  const auto& sensor = sensors.snapshot();
  const auto& touchDiag = touch.diagnostics();
  JsonDocument diagnostic;
  diagnostic["cmd"] = "diag";
  diagnostic["board"] = board::kId;
  diagnostic["firmware"] = "familiar-v1";
  diagnostic["framework"] = ESP_ARDUINO_VERSION_STR;
  const esp_reset_reason_t resetReason = startupResetReason();
  diagnostic["reset_reason"] = resetReasonName(resetReason);
  diagnostic["reset_reason_code"] = static_cast<int>(resetReason);
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
  diagnostic["battery_gpio4_adc_mv"] = sensor.batterySampleReady ? sensor.batteryAdcMillivolts : 0;
  diagnostic["battery_divider_estimate_v"] = sensor.batterySampleReady ? sensor.batteryVolts : 0.0f;
  diagnostic["battery_v"] = sensor.batterySampleReady ? sensor.batteryVolts : 0.0f;
  diagnostic["rtc_ready"] = sensor.rtcReady;
  diagnostic["rtc_time_valid"] = sensor.rtcTimeValid;
  diagnostic["rtc_year"] = sensor.rtcYear;
  diagnostic["rtc_month"] = sensor.rtcMonth;
  diagnostic["rtc_day"] = sensor.rtcDay;
  diagnostic["rtc_weekday"] = sensor.rtcWeekday;
  diagnostic["rtc_hour"] = sensor.rtcHour;
  diagnostic["rtc_minute"] = sensor.rtcMinute;
  diagnostic["rtc_second"] = sensor.rtcSecond;
  diagnostic["rtc_sampled_at_ms"] = sensor.rtcSampledAtMs;
  diagnostic["rtc_sample_age_ms"] = uptimeMs - sensor.rtcSampledAtMs;
  diagnostic["uptime_ms"] = uptimeMs;
  diagnostic["rtc_errors"] = sensor.rtcErrors;
  diagnostic["imu_ready"] = sensor.imuReady;
  diagnostic["imu_config_ready"] = sensor.imuConfigReady;
  diagnostic["imu_acceleration_ready"] = sensor.accelerationReady;
  diagnostic["imu_acceleration_x_g"] = sensor.accelerationXG;
  diagnostic["imu_acceleration_y_g"] = sensor.accelerationYG;
  diagnostic["imu_acceleration_z_g"] = sensor.accelerationZG;
  diagnostic["imu_errors"] = sensor.imuErrors;
  diagnostic["imu_recoveries"] = sensor.imuRecoveries;
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
  audioStats["speech_requests"] = audioDiag.speechRequests;
  audioStats["speech_successes"] = audioDiag.speechSuccesses;
  audioStats["speech_failures"] = audioDiag.speechFailures;
  audioStats["speech_skips"] = audioDiag.speechSkips;
  audioStats["speech_bytes_received"] = audioDiag.speechBytesReceived;
  audioStats["last_speech_bytes"] = audioDiag.lastSpeechBytes;
  audioStats["http_status"] = audioDiag.lastHttpStatus;
  diagnostic["wifi"] = networkService.wifiStatus();
  diagnostic["wifi_configured"] = networkService.wifiConfigured();
  diagnostic["wifi_ready"] = networkService.wifiReady();
  if (networkService.wifiReady()) diagnostic["wifi_ip"] = networkService.localIp().toString();
  diagnostic["ble"] = "unsupported";
  diagnostic["network"] = networkService.tcpStatus();
  diagnostic["tcp_ready"] = networkService.tcpConnected();
  diagnostic["tcp_connects"] = networkService.tcpConnects();
  diagnostic["tcp_disconnects"] = networkService.tcpDisconnects();
  diagnostic["tcp_framing_errors"] = networkService.framingErrors();
  diagnostic["tcp_configured"] = networkService.tcpConfigured();
  if (networkService.tcpConfigured()) {
    diagnostic["tcp_host"] = networkService.tcpHost();
    diagnostic["tcp_port"] = networkService.tcpPort();
  }
  diagnostic["usb_preferred"] = true;
  const uint32_t stateAge = uptimeMs - lastStateFrameAt;
  diagnostic["host_state_frames"] = acceptedStateFrames;
  diagnostic["host_state_seen"] = acceptedStateFrames != 0;
  diagnostic["host_state_age_ms"] = acceptedStateFrames ? stateAge : 0;
  diagnostic["host_state_live"] = acceptedStateFrames && stateAge < 30000;
  diagnostic["host_state_usb_frames"] = acceptedUsbStateFrames;
  diagnostic["host_state_tcp_frames"] = acceptedTcpStateFrames;
  diagnostic["host_state_usb_age_ms"] = acceptedUsbStateFrames
      ? uptimeMs - lastUsbStateFrameAt : 0;
  diagnostic["host_state_tcp_age_ms"] = acceptedTcpStateFrames
      ? uptimeMs - lastTcpStateFrameAt : 0;
  diagnostic["host_protocol_frames"] = acceptedProtocolFrames;
  diagnostic["host_protocol_usb_frames"] = acceptedUsbFrames;
  diagnostic["host_protocol_tcp_frames"] = acceptedTcpFrames;
  const uint32_t hostLastSeenAge = uptimeMs - state.lastSeenMs;
  const bool hostLive = state.connected && hostLastSeenAge < 30000;
  diagnostic["host_connected"] = state.connected;
  diagnostic["host_last_seen_age_ms"] = state.connected ? hostLastSeenAge : 0;
  diagnostic["host_live"] = hostLive;
  diagnostic["rendered_face_live"] = hasRenderedFace ? lastRenderedFaceLive : false;
  diagnostic["face_live_transitions"] = faceLiveTransitionCount;
  diagnostic["face_offline_frames"] = faceOfflineRenderCount;
  diagnostic["face_frames"] = faceRenderCount;
  diagnostic["face_last_render_age_ms"] = lastFaceRenderAt
      ? uptimeMs - lastFaceRenderAt : 0;
  sendJson(diagnostic);
}

void sendPingReply() {
  JsonDocument reply;
  reply["ack"] = "ping";
  reply["ok"] = true;
  sendJson(reply);
  const bool usbAlive = usbLastInputAt && millis() - usbLastInputAt < 30000;
  sendHello(networkService.tcpConnected() && !usbAlive ? "tcp" : "usb");
  sendDiagnostic();
}

void noteUsbActivity(network::HostActivity activity) {
  if (processingUsbLine && network::claimsUsbPriority(activity)) usbLastInputAt = millis();
}

void setStatusFields(uint32_t now) {
  const auto& sensor = sensors.snapshot();
  state.sdStatus = assets.sdReady() ? "SD ready" : String("SD unavailable:") + storage::errorName(assets.lastError());
  state.touchStatus = "AXS15231B";
  state.batteryStatus = sensor.batterySampleReady
      ? String("ADC~") + String(sensor.batteryVolts, 2) + "V"
      : "ADC --";
  if (sensor.rtcTimeValid) {
    char clock[16];
    snprintf(clock, sizeof(clock), "RTC %02u:%02u:%02u", sensor.rtcHour, sensor.rtcMinute, sensor.rtcSecond);
    state.rtcStatus = clock;
  } else state.rtcStatus = "RTC invalid";
  state.wifiStatus = networkService.wifiReady()
      ? String("WiFi ") + networkService.localIp().toString()
      : String("WiFi ") + networkService.wifiStatus();
  state.networkStatus = String("TCP ") + networkService.tcpStatus();
  state.audioStatus = playback.ready() ? "ES8311 16 kHz" : "audio unavailable";
  state.motionStatus = sensor.accelerationReady
      ? String("ACC ") + String(sensor.accelerationXG, 1) + "," +
          String(sensor.accelerationYG, 1) + "," + String(sensor.accelerationZG, 1) + "g"
      : "IMU unavailable";
  state.freeHeap = ESP.getFreeHeap();
  state.wifiConnected = networkService.wifiReady();
  state.bleConnected = false;
  state.linkStatus = usbLastInputAt && now - usbLastInputAt < 30000 ? "USB HOST" :
      networkService.tcpConnected() ? "TCP HOST" : "USB READY";
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
  const JsonObjectConst wifi = request["wifi"].as<JsonObjectConst>();
  const JsonObjectConst host = request["host"].as<JsonObjectConst>();
  if ((!request["wifi"].isNull() && !request["wifi"].is<JsonObjectConst>()) ||
      (!request["host"].isNull() && !request["host"].is<JsonObjectConst>()) ||
      (!request["display"].isNull() && !request["display"].is<JsonObjectConst>())) {
    JsonDocument ack;
    ack["ack"] = "config";
    ack["ok"] = false;
    ack["detail"] = "invalid-config-section";
    sendJson(ack);
    return false;
  }
  if (request["wifi"].is<JsonObjectConst>()) {
    if ((!wifi["ssid"].isNull() && !wifi["ssid"].is<const char*>()) ||
        (!wifi["password"].isNull() && !wifi["password"].is<const char*>()) ||
        (wifi["ssid"].is<const char*>() && strlen(wifi["ssid"].as<const char*>()) > 32) ||
        (wifi["password"].is<const char*>() && strlen(wifi["password"].as<const char*>()) > 63)) {
      JsonDocument ack;
      ack["ack"] = "config";
      ack["ok"] = false;
      ack["detail"] = "invalid-wifi-config";
      sendJson(ack);
      return false;
    }
  }
  if (request["host"].is<JsonObjectConst>()) {
    const JsonVariantConst port = host["port"];
    if ((!host["ip"].isNull() && (!host["ip"].is<const char*>() ||
         !network::isIpv4Literal(host["ip"].as<const char*>()))) ||
        (!host["token"].isNull() && !host["token"].is<const char*>()) ||
        (host["ip"].is<const char*>() && strlen(host["ip"].as<const char*>()) > 253) ||
        (host["token"].is<const char*>() && strlen(host["token"].as<const char*>()) > 128) ||
        (host["token"].is<const char*>() && host["token"].as<const char*>()[0] == '\0') ||
        (!port.isNull() && (!port.is<uint16_t>() || port.as<uint16_t>() == 0))) {
      JsonDocument ack;
      ack["ack"] = "config";
      ack["ok"] = false;
      ack["detail"] = "invalid-host-config";
      sendJson(ack);
      return false;
    }
  }
  JsonDocument updates;
  for (const char* section : {"wifi", "host", "display"}) {
    if (request[section].is<JsonObjectConst>()) updates[section].set(request[section]);
  }
  const bool saved = updates.size() == 0 || assets.mergeConfiguration(updates.as<JsonObjectConst>());
  if (saved && updates.size()) {
    networkService.configure(updates["wifi"].as<JsonObjectConst>(), updates["host"].as<JsonObjectConst>());
    networkService.begin();
  }
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
    noteUsbActivity(network::HostActivity::Ping);
    sendPingReply();
    return;
  }
  if (!strcmp(command, "touchdiag")) {
    noteUsbActivity(network::HostActivity::LocalCommand);
    sendDiagnostic();
    return;
  }
  if (!strcmp(command, "rtcset")) {
    noteUsbActivity(network::HostActivity::LocalCommand);
    const char* const fields[] = {"year", "month", "day", "hour", "minute", "second"};
    int values[6] = {};
    bool fieldsValid = true;
    for (size_t i = 0; i < 6; ++i) {
      const JsonVariantConst value = request[fields[i]];
      if (!value.is<int>()) {
        fieldsValid = false;
        break;
      }
      values[i] = value.as<int>();
    }
    bool set = false;
    if (fieldsValid) {
      peripherals::Wire1Lock lock(pdMS_TO_TICKS(100));
      if (lock) set = sensors.setRtcDateTime(
          values[0], values[1], values[2], values[3], values[4], values[5]);
    }
    JsonDocument result;
    result["cmd"] = "rtcset";
    result["ok"] = set;
    result["detail"] = !fieldsValid ? "invalid_fields" : set ? "set" : "invalid_time_or_rtc_failure";
    sendJson(result);
    return;
  }
  if (!strcmp(command, "audiotest")) {
    noteUsbActivity(network::HostActivity::LocalCommand);
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
    noteUsbActivity(network::HostActivity::Clear);
    state = protocol::UiState();
    hostLastInputAt = now;
    if (processingUsbLine) usbLastInputAt = millis();
    return;
  }
  if (!strcmp(type, "config")) {
    noteUsbActivity(network::HostActivity::Config);
    handleConfig(request);
    hostLastInputAt = now;
    if (processingUsbLine) usbLastInputAt = millis();
    return;
  }
  if (!protocol::applyJsonFrame(state, String(line), now)) return;
  noteUsbActivity(network::HostActivity::ProtocolFrame);
  hostLastInputAt = now;
  const uint32_t receivedAt = millis();
  ++acceptedProtocolFrames;
  if (processingUsbLine) {
    ++acceptedUsbFrames;
  } else {
    ++acceptedTcpFrames;
  }
  const bool stateFrame = request["total"].is<int>() || request["running"].is<int>() ||
      request["waiting"].is<int>() || request["job_state"].is<const char*>() ||
      request["entries"].is<JsonArrayConst>();
  if (stateFrame) {
    ++acceptedStateFrames;
    lastStateFrameAt = receivedAt;
    if (processingUsbLine) {
      ++acceptedUsbStateFrames;
      lastUsbStateFrameAt = receivedAt;
    } else {
      ++acceptedTcpStateFrames;
      lastTcpStateFrameAt = receivedAt;
    }
  }
  const char* speechUrl = protocol::speechUrlForFrame(
      type, request["url"] | "", request["say"] | "");
  if (speechUrl) {
    const char* url = speechUrl;
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
      processingUsbLine = true;
      processLine(serialLineFramer.line());
      processingUsbLine = false;
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

void processTcpLine(const char* line, void*) {
  processingUsbLine = false;
  processLine(line);
}

void loadNetworkConfiguration() {
  if (!assets.sdReady()) return;
  constexpr char kConfigPath[] = "/hermes-buddy-349-v1/config.json";
  if (!SD_MMC.exists(kConfigPath)) return;
  File file = SD_MMC.open(kConfigPath, FILE_READ);
  if (!file || file.size() > 4096) {
    if (file) file.close();
    return;
  }
  JsonDocument config;
  const DeserializationError error = deserializeJson(config, file);
  file.close();
  if (error || !config.is<JsonObject>()) return;
  networkService.configure(config["wifi"].as<JsonObjectConst>(), config["host"].as<JsonObjectConst>());
  networkService.begin();
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
    if (!quietMode) playback.chirp("tap");
  }
}

void pollPhysicalGesture() {
  const peripherals::SensorSnapshot& sensor = sensors.snapshot();
  if (sensor.gestureSequence == lastGestureSequence) return;
  lastGestureSequence = sensor.gestureSequence;
  const char* name = nullptr;
  bool hasQuiet = false;
  if (sensor.gesture == peripherals::Gesture::FaceDown) {
    quietMode = true;
    playback.setQuiet(true);
    name = "facedown";
    hasQuiet = true;
  } else if (sensor.gesture == peripherals::Gesture::Upright) {
    quietMode = false;
    playback.setQuiet(false);
    name = "upright";
    hasQuiet = true;
  } else if (sensor.gesture == peripherals::Gesture::Shake) {
    name = "shake";
  } else if (sensor.gesture == peripherals::Gesture::Tap2) {
    name = "tap2";
  } else if (sensor.gesture == peripherals::Gesture::Pickup) {
    name = "pickup";
  }
  if (!name) return;
  JsonDocument event;
  event["cmd"] = "gesture";
  event["gesture"] = name;
  if (hasQuiet) event["quiet"] = quietMode;
  sendJson(event);
  if (!quietMode && (sensor.gesture == peripherals::Gesture::Upright ||
                     sensor.gesture == peripherals::Gesture::Shake)) {
    playback.chirp("ack");
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
        JsonDocument action;
        action["cmd"] = "action";
        action["action"] = "start";
        sendJson(action);
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
  telemetry["usb"] = usbLastInputAt && now - usbLastInputAt < 30000;
  telemetry["quiet"] = quietMode;
  telemetry["bat"] = sensor.batterySampleReady ? sensor.batteryVolts : 0.0f;
  telemetry["rtc_valid"] = sensor.rtcTimeValid;
  telemetry["imu_ready"] = sensor.imuReady && sensor.accelerationReady;
  telemetry["audio_ready"] = playback.ready();
  telemetry["wifi_ready"] = networkService.wifiReady();
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

  {
    peripherals::Wire1Lock lock;
    if (lock) sensors.begin(Wire1, millis());
  }
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
  loadNetworkConfiguration();
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
  // Select transport from the last *host* activity, then refresh the UI time
  // after both TCP and USB frames have updated state.lastSeenMs. Passing a
  // pre-receive timestamp to FamiliarUi makes unsigned liveness subtraction
  // wrap and paints a one-frame IDLE flash on every TCP heartbeat.
  const uint32_t transportNow = millis();
  const bool usbAlive = usbLastInputAt && transportNow - usbLastInputAt < 30000;
  networkService.poll(usbAlive, processTcpLine, nullptr);
  const uint32_t now = millis();
  pollPowerKey(now);
  pollBootButton();
  {
    peripherals::Wire1Lock lock;
    if (lock) sensors.update(now);
  }
  pollPhysicalGesture();
  if (now - lastStatusUpdateAt >= 1000) {
    setStatusFields(now);
    lastStatusUpdateAt = now;
  }
  pollTouch(now);
  sendTelemetry(now);

  familiarUi.tick(now);
  if (displayReady && canvasReady && (state.dirty || now - lastRenderAt >= kRenderIntervalMs)) {
    const uint32_t renderAt = millis();
    familiarUi.render(renderAt);
    if (state.page == protocol::Page::Face && !state.modalActive) {
      const bool live = state.connected && renderAt - state.lastSeenMs < 30000;
      if (hasRenderedFace && live != lastRenderedFaceLive) ++faceLiveTransitionCount;
      hasRenderedFace = true;
      lastRenderedFaceLive = live;
      ++faceRenderCount;
      if (!live) ++faceOfflineRenderCount;
      lastFaceRenderAt = renderAt;
    }
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
