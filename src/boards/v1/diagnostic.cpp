#include "board_config.h"
#include "../../display/axs15231b_display.h"
#include "../../input/axs15231b_touch.h"

#include <ArduinoJson.h>
#include <LovyanGFX.hpp>
#include <Wire.h>
#include <esp_heap_caps.h>
#include <esp_memory_utils.h>
#include <esp_system.h>

namespace {
constexpr uint32_t kDebounceMs = 35;
constexpr uint32_t kPowerLongPressMs = 2000;
constexpr size_t kLineCapacity = 256;

struct DebouncedInput {
  int pin;
  bool raw;
  bool stable;
  uint32_t changedAt;
};

DebouncedInput powerKey{board::kPowerKey, true, true, 0};
DebouncedInput bootKey{board::kBootKey, true, true, 0};
char inputLine[kLineCapacity];
size_t inputLength = 0;
bool droppingInputLine = false;
bool powerLongPressReported = false;
uint32_t powerPressedAt = 0;
bool expanderReady = false;
bool touchPresent = false;
display::Axs15231bDisplay lcd;
input::Axs15231bTouch touch(Wire1);
LGFX_Sprite diagnosticCanvas;
bool displayReady = false;
bool displayErrorReported = false;
bool psramInitializationOk = false;
uint32_t nextFrameAt = 0;
uint32_t nextTouchAt = 0;
uint32_t nextTouchProbeAt = 0;
uint16_t animationX = 0;

bool drawDisplayProof() {
  diagnosticCanvas.fillScreen(TFT_BLACK);
  diagnosticCanvas.drawRect(0, 0, 640, 172, TFT_WHITE);
  diagnosticCanvas.setTextColor(TFT_WHITE, TFT_BLACK);
  diagnosticCanvas.setTextSize(1);
  diagnosticCanvas.drawString("TOP LEFT", 8, 8);
  diagnosticCanvas.drawString("TOP RIGHT", 550, 8);
  diagnosticCanvas.drawString("BOTTOM LEFT", 8, 150);
  diagnosticCanvas.drawString("BOTTOM RIGHT", 535, 150);
  diagnosticCanvas.fillRect(190, 38, 52, 36, TFT_RED);
  diagnosticCanvas.fillRect(242, 38, 52, 36, TFT_GREEN);
  diagnosticCanvas.fillRect(294, 38, 52, 36, TFT_BLUE);
  diagnosticCanvas.fillRect(346, 38, 52, 36, TFT_WHITE);
  diagnosticCanvas.fillRect(animationX, 100, 24, 18, TFT_YELLOW);
  animationX = static_cast<uint16_t>((animationX + 9) % 608);
  return lcd.present(static_cast<const uint16_t*>(diagnosticCanvas.getBuffer()), 640U * 172U);
}

void initializeDisplayProof() {
  if (!lcd.begin()) return;
  diagnosticCanvas.setColorDepth(16);
  diagnosticCanvas.setPsram(true);
  if (!diagnosticCanvas.createSprite(640, 172)) return;
  displayReady = true;
  if (!drawDisplayProof()) displayReady = false;
}

void emit(JsonDocument& document) {
  serializeJson(document, Serial);
  Serial.write('\n');
}

void addPsramMetrics(JsonDocument& doc) {
  doc["psram_init_ok"] = psramInitializationOk;
  doc["psram_found"] = psramFound();
  doc["psram_bytes"] = ESP.getPsramSize();
  doc["psram_free_bytes"] = ESP.getFreePsram();
  doc["psram_heap_total_bytes"] = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
  doc["psram_heap_free_bytes"] = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  const void* canvasBuffer = diagnosticCanvas.getBuffer();
  doc["display_canvas_psram"] = canvasBuffer != nullptr && esp_ptr_external_ram(canvasBuffer);
  doc["display_frame_psram_bytes"] = lcd.psramFrameBytes();
}

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
    default: return "unknown";
  }
}

bool readExpanderRegister(uint8_t reg, uint8_t& value) {
  Wire.beginTransmission(board::kExpanderAddress);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(board::kExpanderAddress, static_cast<uint8_t>(1)) != 1) return false;
  value = Wire.read();
  return true;
}

