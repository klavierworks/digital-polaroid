#pragma once

#include <Arduino_GFX_Library.h>
#include <stdint.h>

enum class Press { None, Up, Down, Both };

class Buttons {
 public:
  void begin(Arduino_XCA9554SWSPI* expander);
  Press poll(uint32_t now);

 private:
  Press settle(bool up, bool down, uint32_t now);

  Arduino_XCA9554SWSPI* expander_ = nullptr;
  uint32_t polledAt_ = 0;
  bool upHeld_ = false;
  bool downHeld_ = false;
  bool chorded_ = false;
  Press pending_ = Press::None;
  uint32_t pendingAt_ = 0;
};
