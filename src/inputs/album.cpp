#include "album.h"

#include <Arduino.h>
#include <ArduinoJson.h>

#include <vector>

namespace {

constexpr int kRedirectStatus = 330;
constexpr const char* kStreamBody = "{\"streamCtag\":null}";
constexpr const char* kBase62 =
    "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

struct PsramAllocator : ArduinoJson::Allocator {
  void* allocate(size_t size) override { return ps_malloc(size); }
  void deallocate(void* pointer) override { free(pointer); }
  void* reallocate(void* pointer, size_t size) override { return ps_realloc(pointer, size); }
};

PsramAllocator psram;

int base62(const char* digits, size_t count) {
  int value = 0;
  for (size_t i = 0; i < count; ++i) {
    const char* found = strchr(kBase62, digits[i]);
    value = value * 62 + (found ? static_cast<int>(found - kBase62) : 0);
  }
  return value;
}

int partition(const char* token) {
  return token[0] == 'A' ? base62(token + 1, 1) : base62(token + 1, 2);
}

long number(JsonVariantConst value) {
  return value.is<const char*>() ? atol(value.as<const char*>()) : value.as<long>();
}

const char* largestChecksum(JsonObjectConst derivatives) {
  const char* checksum = nullptr;
  long largest = -1;
  for (JsonPairConst derivative : derivatives) {
    const long area = number(derivative.value()["width"]) * number(derivative.value()["height"]);
    if (area > largest && derivative.value()["checksum"].is<const char*>()) {
      largest = area;
      checksum = derivative.value()["checksum"].as<const char*>();
    }
  }
  return checksum;
}

void chooseRandomly(std::vector<uint16_t>& indices, size_t wanted) {
  for (size_t i = 0; i < wanted; ++i) {
    const size_t j = i + esp_random() % (indices.size() - i);
    std::swap(indices[i], indices[j]);
  }
}

}  // namespace

bool Album::begin() {
  urls_ = static_cast<char*>(ps_malloc(kAlbumPhotos * kMaxUrlBytes));
  snprintf(base_, sizeof(base_), "https://p%02d-sharedstreams.icloud.com/%s/sharedstreams/",
           partition(kAlbumToken), kAlbumToken);
  Serial.printf("album: url storage %s, server %.48s...\n", urls_ ? "ok" : "FAILED", base_);
  return urls_ != nullptr;
}

bool Album::current(uint32_t now) const {
  return loaded_ && count_ > 0 && now - refreshedAt_ < kAlbumRefreshMs;
}

void Album::expire() {
  loaded_ = false;
}

const char* Album::next() {
  if (count_ == 0) {
    return nullptr;
  }
  const char* url = slot(cursor_);
  cursor_ = (cursor_ + 1) % count_;
  return url;
}

bool Album::refresh(uint8_t* scratch, size_t capacity, uint32_t now) {
  Serial.println("album: fetching the photo list");
  Response stream{};
  if (!locate(scratch, capacity, stream)) {
    return false;
  }
  Pick picks[kAlbumPhotos];
  const size_t picked = pickPhotos(scratch, stream.length, picks);
  if (picked == 0) {
    return false;
  }
  const size_t resolved = resolveUrls(picks, picked, scratch, capacity);
  Serial.printf("album: %u of %u picked photos have download urls\n",
                static_cast<unsigned>(resolved), static_cast<unsigned>(picked));
  if (resolved == 0) {
    return false;
  }
  count_ = resolved;
  cursor_ = 0;
  refreshedAt_ = now;
  loaded_ = true;
  return true;
}

bool Album::locate(uint8_t* scratch, size_t capacity, Response& stream) {
  char url[320];
  snprintf(url, sizeof(url), "%swebstream", base_);
  stream = request(url, kStreamBody, scratch, capacity - 1);
  if (stream.status == kRedirectStatus) {
    scratch[stream.length] = 0;
    JsonDocument redirect;
    if (deserializeJson(redirect, reinterpret_cast<const char*>(scratch)) ||
        !redirect["X-Apple-MMe-Host"].is<const char*>()) {
      Serial.println("album: redirect without a host");
      return false;
    }
    snprintf(base_, sizeof(base_), "https://%s/%s/sharedstreams/",
             redirect["X-Apple-MMe-Host"].as<const char*>(), kAlbumToken);
    Serial.printf("album: redirected to %s\n", redirect["X-Apple-MMe-Host"].as<const char*>());
    snprintf(url, sizeof(url), "%swebstream", base_);
    stream = request(url, kStreamBody, scratch, capacity - 1);
  }
  if (stream.status != 200 || stream.length == 0) {
    Serial.printf("album: photo list failed with status %d\n", stream.status);
    return false;
  }
  return true;
}

