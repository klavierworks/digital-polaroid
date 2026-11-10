#pragma once

#include <stddef.h>
#include <stdint.h>

constexpr uint16_t kPanelWidth = 480;
constexpr uint16_t kPanelHeight = 480;

struct PanelTiming {
  uint16_t hsyncPolarity;
  uint16_t hsyncFrontPorch;
  uint16_t hsyncPulseWidth;
  uint16_t hsyncBackPorch;
  uint16_t vsyncPolarity;
  uint16_t vsyncFrontPorch;
  uint16_t vsyncPulseWidth;
  uint16_t vsyncBackPorch;
  uint16_t pclkActiveNeg;
  uint16_t pclkIdleHigh;
  uint32_t pixelClockHz;
};

constexpr PanelTiming kPanelTiming = {1, 46, 2, 44, 1, 16, 2, 18, 0, 0, 17700000};

constexpr uint8_t kExpanderAddress = 0x3F;

constexpr unsigned long kSerialBaud = 115200;

constexpr uint16_t kBounceLines = 30;
constexpr uint32_t kStripSlots = 3;
constexpr uint16_t kStripsPerFrame = kPanelHeight / kBounceLines;
constexpr uint32_t kProducerPriority = 24;

constexpr uint32_t kBacklightPwmHz = 19531;
constexpr uint8_t kBacklightPwmBits = 12;
constexpr float kBacklightGamma = 2.2f;