bool writeExpanderRegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(board::kExpanderAddress);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool setSystemHold(bool enabled) {
  if (!expanderReady) return false;
  uint8_t output = 0;
  uint8_t configuration = 0;
  if (!readExpanderRegister(0x01, output) || !readExpanderRegister(0x03, configuration)) return false;

  const uint8_t holdBit = board::kExpanderHoldBit;
  output = enabled ? static_cast<uint8_t>(output | holdBit)
                   : static_cast<uint8_t>(output & ~holdBit);
  configuration = static_cast<uint8_t>(configuration & ~holdBit);  // 0 means output
  return writeExpanderRegister(0x01, output) && writeExpanderRegister(0x03, configuration);
}

bool initializePowerHold() {
  Wire.beginTransmission(board::kExpanderAddress);
  if (Wire.endTransmission() != 0) return false;

  expanderReady = true;
  if (!setSystemHold(true)) {
    expanderReady = false;
    return false;
  }
  return true;
}

bool probeAddress(TwoWire& bus, uint8_t address) {
  bus.beginTransmission(address);
  return bus.endTransmission() == 0;
}

bool probeTouchController() {
  return probeAddress(Wire1, board::kTouchAddress);
}

void emitHello() {
  JsonDocument doc;
  doc["type"] = "hello";
  doc["hello"] = "hermes-buddy";
  doc["board"] = board::kId;
  doc["firmware"] = "diagnostic-v1";
  doc["framework"] = ESP_ARDUINO_VERSION_STR;
  doc["chip"] = ESP.getChipModel();
  doc["chip_revision"] = ESP.getChipRevision();
  doc["flash_bytes"] = ESP.getFlashChipSize();
  addPsramMetrics(doc);
  doc["reset_reason"] = resetReasonName(esp_reset_reason());
  doc["peripheral_i2c_sda"] = board::kPeripheralSda;
  doc["peripheral_i2c_scl"] = board::kPeripheralScl;
  doc["touch_i2c_sda"] = board::kTouchSda;
  doc["touch_i2c_scl"] = board::kTouchScl;
  doc["expander_present"] = expanderReady;
  doc["system_hold"] = expanderReady;
  doc["touch_present"] = touchPresent;
  doc["display_ready"] = displayReady;
  emit(doc);
}

void emitDiagnostic() {
  JsonDocument doc;
  doc["type"] = "diagnostic";
  doc["board"] = board::kId;
  doc["framework"] = ESP_ARDUINO_VERSION_STR;
  doc["chip"] = ESP.getChipModel();
  doc["chip_revision"] = ESP.getChipRevision();
  doc["flash_bytes"] = ESP.getFlashChipSize();
  addPsramMetrics(doc);
  doc["heap_free_bytes"] = ESP.getFreeHeap();
  doc["heap_min_free_bytes"] = ESP.getMinFreeHeap();
  doc["reset_reason"] = resetReasonName(esp_reset_reason());
  doc["expander_present"] = expanderReady;
  doc["touch_present"] = touchPresent;
  doc["display_ready"] = displayReady;
  doc["power_key"] = digitalRead(board::kPowerKey) == LOW ? "pressed" : "released";
  doc["boot_key"] = digitalRead(board::kBootKey) == LOW ? "pressed" : "released";
  emit(doc);
}

void emitPong() {
  JsonDocument doc;
  doc["ack"] = "ping";
  doc["ok"] = true;
  emit(doc);
}

void processLine(const char* line) {
  JsonDocument request;
  const DeserializationError error = deserializeJson(request, line);
  if (error) {
    JsonDocument response;
    response["type"] = "error";
    response["error"] = "invalid_json";
    emit(response);
    return;
  }

  const char* command = request["cmd"] | request["type"] | "";
  if (strcmp(command, "ping") == 0) {
    emitPong();
  } else if (strcmp(command, "hello") == 0) {
    emitHello();
  } else if (strcmp(command, "diagnostic") == 0) {
    emitDiagnostic();
  } else if (strcmp(command, "shutdown") == 0) {
    const bool off = setSystemHold(false);
    JsonDocument response;
    response["type"] = "shutdown";
    response["board"] = board::kId;
    response["hold_released"] = off;
    emit(response);
  } else {
    JsonDocument response;
    response["type"] = "error";
    response["error"] = "unknown_command";
    emit(response);
  }
}