size_t Album::pickPhotos(const uint8_t* json, size_t length, Pick* picks) {
  JsonDocument filter;
  filter["streamName"] = true;
  filter["photos"][0]["photoGuid"] = true;
  filter["photos"][0]["mediaAssetType"] = true;
  JsonObject derivative = filter["photos"][0]["derivatives"]["*"].to<JsonObject>();
  derivative["checksum"] = true;
  derivative["width"] = true;
  derivative["height"] = true;
  JsonDocument stream(&psram);
  const DeserializationError error = deserializeJson(
      stream, reinterpret_cast<const char*>(json), length, DeserializationOption::Filter(filter));
  if (error) {
    Serial.printf("album: photo list unreadable: %s\n", error.c_str());
    return 0;
  }
  JsonArrayConst items = stream["photos"];
  std::vector<uint16_t> photos;
  for (size_t i = 0; i < items.size(); ++i) {
    if (items[i]["mediaAssetType"] != "video") {
      photos.push_back(static_cast<uint16_t>(i));
    }
  }
  Serial.printf("album: \"%s\" lists %u items, %u photos, videos ignored\n",
                stream["streamName"] | "", static_cast<unsigned>(items.size()),
                static_cast<unsigned>(photos.size()));
  const size_t wanted = min(photos.size(), kAlbumPhotos);
  chooseRandomly(photos, wanted);
  size_t picked = 0;
  for (size_t i = 0; i < wanted; ++i) {
    JsonObjectConst photo = items[photos[i]];
    const char* guid = photo["photoGuid"];
    const char* checksum = largestChecksum(photo["derivatives"]);
    if (guid == nullptr || checksum == nullptr) {
      continue;
    }
    strlcpy(picks[picked].guid, guid, sizeof(picks[picked].guid));
    strlcpy(picks[picked].checksum, checksum, sizeof(picks[picked].checksum));
    picked++;
  }
  return picked;
}

size_t Album::resolveUrls(const Pick* picks, size_t count, uint8_t* scratch, size_t capacity) {
  String body = "{\"photoGuids\":[";
  for (size_t i = 0; i < count; ++i) {
    body += i ? ",\"" : "\"";
    body += picks[i].guid;
    body += "\"";
  }
  body += "]}";
  char url[320];
  snprintf(url, sizeof(url), "%swebasseturls", base_);
  const Response assets = request(url, body.c_str(), scratch, capacity);
  if (assets.status != 200 || assets.length == 0) {
    Serial.printf("album: download urls failed with status %d\n", assets.status);
    return 0;
  }
  JsonDocument document(&psram);
  const DeserializationError error =
      deserializeJson(document, reinterpret_cast<const char*>(scratch), assets.length);
  if (error) {
    Serial.printf("album: download urls unreadable: %s\n", error.c_str());
    return 0;
  }
  JsonObjectConst items = document["items"];
  size_t resolved = 0;
  for (size_t i = 0; i < count; ++i) {
    const char* checksum = picks[i].checksum;
    JsonObjectConst item = items[checksum];
    const char* location = item["url_location"];
    const char* path = item["url_path"];
    if (location == nullptr || path == nullptr) {
      Serial.printf("album: no download url for %s\n", checksum);
      continue;
    }
    const int written = snprintf(slot(resolved), kMaxUrlBytes, "https://%s%s", location, path);
    if (written > 0 && static_cast<size_t>(written) < kMaxUrlBytes) {
      resolved++;
    }
  }
  return resolved;
}

char* Album::slot(size_t index) const {
  return urls_ + index * kMaxUrlBytes;
}
