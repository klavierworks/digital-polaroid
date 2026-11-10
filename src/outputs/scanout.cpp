#include "scanout.h"

#include <Arduino.h>

bool Scanout::begin(uint16_t width, uint16_t height, const PanelTiming& timing,
                    StripRing& ring) {
  width_ = width;
  height_ = height;
  ring_ = &ring;

  esp_lcd_rgb_panel_config_t config = {};
  config.clk_src = LCD_CLK_SRC_DEFAULT;
  config.timings.pclk_hz = timing.pixelClockHz;
  config.timings.h_res = width;
  config.timings.v_res = height;
  config.timings.hsync_pulse_width = timing.hsyncPulseWidth;
  config.timings.hsync_back_porch = timing.hsyncBackPorch;
  config.timings.hsync_front_porch = timing.hsyncFrontPorch;
  config.timings.vsync_pulse_width = timing.vsyncPulseWidth;
  config.timings.vsync_back_porch = timing.vsyncBackPorch;
  config.timings.vsync_front_porch = timing.vsyncFrontPorch;
  config.timings.flags.hsync_idle_low = timing.hsyncPolarity == 0;
  config.timings.flags.vsync_idle_low = timing.vsyncPolarity == 0;
  config.timings.flags.de_idle_high = 0;
  config.timings.flags.pclk_active_neg = timing.pclkActiveNeg;
  config.timings.flags.pclk_idle_high = timing.pclkIdleHigh;
  config.data_width = 16;
  config.bits_per_pixel = 16;
  config.num_fbs = 0;
  config.bounce_buffer_size_px = static_cast<size_t>(width) * kBounceLines;
  config.dma_burst_size = 64;
  config.hsync_gpio_num = TFT_HSYNC;
  config.vsync_gpio_num = TFT_VSYNC;
  config.de_gpio_num = TFT_DE;
  config.pclk_gpio_num = TFT_PCLK;
  config.disp_gpio_num = -1;
  const int dataPins[16] = {TFT_B1, TFT_B2, TFT_B3, TFT_B4, TFT_B5,
                            TFT_G0, TFT_G1, TFT_G2, TFT_G3, TFT_G4, TFT_G5,
                            TFT_R1, TFT_R2, TFT_R3, TFT_R4, TFT_R5};
  for (int i = 0; i < 16; ++i) {
    config.data_gpio_nums[i] = dataPins[i];
  }
  config.flags.disp_active_low = 1;
  config.flags.no_fb = 1;

  esp_err_t status = esp_lcd_new_rgb_panel(&config, &panel_);
  if (status != ESP_OK) {
    Serial.printf("qualiabridge: rgb panel create failed: %d\n", status);
    return false;
  }
  status = esp_lcd_panel_reset(panel_);
  if (status == ESP_OK) {
    status = esp_lcd_panel_init(panel_);
  }
  if (status != ESP_OK) {
    Serial.printf("qualiabridge: rgb panel init failed: %d\n", status);
    return false;
  }

  esp_lcd_rgb_panel_event_callbacks_t callbacks = {};
  callbacks.on_vsync = onVsync;
  callbacks.on_bounce_empty = onBounceEmpty;
  status = esp_lcd_rgb_panel_register_event_callbacks(panel_, &callbacks, this);
  if (status != ESP_OK) {
    Serial.printf("qualiabridge: frame callback rejected: %d\n", status);
  }
  return true;
}

uint32_t Scanout::frames() const {
  return frames_;
}

bool Scanout::onBounceEmpty(esp_lcd_panel_handle_t, void* bounce, int posPx,
                            int lenBytes, void* context) {
  Scanout* scanout = static_cast<Scanout*>(context);
  return scanout->ring_->drainFromIsr(bounce, static_cast<size_t>(lenBytes), posPx);
}

bool IRAM_ATTR Scanout::onVsync(esp_lcd_panel_handle_t,
                                const esp_lcd_rgb_panel_event_data_t*,
                                void* context) {
  Scanout* scanout = static_cast<Scanout*>(context);
  scanout->frames_ = scanout->frames_ + 1;
  return false;
}
