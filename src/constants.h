#pragma once

#include <stddef.h>
#include <stdint.h>

constexpr uint32_t kHoldMs = 60000;
constexpr uint8_t kBrightness = 120;
constexpr uint8_t kBrightnessStep = 16;
constexpr uint8_t kMinBrightness = 8;
constexpr uint16_t kBrightnessFadeMs = 120;
constexpr uint32_t kButtonPollMs = 20;
constexpr uint32_t kChordMs = 80;

constexpr size_t kAlbumPhotos = 20;
constexpr uint32_t kAlbumRefreshMs = 5 * 60 * 1000;

constexpr size_t kMaxDownloadBytes = 3 * 1024 * 1024;
constexpr size_t kMaxUrlBytes = 1024;
constexpr uint16_t kFetchTimeoutMs = 15000;
constexpr uint32_t kRetryMs = 10000;
constexpr uint32_t kWifiPollMs = 500;
