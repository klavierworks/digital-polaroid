#include "panel.h"

#include <Arduino.h>
#include <Wire.h>
#include <rom/cache.h>

#include "../config.h"
#include "panel_init.h"

namespace {

constexpr uint8_t kBayer[4][4] = {
    {0, 8, 2, 10},
    {12, 4, 14, 6},
    {3, 11, 1, 9},
    {15, 7, 13, 5},
};

uint16_t widen(uint8_t level) {
  return static_cast<uint16_t>(level * 257u);
}

constexpr uint32_t kSolidWhite = 256;
constexpr uint16_t kEasedFadeMs = 1000;
constexpr uint16_t kBacklightRows = 32;

uint16_t blendPixel(uint16_t pixel, const uint8_t* five, const uint8_t* six) {
  return static_cast<uint16_t>((five[(pixel >> 11) & 0x1F] << 11) |
                               (six[(pixel >> 5) & 0x3F] << 5) | five[pixel & 0x1F]);
}

uint32_t smoothstep(uint32_t t) {
  const uint64_t squared = (static_cast<uint64_t>(t) * t) >> 16;
  return static_cast<uint32_t>((squared * ((3u << 16) - 2 * t)) >> 16);
}

}  // namespace

void Panel::Fade::set(uint16_t target) {
  current = target;
  active = false;
  armed = false;
}

void Panel::Fade::start(uint16_t target, uint16_t transitionMs, uint16_t delayMs,
                        uint32_t now) {
  if (delayMs == 0) {
    if (transitionMs == 0) {
      set(target);
      return;
    }
    armed = false;
    from = current;
    to = target;
    startMs = now;
    durationMs = transitionMs;
    active = true;
    return;
  }
  armedTo = target;
  armedStartMs = now + delayMs;
  armedDurationMs = transitionMs;
  armed = true;
}

void Panel::Fade::release(uint32_t now) {
  if (!armed || static_cast<int32_t>(now - armedStartMs) < 0) {
    return;
  }
  armed = false;
  from = current;
  to = armedTo;
  startMs = armedStartMs;
  durationMs = armedDurationMs;
  active = true;
}

void Panel::Fade::advance(uint32_t now) {
  release(now);
  if (!active) {
    return;
  }
  const uint32_t elapsed = now - startMs;
  if (elapsed >= durationMs) {
    current = to;
    active = false;
    return;
  }
  const uint32_t t =
      static_cast<uint32_t>((static_cast<uint64_t>(elapsed) << 16) / durationMs);
  const uint32_t eased = durationMs < kEasedFadeMs ? t : smoothstep(t);
  const int32_t span = static_cast<int32_t>(to) - static_cast<int32_t>(from);
  current = static_cast<uint16_t>(
      static_cast<int32_t>(from) +
      static_cast<int32_t>((static_cast<int64_t>(span) * eased) >> 16));
}

