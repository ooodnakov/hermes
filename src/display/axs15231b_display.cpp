#include "axs15231b_display.h"

#include <Arduino.h>
#include <cstring>

#include "../boards/v1/board_config.h"
#include "axs15231b/esp_lcd_axs15231b.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace display {
namespace {
constexpr size_t kNativePixels = 172U * 640U;
constexpr size_t kChunkRows = 64;
constexpr size_t kChunkBytes = 172U * kChunkRows * sizeof(uint16_t);
constexpr uint32_t kLcdClockHz = 40U * 1000U * 1000U;
constexpr TickType_t kTransferTimeout = pdMS_TO_TICKS(1000);
// The V1 panel's built-in settings are used by the working board firmware.
// The driver's generic table programs a different panel's gate/gamma settings.
const axs15231b_lcd_init_cmd_t kV1InitCommands[] = {
    {0x11, nullptr, 0, 100},
    {0x29, nullptr, 0, 100},
};

bool onColorTransferDone(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void* context) {
  auto* done = static_cast<SemaphoreHandle_t>(context);
  BaseType_t taskWoken = pdFALSE;
  xSemaphoreGiveFromISR(done, &taskWoken);
  return taskWoken == pdTRUE;
}

bool waitForTransfer(SemaphoreHandle_t done) {
  return xSemaphoreTake(done, kTransferTimeout) == pdTRUE;
}
}  // namespace

Axs15231bDisplay::~Axs15231bDisplay() {
  cleanup();
}

size_t Axs15231bDisplay::psramFrameBytes() const {
  return nativeFrame_ ? kNativePixels * sizeof(uint16_t) : 0;
}

bool Axs15231bDisplay::setBrightness(uint8_t percent) {
  if (percent != 25 && percent != 50 && percent != 75 && percent != 100) return false;
  if (backlightReady_) {
    const uint8_t duty = static_cast<uint8_t>(((100 - percent) * 255 + 50) / 100);
    if (!ledcWrite(board::kBacklight, duty)) return false;
  }
  brightnessPercent_ = percent;
  return true;
}

void Axs15231bDisplay::cleanup() {
  // Never release DMA memory while the SPI driver may still be reading it.
  // A missing callback is treated as a fatal one-shot display failure.
  if (transferInFlight_) return;
  if (panel_) {
    esp_lcd_panel_del(static_cast<esp_lcd_panel_handle_t>(panel_));
    panel_ = nullptr;
  }
  if (io_) {
    esp_lcd_panel_io_del(static_cast<esp_lcd_panel_io_handle_t>(io_));
    io_ = nullptr;
  }
  if (busInitialized_) {
    spi_bus_free(SPI3_HOST);
    busInitialized_ = false;
  }
  if (transferDone_) vSemaphoreDelete(static_cast<SemaphoreHandle_t>(transferDone_));
  transferDone_ = nullptr;
  if (nativeFrame_) heap_caps_free(nativeFrame_);
  nativeFrame_ = nullptr;
  if (dmaChunk_) heap_caps_free(dmaChunk_);
  dmaChunk_ = nullptr;
}