void pollSerial() {
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\n') {
      if (!droppingInputLine) {
        inputLine[inputLength] = '\0';
        if (inputLength > 0 && inputLine[inputLength - 1] == '\r') inputLine[inputLength - 1] = '\0';
        processLine(inputLine);
      }
      inputLength = 0;
      droppingInputLine = false;
    } else if (!droppingInputLine) {
      if (inputLength + 1 < sizeof(inputLine)) {
        inputLine[inputLength++] = c;
      } else {
        droppingInputLine = true;
      }
    }
  }
}

void emitKeyEvent(const char* key, bool pressed) {
  JsonDocument doc;
  doc["type"] = "event";
  doc["event"] = "key";
  doc["key"] = key;
  doc["pressed"] = pressed;
  emit(doc);
}

void pollKey(DebouncedInput& input, const char* name) {
  const bool value = digitalRead(input.pin) != LOW;
  const uint32_t now = millis();
  if (value != input.raw) {
    input.raw = value;
    input.changedAt = now;
  } else if (value != input.stable && now - input.changedAt >= kDebounceMs) {
    input.stable = value;
    emitKeyEvent(name, !value);
    if (input.pin == board::kPowerKey) {
      if (!value) {
        powerPressedAt = now;
        powerLongPressReported = false;
      } else {
        powerPressedAt = 0;
        powerLongPressReported = false;
      }
    }
  }
}

void pollLongPress() {
  if (powerKey.stable || powerLongPressReported || powerPressedAt == 0) return;
  if (millis() - powerPressedAt < kPowerLongPressMs) return;

  powerLongPressReported = true;
  const bool released = setSystemHold(false);
  JsonDocument doc;
  doc["type"] = "shutdown";
  doc["reason"] = "power_key_long_press";
  doc["hold_released"] = released;
  emit(doc);
}
}  // namespace

void setup() {
  Serial.begin(115200);
  // Idempotent: Arduino reports true immediately if boot-time PSRAM init ran.
  psramInitializationOk = psramFound() || psramInit();
  pinMode(board::kPowerKey, INPUT_PULLUP);
  pinMode(board::kBootKey, INPUT_PULLUP);
  delay(10);

  Wire.begin(board::kPeripheralSda, board::kPeripheralScl, 100000);
  Wire.setTimeOut(50);
  const bool holdSet = initializePowerHold();

  Wire1.begin(board::kTouchSda, board::kTouchScl, 100000);
  Wire1.setTimeOut(50);
  touchPresent = probeTouchController();
  initializeDisplayProof();
  if (!touchPresent) nextTouchProbeAt = millis() + 1000;

  const bool powerState = digitalRead(board::kPowerKey) != LOW;
  const bool bootState = digitalRead(board::kBootKey) != LOW;
  powerKey.raw = powerKey.stable = powerState;
  bootKey.raw = bootKey.stable = bootState;
  powerKey.changedAt = bootKey.changedAt = millis();

  if (!holdSet) {
    JsonDocument error;
    error["type"] = "error";
    error["subsystem"] = "power_expander";
    error["error"] = "tca9554_p6_hold_failed";
    emit(error);
  }
  emitHello();
}

void loop() {
  pollSerial();
  pollKey(powerKey, "power");
  pollKey(bootKey, "boot");
  pollLongPress();
  const uint32_t now = millis();
  if (!touchPresent && now >= nextTouchProbeAt) {
    touchPresent = probeTouchController();
    nextTouchProbeAt = now + 1000;
  }
  if (displayReady && now >= nextFrameAt) {
    if (!drawDisplayProof()) {
      displayReady = false;
      if (!displayErrorReported) {
        JsonDocument error;
        error["type"] = "error";
        error["subsystem"] = "display";
        error["error"] = "frame_transfer_failed";
        emit(error);
        displayErrorReported = true;
      }
    }
    nextFrameAt = now + 150;
  }
  if (touchPresent && now >= nextTouchAt) {
    input::TouchPoint point{};
    if (touch.read(point)) {
      JsonDocument event;
      event["type"] = "event";
      event["event"] = "touch";
      event["x"] = point.x;
      event["y"] = point.y;
      emit(event);
    }
    nextTouchAt = now + 20;
  }
  delay(5);
}
