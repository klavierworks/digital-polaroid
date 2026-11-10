#include "fetcher.h"

#include "../config.h"
#include "../constants.h"
#include "decode.h"
#include "download.h"
#include "wifi.h"

namespace {

constexpr uint32_t kTaskStack = 16384;
constexpr UBaseType_t kTaskPriority = 1;
constexpr BaseType_t kTaskCore = 1;
constexpr uint32_t kWifiReportMs = 5000;

}  // namespace

bool Fetcher::begin() {
  jpeg_ = static_cast<uint8_t*>(ps_malloc(kMaxDownloadBytes));
  canvas_ = static_cast<uint16_t*>(
      ps_malloc(static_cast<size_t>(kPanelWidth) * kPanelHeight * sizeof(uint16_t)));
  requests_ = xQueueCreate(1, sizeof(uint8_t));
  results_ = xQueueCreate(1, sizeof(uint8_t));
  Serial.printf("fetch: jpeg buffer %s, canvas %s, queues %s\n", jpeg_ ? "ok" : "FAILED",
                canvas_ ? "ok" : "FAILED",
                requests_ != nullptr && results_ != nullptr ? "ok" : "FAILED");
  if (jpeg_ == nullptr || canvas_ == nullptr || requests_ == nullptr || results_ == nullptr ||
      !album_.begin()) {
    return false;
  }
  if (xTaskCreatePinnedToCore(task, "fetch", kTaskStack, this, kTaskPriority, nullptr,
                              kTaskCore) != pdPASS) {
    Serial.println("fetch: task FAILED");
    return false;
  }
  Serial.println("fetch: task started");
  release();
  return true;
}

uint16_t* Fetcher::ready() {
  uint8_t token = 0;
  if (!holding_ && xQueueReceive(results_, &token, 0) == pdTRUE) {
    holding_ = true;
  }
  return holding_ ? canvas_ : nullptr;
}

void Fetcher::release() {
  holding_ = false;
  const uint8_t token = 0;
  xQueueSend(requests_, &token, 0);
}

const char* Fetcher::stage() const {
  switch (stage_) {
    case Stage::Idle:
      return "idle";
    case Stage::WaitingForWifi:
      return "waiting-for-wifi";
    case Stage::ReadingAlbum:
      return "reading-album";
    case Stage::Downloading:
      return "downloading";
    case Stage::Decoding:
      return "decoding";
    case Stage::Retrying:
      return "retrying";
    case Stage::Ready:
      return "ready";
  }
  return "unknown";
}

void Fetcher::task(void* arg) {
  static_cast<Fetcher*>(arg)->run();
}

void Fetcher::run() {
  uint8_t token = 0;
  for (;;) {
    stage_ = Stage::Idle;
    xQueueReceive(requests_, &token, portMAX_DELAY);
    Serial.println("fetch: next image requested");
    while (!fetchOnce()) {
      stage_ = Stage::Retrying;
      Serial.printf("fetch: failed, retrying in %lu ms\n", static_cast<unsigned long>(kRetryMs));
      vTaskDelay(pdMS_TO_TICKS(kRetryMs));
    }
    stage_ = Stage::Ready;
    Serial.println("fetch: image ready for the slideshow");
    xQueueSend(results_, &token, portMAX_DELAY);
  }
}

bool Fetcher::fetchOnce() {
  stage_ = Stage::WaitingForWifi;
  uint32_t waitingSince = millis();
  while (!wifiUp()) {
    if (millis() - waitingSince >= kWifiReportMs) {
      Serial.printf("fetch: still waiting for wifi (%s)\n", wifiState());
      waitingSince = millis();
    }
    vTaskDelay(pdMS_TO_TICKS(kWifiPollMs));
  }
  if (!album_.current(millis())) {
    stage_ = Stage::ReadingAlbum;
    if (!album_.refresh(jpeg_, kMaxDownloadBytes, millis())) {
      return false;
    }
  }
  stage_ = Stage::Downloading;
  const uint32_t started = millis();
  const Response photo = request(album_.next(), nullptr, jpeg_, kMaxDownloadBytes);
  if (photo.status >= 400) {
    Serial.println("fetch: photo refused, the album will be read again");
    album_.expire();
  }
  if (photo.status != 200 || photo.length == 0) {
    return false;
  }
  const size_t length = photo.length;
  stage_ = Stage::Decoding;
  const uint32_t downloaded = millis();
  if (!decodeInto(jpeg_, length, canvas_)) {
    return false;
  }
  Serial.printf("fetch: image %u bytes, download %lu ms, decode %lu ms\n",
                static_cast<unsigned>(length),
                static_cast<unsigned long>(downloaded - started),
                static_cast<unsigned long>(millis() - downloaded));
  return true;
}
