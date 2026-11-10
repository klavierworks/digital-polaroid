#include "buttons.h"

#include "../constants.h"

void Buttons::begin(Arduino_XCA9554SWSPI* expander) {
  expander_ = expander;
  if (expander_ == nullptr) {
    return;
  }
  expander_->pinMode(PCA_BUTTON_UP, INPUT);
  expander_->pinMode(PCA_BUTTON_DOWN, INPUT);
}

Press Buttons::poll(uint32_t now) {
  if (expander_ == nullptr || now - polledAt_ < kButtonPollMs) {
    return Press::None;
  }
  polledAt_ = now;
  const bool up = expander_->digitalRead(PCA_BUTTON_UP) == LOW;
  const bool down = expander_->digitalRead(PCA_BUTTON_DOWN) == LOW;
  const Press press = settle(up, down, now);
  upHeld_ = up;
  downHeld_ = down;
  return press;
}

Press Buttons::settle(bool up, bool down, uint32_t now) {
  if (up && down) {
    pending_ = Press::None;
    if (chorded_) {
      return Press::None;
    }
    chorded_ = true;
    return Press::Both;
  }
  if (chorded_) {
    chorded_ = up || down;
    return Press::None;
  }
  if (up && !upHeld_) {
    pending_ = Press::Up;
    pendingAt_ = now;
  } else if (down && !downHeld_) {
    pending_ = Press::Down;
    pendingAt_ = now;
  }
  if (pending_ != Press::None && now - pendingAt_ >= kChordMs) {
    const Press press = pending_;
    pending_ = Press::None;
    return press;
  }
  return Press::None;
}
