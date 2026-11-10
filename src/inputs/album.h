#pragma once

#include <stddef.h>
#include <stdint.h>

#include "../constants.h"
#include "../secrets.h"
#include "download.h"

class Album {
 public:
  bool begin();
  bool refresh(uint8_t* scratch, size_t capacity, uint32_t now);
  bool current(uint32_t now) const;
  void expire();
  const char* next();

 private:
  struct Pick {
    char guid[48];
    char checksum[64];
  };

  bool locate(uint8_t* scratch, size_t capacity, Response& stream);
  size_t pickPhotos(const uint8_t* json, size_t length, Pick* picks);
  size_t resolveUrls(const Pick* picks, size_t count, uint8_t* scratch, size_t capacity);
  char* slot(size_t index) const;

  char base_[256] = {0};
  char* urls_ = nullptr;
  size_t count_ = 0;
  size_t cursor_ = 0;
  uint32_t refreshedAt_ = 0;
  bool loaded_ = false;
};
