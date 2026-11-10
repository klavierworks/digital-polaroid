#include "slideshow.h"

#include "constants.h"
#include "inputs/wifi.h"

bool Slideshow::begin(uint32_t now) {
  if (!panel_.begin()) {
    Serial.println("slideshow: panel init FAILED");
    return false;
  }
  Serial.println("slideshow: panel ready, holding white until the first image");
  panel_.setWhiteness(UINT8_MAX, 0, 0, now);
  panel_.setBacklight(brightness_, 0, 0, now);
  buttons_.begin(panel_.expander());
  return fetcher_.begin();
}

void Slideshow::tick(uint32_t now) {
  panel_.update(now);
  handle(buttons_.poll(now), now);
  switch (phase_) {
    case Phase::Waiting:
      if (uint16_t* image = fetcher_.ready()) {
        Serial.println("slideshow: first image arrived");
        show(image, now);
      }
      break;
    case Phase::Holding:
      if (!due(now)) {
        break;
      }
      if (uint16_t* image = fetcher_.ready()) {
        show(image, now);
      } else if (!late_) {
        late_ = true;
        Serial.printf("slideshow: hold over but next image not ready (fetch %s)\n",
                      fetcher_.stage());
      }
      break;
  }
}

void Slideshow::report(uint32_t now) {
  const RenderStats stats = panel_.takeStats(now);
  Serial.printf(
      "alive t=%lu slides=%lu phase=%s fetch=%s wifi=%s fps=%lu underruns=%lu heap=%u "
      "psram=%u\n",
      static_cast<unsigned long>(now), static_cast<unsigned long>(slides_), phaseName(),
      fetcher_.stage(), wifiState(), static_cast<unsigned long>(stats.fps),
      static_cast<unsigned long>(stats.underruns),
                static_cast<unsigned>(ESP.getFreeHeap()),
                static_cast<unsigned>(ESP.getFreePsram()));
}

void Slideshow::show(uint16_t* image, uint32_t now) {
  panel_.drawFrame(image);
  fetcher_.release();
  panel_.setWhiteness(0, 0, 0, now);
  late_ = false;
  phase_ = Phase::Holding;
  dueAt_ = now + kHoldMs;
  slides_++;
  Serial.printf("slideshow: showing image %lu\n", static_cast<unsigned long>(slides_));
}

void Slideshow::handle(Press press, uint32_t now) {
  if (press == Press::None) {
    return;
  }
  if (press == Press::Both) {
    Serial.println("slideshow: skip requested");
    dueAt_ = now;
    return;
  }
  const int step = press == Press::Up ? kBrightnessStep : -kBrightnessStep;
  brightness_ = static_cast<uint8_t>(
      constrain(static_cast<int>(brightness_) + step, kMinBrightness, UINT8_MAX));
  panel_.setBacklight(brightness_, kBrightnessFadeMs, 0, now);
}

bool Slideshow::due(uint32_t now) const {
  return static_cast<int32_t>(now - dueAt_) >= 0;
}

const char* Slideshow::phaseName() const {
  switch (phase_) {
    case Phase::Waiting:
      return "waiting-for-first-image";
    case Phase::Holding:
      return "holding";
  }
  return "unknown";
}