bool Axs15231bDisplay::begin() {
  if (ready()) return true;
  if (failed_) return false;
  const auto fail = [this]() {
    failed_ = true;
    cleanup();
    return false;
  };
  transferDone_ = xSemaphoreCreateBinary();
  if (!transferDone_) return fail();
  nativeFrame_ = static_cast<uint16_t*>(heap_caps_malloc(kNativePixels * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  dmaChunk_ = static_cast<uint8_t*>(heap_caps_aligned_alloc(4, kChunkBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (!nativeFrame_ || !dmaChunk_) return fail();

  spi_bus_config_t busConfig = {};
  busConfig.data0_io_num = board::kDisplayData0;
  busConfig.data1_io_num = board::kDisplayData1;
  busConfig.sclk_io_num = board::kDisplayClock;
  busConfig.data2_io_num = board::kDisplayData2;
  busConfig.data3_io_num = board::kDisplayData3;
  busConfig.max_transfer_sz = kChunkBytes;
  if (spi_bus_initialize(SPI3_HOST, &busConfig, SPI_DMA_CH_AUTO) != ESP_OK) return fail();
  busInitialized_ = true;

  esp_lcd_panel_io_spi_config_t ioConfig = {};
  ioConfig.cs_gpio_num = board::kDisplayCs;
  ioConfig.dc_gpio_num = -1;
  ioConfig.spi_mode = 3;
  ioConfig.pclk_hz = kLcdClockHz;
  ioConfig.trans_queue_depth = 1;
  ioConfig.on_color_trans_done = onColorTransferDone;
  ioConfig.user_ctx = transferDone_;
  ioConfig.lcd_cmd_bits = 32;
  ioConfig.lcd_param_bits = 8;
  ioConfig.flags.quad_mode = true;
  esp_lcd_panel_io_handle_t panelIo = nullptr;
  if (esp_lcd_new_panel_io_spi(SPI3_HOST, &ioConfig, &panelIo) != ESP_OK) return fail();
  io_ = panelIo;

  axs15231b_vendor_config_t vendorConfig = {};
  vendorConfig.flags.use_qspi_interface = 1;
  vendorConfig.init_cmds = kV1InitCommands;
  vendorConfig.init_cmds_size = sizeof(kV1InitCommands) / sizeof(kV1InitCommands[0]);
  esp_lcd_panel_dev_config_t panelConfig = {};
  panelConfig.reset_gpio_num = board::kDisplayReset;
  panelConfig.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
  panelConfig.bits_per_pixel = 16;
  panelConfig.vendor_config = &vendorConfig;
  esp_lcd_panel_handle_t panel = nullptr;
  if (esp_lcd_new_panel_axs15231b(panelIo, &panelConfig, &panel) != ESP_OK) return fail();
  panel_ = panel;
  // Match the reset pulse used by the working Waveshare V1 firmware.
  if (gpio_set_level(static_cast<gpio_num_t>(board::kDisplayReset), 1) != ESP_OK) return fail();
  delay(30);
  if (gpio_set_level(static_cast<gpio_num_t>(board::kDisplayReset), 0) != ESP_OK) return fail();
  delay(250);
  if (gpio_set_level(static_cast<gpio_num_t>(board::kDisplayReset), 1) != ESP_OK) return fail();
  delay(30);
  if (esp_lcd_panel_init(panel) != ESP_OK) return fail();

  // Native glass orientation is 172x640; the caller paints at 640x172.
  // V1 drives the backlight with active-low PWM: duty 0 is fully on.
  if (!ledcAttach(board::kBacklight, 50000, 8)) return fail();
  backlightReady_ = true;
  if (!setBrightness(brightnessPercent_)) return fail();
  return true;
}

bool Axs15231bDisplay::present(const uint16_t* logicalFrame, size_t pixelCount) {
  if (!ready() || !logicalFrame || pixelCount != static_cast<size_t>(kWidth * kHeight)) return false;

  // Match Waveshare's full-frame 90-degree software transform:
  // native[y][x] = logical[171-x][y].
  for (int nativeY = 0; nativeY < 640; ++nativeY) {
    uint16_t* targetRow = nativeFrame_ + static_cast<size_t>(nativeY) * 172;
    for (int nativeX = 0; nativeX < 172; ++nativeX) {
      targetRow[nativeX] = logicalFrame[static_cast<size_t>(171 - nativeX) * kWidth + nativeY];
    }
  }

  const auto panel = static_cast<esp_lcd_panel_handle_t>(panel_);
  const auto done = static_cast<SemaphoreHandle_t>(transferDone_);
  for (int y = 0; y < 640; y += kChunkRows) {
    const int rows = (y + kChunkRows <= 640) ? kChunkRows : 640 - y;
    const size_t bytes = static_cast<size_t>(rows) * 172 * sizeof(uint16_t);
    std::memcpy(dmaChunk_, nativeFrame_ + static_cast<size_t>(y) * 172, bytes);
    // One outstanding transfer at a time keeps the DMA buffer alive and
    // unchanged until the driver's color-transfer callback fires.
    if (esp_lcd_panel_draw_bitmap(panel, 0, y, 172, y + rows, dmaChunk_) != ESP_OK) {
      failed_ = true;
      return false;
    }
    transferInFlight_ = true;
    if (!waitForTransfer(done)) {
      failed_ = true;
      return false;
    }
    transferInFlight_ = false;
  }
  return true;
}

}  // namespace display
