#include <Arduino.h>

#include "config.h"
#include "inputs/wifi.h"
#include "slideshow.h"

namespace {

constexpr uint32_t kReportMs = 5000;

Slideshow slideshow;

}  // namespace

void setup() {
  Serial.begin(kSerialBaud);
  delay(300);
  Serial.println();
  Serial.printf("boot: psram free %u\n", static_cast<unsigned>(ESP.getFreePsram()));
  startWifi();
  Serial.println(slideshow.begin(millis()) ? "boot: slideshow ready" : "boot: slideshow init FAILED");
}

void loop() {
  const uint32_t now = millis();
  slideshow.tick(now);
  delay(1);
}
