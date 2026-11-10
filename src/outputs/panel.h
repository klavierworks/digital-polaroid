#pragma once

#include <Arduino_GFX_Library.h>

#include "../config.h"
#include "scanout.h"
#include "strips.h"

struct RenderStats {
  uint32_t duty;
  uint32_t fps;
  uint32_t windowMs;
  uint32_t renders;
  uint32_t meanUs;
  uint32_t blendUs;
  uint32_t underruns;
  uint32_t peakUs;
};

class Panel {
 public:
  bool begin();
  Arduino_XCA9554SWSPI* expander() const;
  void drawFrame(uint16_t* canvas);
  void setBacklight(uint8_t level, uint16_t transitionMs, uint16_t delayMs, uint32_t now);
  void setWhiteness(uint8_t level, uint16_t transitionMs, uint16_t delayMs, uint32_t now);
  void update(uint32_t now);
  RenderStats takeStats(uint32_t now);

 private:
  struct Fade {
    uint16_t current = 0;
    uint16_t from = 0;
    uint16_t to = 0;
    uint32_t startMs = 0;
    uint16_t durationMs = 0;
    bool active = false;
    uint16_t armedTo = 0;
    uint32_t armedStartMs = 0;
    uint16_t armedDurationMs = 0;
    bool armed = false;

    void set(uint16_t target);
    void start(uint16_t target, uint16_t transitionMs, uint16_t delayMs, uint32_t now);
    void release(uint32_t now);
    void advance(uint32_t now);
  };

  void selfTest();
  void fill(uint16_t colour);
  void stripes();
  uint32_t blendAlpha() const;
  void buildGammaTable();
  void buildBlendTables(uint32_t alpha);
  void applyBacklight();
  void advanceBacklight(uint32_t now);
  void produce();
  void fillStrip(uint16_t* slot, uint16_t firstRow, uint32_t alpha);
  void blendRows(uint16_t* destination, uint16_t firstRow, uint16_t rowCount);
  static void produceTask(void* arg);
  static void blendTask(void* arg);
  void processRow(const uint16_t* source, uint16_t* out, uint16_t row);

  Arduino_XCA9554SWSPI* expander_ = nullptr;
  StripRing ring_;
  Scanout scanout_;
  uint16_t* source_ = nullptr;
  TaskHandle_t helper_ = nullptr;
  TaskHandle_t primary_ = nullptr;
  TaskHandle_t producer_ = nullptr;
  uint16_t* helperDestination_ = nullptr;
  uint16_t helperFirstRow_ = 0;
  uint16_t helperRowCount_ = 0;
  uint16_t gammaDuty_[256] = {0};
  uint8_t lut5_[8][32] = {{0}};
  uint8_t lut6_[4][64] = {{0}};
  uint32_t tablesAlpha_ = UINT32_MAX;
  uint32_t appliedDuty_ = UINT32_MAX;
  uint32_t renders_ = 0;
  uint32_t renderUs_ = 0;
  uint32_t blendUs_ = 0;
  uint32_t peakUs_ = 0;
  uint32_t reportedFrames_ = 0;
  uint32_t reportedAt_ = 0;
  Fade backlight_;
  Fade white_;
  bool hasFrame_ = false;
};
