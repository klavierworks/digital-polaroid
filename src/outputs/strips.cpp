#include "strips.h"

#include <Arduino.h>
#include <string.h>

bool StripRing::begin(size_t stripBytes, uint16_t stripPixels) {
  stripBytes_ = stripBytes;
  stripPixels_ = stripPixels;
  drained_ = xSemaphoreCreateBinary();
  if (drained_ == nullptr) {
    return false;
  }
  for (uint32_t i = 0; i < kStripSlots; ++i) {
    slots_[i] = static_cast<uint8_t*>(
        heap_caps_malloc(stripBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (slots_[i] == nullptr) {
      return false;
    }
  }
  return true;
}

bool StripRing::hasSpace() const {
  return produced_ - consumed_ < kStripSlots;
}

uint16_t* StripRing::claim() {
  while (!hasSpace()) {
    xSemaphoreTake(drained_, pdMS_TO_TICKS(20));
  }
  claimed_ = produce_;
  return reinterpret_cast<uint16_t*>(slots_[produced_ % kStripSlots]);
}

uint16_t StripRing::claimedStrip() const {
  return claimed_;
}

void StripRing::publish() {
  if (resync_) {
    resync_ = false;
    return;
  }
  slotStrip_[produced_ % kStripSlots] = claimed_;
  produced_ = produced_ + 1;
  produce_ = (claimed_ + 1) % kStripsPerFrame;
}

bool StripRing::signalFromIsr() {
  BaseType_t woken = pdFALSE;
  xSemaphoreGiveFromISR(drained_, &woken);
  return woken == pdTRUE;
}

bool StripRing::drainFromIsr(void* destination, size_t bytes, int positionPx) {
  const uint16_t wanted =
      static_cast<uint16_t>(static_cast<uint32_t>(positionPx) / stripPixels_);
  if (produced_ == consumed_ || slotStrip_[consumed_ % kStripSlots] != wanted) {
    underruns_ = underruns_ + 1;
    consumed_ = produced_;
    produce_ = (wanted + 1) % kStripsPerFrame;
    resync_ = true;
    return signalFromIsr();
  }
  memcpy(destination, slots_[consumed_ % kStripSlots],
         bytes < stripBytes_ ? bytes : stripBytes_);
  consumed_ = consumed_ + 1;
  return signalFromIsr();
}

uint32_t StripRing::takeUnderruns() {
  const uint32_t count = underruns_;
  underruns_ = 0;
  return count;
}