bool Panel::begin() {
  Serial.printf("qualiabridge: panel %ux%u\n", kPanelWidth, kPanelHeight);

  Wire.begin();

  expander_ = new Arduino_XCA9554SWSPI(
      PCA_TFT_RESET, PCA_TFT_CS, PCA_TFT_SCK, PCA_TFT_MOSI, &Wire, kExpanderAddress);
  if (!expander_->begin()) {
    Serial.println("qualiabridge: expander FAILED");
    return false;
  }
  expander_->sendCommand(0x01);
  delay(120);
  expander_->batchOperation(kPanelInit, kPanelInitLen);

  source_ = static_cast<uint16_t*>(heap_caps_malloc(
      static_cast<size_t>(kPanelWidth) * kPanelHeight * sizeof(uint16_t),
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (source_ == nullptr) {
    Serial.println("qualiabridge: source buffer FAILED");
    return false;
  }

  if (!ring_.begin(static_cast<size_t>(kPanelWidth) * kBounceLines * sizeof(uint16_t),
                   kPanelWidth * kBounceLines)) {
    Serial.println("qualiabridge: strip ring FAILED");
    return false;
  }

  if (!scanout_.begin(kPanelWidth, kPanelHeight, kPanelTiming, ring_)) {
    Serial.println("qualiabridge: scanout FAILED");
    return false;
  }

  if (xTaskCreatePinnedToCore(blendTask, "blend", 4096, this, 2, &helper_, 1) != pdPASS) {
    helper_ = nullptr;
    Serial.println("qualiabridge: blend helper FAILED, blending on one core");
  }
  if (xTaskCreatePinnedToCore(produceTask, "produce", 4096, this, kProducerPriority,
                              &producer_, 0) !=
      pdPASS) {
    Serial.println("qualiabridge: producer FAILED");
    return false;
  }

  buildGammaTable();

  expander_->pinMode(PCA_TFT_BACKLIGHT, INPUT);

  if (!ledcSetClockSource(LEDC_USE_APB_CLK)) {
    Serial.println("qualiabridge: backlight pwm clock source rejected");
  }
  if (!ledcAttach(A1, kBacklightPwmHz, kBacklightPwmBits)) {
    Serial.printf("qualiabridge: backlight pwm attach FAILED on pin %u (%lu Hz, %u bits)\n",
                  A1, static_cast<unsigned long>(kBacklightPwmHz), kBacklightPwmBits);
  } else {
    Serial.printf("qualiabridge: backlight pwm on pin %u (%lu Hz, %u bits)\n", A1,
                  static_cast<unsigned long>(kBacklightPwmHz), kBacklightPwmBits);
  }
  backlight_.set(UINT16_MAX);
  applyBacklight();

  selfTest();
  return true;
}

Arduino_XCA9554SWSPI* Panel::expander() const {
  return expander_;
}

void Panel::setBacklight(uint8_t level, uint16_t transitionMs, uint16_t delayMs,
                         uint32_t now) {
  Serial.printf("qualiabridge: backlight %u over %u ms in %u ms\n", level, transitionMs,
                delayMs);
  backlight_.start(widen(level), transitionMs, delayMs, now);
}

void Panel::setWhiteness(uint8_t level, uint16_t transitionMs, uint16_t delayMs,
                         uint32_t now) {
  white_.start(widen(level), transitionMs, delayMs, now);
}

void Panel::update(uint32_t now) {
  advanceBacklight(now);
  white_.advance(now);
}

uint32_t Panel::blendAlpha() const {
  return (static_cast<uint32_t>(white_.current) + 128) >> 8;
}

void Panel::buildGammaTable() {
  const uint32_t dutyMax = (1u << kBacklightPwmBits) - 1;
  for (uint32_t i = 0; i < 256; ++i) {
    gammaDuty_[i] = static_cast<uint16_t>(
        lroundf(powf(i / 255.0f, kBacklightGamma) * dutyMax));
  }
}

void Panel::advanceBacklight(uint32_t now) {
  backlight_.advance(now);
  applyBacklight();
}

void Panel::applyBacklight() {
  const uint16_t level = backlight_.current;
  uint32_t duty;
  if (level == UINT16_MAX) {
    duty = 1u << kBacklightPwmBits;
  } else {
    const uint8_t index = level >> 8;
    const uint16_t base = gammaDuty_[index];
    const uint16_t next = gammaDuty_[index == 255 ? 255 : index + 1];
    duty = base + ((static_cast<uint32_t>(next - base) * (level & 0xFF)) >> 8);
  }
  if (duty != appliedDuty_) {
    if (!ledcWrite(A1, duty)) {
      Serial.printf("qualiabridge: backlight write FAILED (duty %lu)\n",
                    static_cast<unsigned long>(duty));
    }
    appliedDuty_ = duty;
  }
}

void Panel::selfTest() {
  const uint16_t colours[] = {RGB565_RED, RGB565_GREEN, RGB565_BLUE, RGB565_WHITE};
  const char* names[] = {"red", "green", "blue", "white"};
  for (size_t i = 0; i < 4; ++i) {
    Serial.printf("qualiabridge: self-test %s\n", names[i]);
    fill(colours[i]);
    delay(500);
  }
  stripes();
  Serial.println("qualiabridge: self-test stripes");
}

void Panel::fill(uint16_t colour) {
  if (source_ == nullptr) {
    return;
  }
  const size_t count = static_cast<size_t>(kPanelWidth) * kPanelHeight;
  for (size_t i = 0; i < count; ++i) {
    source_[i] = colour;
  }
  hasFrame_ = true;
}

void Panel::stripes() {
  if (source_ == nullptr) {
    return;
  }
  for (uint16_t y = 0; y < kPanelHeight; ++y) {
    uint16_t* row = source_ + static_cast<size_t>(y) * kPanelWidth;
    for (uint16_t x = 0; x < kPanelWidth; ++x) {
      row[x] = (x / 8) % 2 == 0 ? RGB565_RED : RGB565_BLUE;
    }
  }
  hasFrame_ = true;
}

void Panel::drawFrame(uint16_t* canvas) {
  if (source_ == nullptr) {
    return;
  }
  memcpy(source_, canvas,
         static_cast<size_t>(kPanelWidth) * kPanelHeight * sizeof(uint16_t));
  hasFrame_ = true;
}

RenderStats Panel::takeStats(uint32_t now) {
  const uint32_t frames = scanout_.frames();
  const uint32_t window = now - reportedAt_;
  const RenderStats stats = {
      appliedDuty_,
      window > 0 ? (frames - reportedFrames_) * 1000 / window : 0,
      window,
      renders_,
      renders_ > 0 ? renderUs_ / renders_ : 0,
      renders_ > 0 ? blendUs_ / renders_ : 0,
      ring_.takeUnderruns(),
      peakUs_,
  };
  reportedFrames_ = frames;
  reportedAt_ = now;
  renders_ = 0;
  renderUs_ = 0;
  blendUs_ = 0;
  peakUs_ = 0;
  return stats;
}

void Panel::produce() {
  uint32_t alpha = blendAlpha();
  uint32_t started = micros();
  uint32_t blended = 0;
  bool framed = false;
  for (;;) {
    uint16_t* slot = ring_.claim();
    const uint16_t strip = ring_.claimedStrip();
    if (strip == 0) {
      if (framed) {
        const uint32_t spent = micros() - started;
        renders_ += 1;
        renderUs_ += spent;
        blendUs_ += blended;
        if (spent > peakUs_) {
          peakUs_ = spent;
        }
      }
      started = micros();
      blended = 0;
      framed = true;
      alpha = blendAlpha();
      if (alpha > 0 && alpha < kSolidWhite && alpha != tablesAlpha_) {
        buildBlendTables(alpha);
      }
    }
    const uint32_t stripStart = micros();
    fillStrip(slot, strip * kBounceLines, alpha);
    blended += micros() - stripStart;
    ring_.publish();
  }
}

void Panel::fillStrip(uint16_t* slot, uint16_t firstRow, uint32_t alpha) {
  const size_t bytes =
      static_cast<size_t>(kPanelWidth) * kBounceLines * sizeof(uint16_t);
  if (!hasFrame_) {
    memset(slot, 0, bytes);
    return;
  }
  if (alpha >= kSolidWhite) {
    memset(slot, 0xFF, bytes);
    return;
  }
  if (alpha == 0) {
    memcpy(slot, source_ + static_cast<size_t>(firstRow) * kPanelWidth, bytes);
    return;
  }
  if (helper_ == nullptr) {
    blendRows(slot, firstRow, kBounceLines);
    return;
  }
  const uint16_t half = kBounceLines / 2;
  primary_ = xTaskGetCurrentTaskHandle();
  helperDestination_ = slot + static_cast<size_t>(half) * kPanelWidth;
  helperFirstRow_ = firstRow + half;
  helperRowCount_ = kBounceLines - half;
  xTaskNotifyGive(helper_);
  blendRows(slot, firstRow, half);
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
}

void Panel::blendRows(uint16_t* destination, uint16_t firstRow, uint16_t rowCount) {
  for (uint16_t i = 0; i < rowCount; ++i) {
    const uint16_t y = firstRow + i;
    const uint16_t* row = source_ + static_cast<size_t>(y) * kPanelWidth;
    if (i + 1 < rowCount) {
      Cache_Start_DCache_Preload(reinterpret_cast<uint32_t>(row + kPanelWidth),
                                 kPanelWidth * sizeof(uint16_t), 0);
    }
    processRow(row, destination + static_cast<size_t>(i) * kPanelWidth, y);
  }
}

void Panel::produceTask(void* arg) {
  static_cast<Panel*>(arg)->produce();
}

void Panel::blendTask(void* arg) {
  Panel* panel = static_cast<Panel*>(arg);
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    panel->blendRows(panel->helperDestination_, panel->helperFirstRow_,
                     panel->helperRowCount_);
    xTaskNotifyGive(panel->primary_);
  }
}

__attribute__((optimize("O3"))) void Panel::processRow(const uint16_t* source,
                                                       uint16_t* out, uint16_t row) {
  const uint8_t* bayer = kBayer[row & 3];
  const uint8_t* fiveA = lut5_[bayer[0] >> 1];
  const uint8_t* fiveB = lut5_[bayer[1] >> 1];
  const uint8_t* fiveC = lut5_[bayer[2] >> 1];
  const uint8_t* fiveD = lut5_[bayer[3] >> 1];
  const uint8_t* sixA = lut6_[bayer[0] >> 2];
  const uint8_t* sixB = lut6_[bayer[1] >> 2];
  const uint8_t* sixC = lut6_[bayer[2] >> 2];
  const uint8_t* sixD = lut6_[bayer[3] >> 2];
  for (uint16_t x = 0; x < kPanelWidth; x += 4) {
    out[x] = blendPixel(source[x], fiveA, sixA);
    out[x + 1] = blendPixel(source[x + 1], fiveB, sixB);
    out[x + 2] = blendPixel(source[x + 2], fiveC, sixC);
    out[x + 3] = blendPixel(source[x + 3], fiveD, sixD);
  }
}

void Panel::buildBlendTables(uint32_t alpha) {
  for (uint32_t value = 0; value < 32; ++value) {
    const uint32_t wide = (value << 3) | (value >> 2);
    const uint32_t blended = wide + (((255 - wide) * alpha) >> 8);
    for (uint32_t dither = 0; dither < 8; ++dither) {
      const uint32_t dithered = (blended + dither) >> 3;
      lut5_[dither][value] = static_cast<uint8_t>(dithered > 31 ? 31 : dithered);
    }
  }
  for (uint32_t value = 0; value < 64; ++value) {
    const uint32_t wide = (value << 2) | (value >> 4);
    const uint32_t blended = wide + (((255 - wide) * alpha) >> 8);
    for (uint32_t dither = 0; dither < 4; ++dither) {
      const uint32_t dithered = (blended + dither) >> 2;
      lut6_[dither][value] = static_cast<uint8_t>(dithered > 63 ? 63 : dithered);
    }
  }
  tablesAlpha_ = alpha;
}
