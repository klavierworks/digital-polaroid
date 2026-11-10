#pragma once

#include <stdint.h>

#include "constants.h"
#include "inputs/buttons.h"
#include "inputs/fetcher.h"
#include "outputs/panel.h"

class Slideshow {
 public:
  bool begin(uint32_t now);
  void tick(uint32_t now);
  void report(uint32_t now);

 private:
  enum class Phase { Waiting, Holding };

  void show(uint16_t* image, uint32_t now);
  void handle(Press press, uint32_t now);
  bool due(uint32_t now) const;
  const char* phaseName() const;

  Panel panel_;
  Fetcher fetcher_;
  Buttons buttons_;
  uint8_t brightness_ = kBrightness;
  Phase phase_ = Phase::Waiting;
  uint32_t dueAt_ = 0;
  uint32_t slides_ = 0;
  bool late_ = false;
};
