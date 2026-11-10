#include "decode.h"

#include <Arduino.h>
#include <JPEGDEC.h>

#include <new>

#include "../config.h"

namespace {

constexpr int kRowsPerPause = 16;
constexpr int kScales[] = {8, 4, 2};

struct Target {
  uint16_t* pixels;
  int width;
  int height;
};

JPEGDEC* decoder = nullptr;
Target target = {nullptr, 0, 0};

int drawBlock(JPEGDRAW* block) {
  for (int row = 0; row < block->iHeight; ++row) {
    const int y = block->y + row;
    if (y >= target.height) {
      break;
    }
    const int width = min(block->iWidth, target.width - block->x);
    if (width <= 0) {
      continue;
    }
    memcpy(target.pixels + static_cast<size_t>(y) * target.width + block->x,
           block->pPixels + row * block->iWidth, width * sizeof(uint16_t));
  }
  return 1;
}

int chooseScale(int width, int height) {
  const int shorter = min(width, height);
  for (int scale : kScales) {
    if (shorter / scale >= kPanelWidth) {
      return scale;
    }
  }
  return 1;
}

int scaleOption(int scale) {
  switch (scale) {
    case 8:
      return JPEG_SCALE_EIGHTH;
    case 4:
      return JPEG_SCALE_QUARTER;
    case 2:
      return JPEG_SCALE_HALF;
    default:
      return 0;
  }
}

void pace(int row) {
  if (row % kRowsPerPause == kRowsPerPause - 1) {
    vTaskDelay(1);
  }
}

uint16_t mix(uint16_t a, uint16_t b, uint32_t weight) {
  const uint32_t spreadA = (a | static_cast<uint32_t>(a) << 16) & 0x07E0F81F;
  const uint32_t spreadB = (b | static_cast<uint32_t>(b) << 16) & 0x07E0F81F;
  const uint32_t blended = (spreadA + (((spreadB - spreadA) * weight) >> 5)) & 0x07E0F81F;
  return static_cast<uint16_t>(blended | blended >> 16);
}

int32_t clampCoordinate(int32_t value, int size) {
  const int32_t highest = static_cast<int32_t>(size - 1) << 16;
  return value < 0 ? 0 : (value > highest ? highest : value);
}

void coverPanel(const Target& source, uint16_t* canvas) {
  const int shorter = min(source.width, source.height);
  const int32_t step = (static_cast<int32_t>(shorter) << 16) / kPanelWidth;
  const int32_t startX =
      ((static_cast<int32_t>(source.width) << 16) - step * kPanelWidth) / 2 + step / 2 - 0x8000;
  const int32_t startY =
      ((static_cast<int32_t>(source.height) << 16) - step * kPanelHeight) / 2 + step / 2 -
      0x8000;
  for (int y = 0; y < kPanelHeight; ++y) {
    const int32_t v = clampCoordinate(startY + step * y, source.height);
    const int top = v >> 16;
    const int bottom = min(top + 1, source.height - 1);
    const uint32_t down = (v >> 11) & 0x1F;
    const uint16_t* upper = source.pixels + static_cast<size_t>(top) * source.width;
    const uint16_t* lower = source.pixels + static_cast<size_t>(bottom) * source.width;
    uint16_t* out = canvas + static_cast<size_t>(y) * kPanelWidth;
    for (int x = 0; x < kPanelWidth; ++x) {
      const int32_t u = clampCoordinate(startX + step * x, source.width);
      const int left = u >> 16;
      const int right = min(left + 1, source.width - 1);
      const uint32_t across = (u >> 11) & 0x1F;
      out[x] = mix(mix(upper[left], upper[right], across), mix(lower[left], lower[right], across),
                   down);
    }
    pace(y);
  }
}

JPEGDEC* createDecoder() {
  void* memory = ps_malloc(sizeof(JPEGDEC));
  return memory ? new (memory) JPEGDEC() : nullptr;
}

}  // namespace

bool decodeInto(const uint8_t* data, size_t length, uint16_t* canvas) {
  if (decoder == nullptr) {
    decoder = createDecoder();
  }
  if (decoder == nullptr) {
    Serial.println("decode: decoder allocation FAILED");
    return false;
  }
  JPEGDEC& jpeg = *decoder;
  if (!jpeg.openRAM(const_cast<uint8_t*>(data), static_cast<int>(length), drawBlock)) {
    Serial.printf("decode: not a readable jpeg (error %d)\n", jpeg.getLastError());
    return false;
  }
  if (jpeg.getJPEGType() == JPEG_MODE_PROGRESSIVE) {
    Serial.println("decode: progressive jpeg, skipped");
    jpeg.close();
    return false;
  }
  const int width = jpeg.getWidth();
  const int height = jpeg.getHeight();
  const int scale = chooseScale(width, height);
  target = {nullptr, (width + scale - 1) / scale, (height + scale - 1) / scale};
  target.pixels = static_cast<uint16_t*>(
      ps_malloc(static_cast<size_t>(target.width) * target.height * sizeof(uint16_t)));
  Serial.printf("decode: %dx%d at 1/%d is %dx%d, buffer %s, psram free %u\n", width, height,
                scale, target.width, target.height, target.pixels ? "ok" : "FAILED",
                static_cast<unsigned>(ESP.getFreePsram()));
  if (target.pixels == nullptr) {
    jpeg.close();
    return false;
  }
  jpeg.setPixelType(RGB565_LITTLE_ENDIAN);
  const bool decoded = jpeg.decode(0, 0, scaleOption(scale)) == 1;
  jpeg.close();
  if (decoded) {
    coverPanel(target, canvas);
  } else {
    Serial.printf("decode: failed (error %d)\n", jpeg.getLastError());
  }
  free(target.pixels);
  target.pixels = nullptr;
  return decoded;
}
