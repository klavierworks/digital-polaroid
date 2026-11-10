#pragma once

#include <stdint.h>

#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"

#include "../config.h"
#include "strips.h"

class Scanout {
 public:
  bool begin(uint16_t width, uint16_t height, const PanelTiming& timing,
             StripRing& ring);
  uint32_t frames() const;

 private:
  static bool onVsync(esp_lcd_panel_handle_t panel,
                      const esp_lcd_rgb_panel_event_data_t* data,
                      void* context);
  static bool onBounceEmpty(esp_lcd_panel_handle_t panel, void* bounce, int posPx,
                            int lenBytes, void* context);

  esp_lcd_panel_handle_t panel_ = nullptr;
  StripRing* ring_ = nullptr;
  uint16_t width_ = 0;
  uint16_t height_ = 0;
  volatile uint32_t frames_ = 0;
};
