#pragma once

#include <Arduino.h>
#include <stdint.h>

#include "album.h"

class Fetcher {
 public:
  bool begin();
  uint16_t* ready();
  void release();
  const char* stage() const;

 private:
  enum class Stage { Idle, WaitingForWifi, ReadingAlbum, Downloading, Decoding, Retrying, Ready };

  static void task(void* arg);
  void run();
  bool fetchOnce();

  Album album_;
  uint8_t* jpeg_ = nullptr;
  uint16_t* canvas_ = nullptr;
  QueueHandle_t requests_ = nullptr;
  QueueHandle_t results_ = nullptr;
  bool holding_ = false;
  volatile Stage stage_ = Stage::Idle;
};
