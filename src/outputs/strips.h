#pragma once

#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "../config.h"

class StripRing {
 public:
  bool begin(size_t stripBytes, uint16_t stripPixels);
  uint16_t* claim();
  uint16_t claimedStrip() const;
  void publish();
  bool drainFromIsr(void* destination, size_t bytes, int positionPx);
  uint32_t takeUnderruns();

 private:
  bool hasSpace() const;
  bool signalFromIsr();

  uint8_t* slots_[kStripSlots] = {nullptr};
  uint16_t slotStrip_[kStripSlots] = {0};
  size_t stripBytes_ = 0;
  uint16_t stripPixels_ = 0;
  uint16_t claimed_ = 0;
  volatile uint16_t produce_ = 0;
  volatile bool resync_ = false;
  volatile uint32_t produced_ = 0;
  volatile uint32_t consumed_ = 0;
  volatile uint32_t underruns_ = 0;
  SemaphoreHandle_t drained_ = nullptr;
};
