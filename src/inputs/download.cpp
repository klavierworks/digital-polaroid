#include "download.h"

#include <NetworkClientSecure.h>

#include "../constants.h"

namespace {

constexpr int kLoggedUrlChars = 96;
constexpr uint16_t kHttpsPort = 443;
constexpr size_t kMaxHostChars = 128;
constexpr size_t kMaxLineChars = 512;
constexpr int kMaxRedirects = 3;

struct Target {
  char host[kMaxHostChars];
  const char* path;
};

bool parseUrl(const char* url, Target& target) {
  constexpr const char* kScheme = "https://";
  if (strncmp(url, kScheme, strlen(kScheme)) != 0) {
    return false;
  }
  const char* host = url + strlen(kScheme);
  const char* slash = strchr(host, '/');
  const size_t length = slash ? static_cast<size_t>(slash - host) : strlen(host);
  if (length == 0 || length >= sizeof(target.host)) {
    return false;
  }
  memcpy(target.host, host, length);
  target.host[length] = 0;
  target.path = slash ? slash : "/";
  return true;
}

bool awaitData(NetworkClientSecure& client, uint32_t waitMs) {
  const uint32_t started = millis();
  while (client.available() == 0) {
    if (!client.connected() || millis() - started >= waitMs) {
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  return true;
}

bool readLine(NetworkClientSecure& client, char* line, size_t capacity) {
  size_t length = 0;
  for (;;) {
    if (!awaitData(client, kFetchTimeoutMs)) {
      return false;
    }
    const int byte = client.read();
    if (byte == '\n') {
      if (length > 0 && line[length - 1] == '\r') {
        length--;
      }
      line[length] = 0;
      return true;
    }
    if (byte >= 0 && length + 1 < capacity) {
      line[length++] = static_cast<char>(byte);
    }
  }
}

int readStatus(NetworkClientSecure& client) {
  char line[kMaxLineChars];
  if (!readLine(client, line, sizeof(line)) || strncmp(line, "HTTP/", 5) != 0) {
    return -1;
  }
  const char* space = strchr(line, ' ');
  return space ? atoi(space + 1) : -1;
}

struct Headers {
  long length = -1;
  char location[kMaxLineChars] = {0};
};

const char* headerValue(const char* line, const char* name) {
  const size_t length = strlen(name);
  if (strncasecmp(line, name, length) != 0) {
    return nullptr;
  }
  const char* value = line + length;
  while (*value == ' ') {
    value++;
  }
  return value;
}

bool readHeaders(NetworkClientSecure& client, Headers& headers) {
  char line[kMaxLineChars];
  while (readLine(client, line, sizeof(line))) {
    if (line[0] == 0) {
      return true;
    }
    if (const char* value = headerValue(line, "Content-Length:")) {
      headers.length = atol(value);
    } else if (const char* value = headerValue(line, "Location:")) {
      strlcpy(headers.location, value, sizeof(headers.location));
    }
  }
  return false;
}

bool isRedirect(int status) {
  return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

bool readBody(NetworkClientSecure& client, uint8_t* buffer, size_t limit, bool sized,
              size_t& received) {
  received = 0;
  uint32_t lastData = millis();
  uint32_t lastReport = lastData;
  for (;;) {
    const uint32_t now = millis();
    if (sized && received == limit) {
      return true;
    }
    const int waiting = client.available();
    if (waiting > 0) {
      if (received == limit) {
        Serial.printf("download: body exceeds the %u byte buffer\n",
                      static_cast<unsigned>(limit));
        return false;
      }
      const size_t want = min(static_cast<size_t>(waiting), limit - received);
      const int got = client.read(buffer + received, want);
      if (got > 0) {
        received += static_cast<size_t>(got);
        lastData = now;
      }
    } else if (!client.connected()) {
      if (sized) {
        Serial.printf("download: connection closed at %u of %u bytes\n",
                      static_cast<unsigned>(received), static_cast<unsigned>(limit));
      }
      return !sized;
    } else if (now - lastData >= kFetchTimeoutMs) {
      Serial.printf("download: no data for %u ms after %u bytes\n", kFetchTimeoutMs,
                    static_cast<unsigned>(received));
      return false;
    } else {
      vTaskDelay(1);
    }
    if (now - lastReport >= 1000) {
      lastReport = now;
      Serial.printf("download: %u bytes so far\n", static_cast<unsigned>(received));
    }
  }
}

void sendRequest(NetworkClientSecure& client, const Target& target, const char* postBody) {
  client.printf("%s %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: pictureframe\r\nAccept: */*\r\n",
                postBody ? "POST" : "GET", target.path, target.host);
  if (postBody != nullptr) {
    client.printf(
        "Origin: https://www.icloud.com\r\nContent-Type: text/plain\r\nContent-Length: %u\r\n",
        static_cast<unsigned>(strlen(postBody)));
  }
  client.print("Connection: close\r\n\r\n");
  if (postBody != nullptr) {
    client.print(postBody);
  }
}

Response fetchOnce(const char* url, const char* postBody, uint8_t* buffer, size_t capacity,
                   Headers& headers) {
  Serial.printf("download: %s %.*s\n", postBody ? "POST" : "GET", kLoggedUrlChars, url);
  Target target{};
  if (!parseUrl(url, target)) {
    Serial.println("download: unsupported url");
    return {0, 0};
  }
  NetworkClientSecure client;
  client.setInsecure();
  if (!client.connect(target.host, kHttpsPort, kFetchTimeoutMs)) {
    Serial.printf("download: could not connect to %s\n", target.host);
    return {0, 0};
  }
  sendRequest(client, target, postBody);
  const uint32_t sent = millis();
  if (!awaitData(client, kFetchTimeoutMs)) {
    Serial.printf("download: no reply within %u ms\n", kFetchTimeoutMs);
    client.stop();
    return {0, 0};
  }
  const int status = readStatus(client);
  const bool complete = readHeaders(client, headers);
  Serial.printf("download: status %d after %lu ms, content length %ld\n", status,
                static_cast<unsigned long>(millis() - sent), headers.length);
  if (status <= 0 || !complete) {
    Serial.println("download: malformed reply");
    client.stop();
    return {status, 0};
  }
  if (isRedirect(status)) {
    client.stop();
    return {status, 0};
  }
  if (headers.length > static_cast<long>(capacity)) {
    Serial.printf("download: %ld bytes exceeds the %u byte buffer\n", headers.length,
                  static_cast<unsigned>(capacity));
    client.stop();
    return {status, 0};
  }
  const bool sized = headers.length >= 0;
  size_t received = 0;
  const bool whole = readBody(client, buffer,
                              sized ? static_cast<size_t>(headers.length) : capacity, sized,
                              received);
  client.stop();
  if (!whole) {
    return {status, 0};
  }
  Serial.printf("download: received %u bytes\n", static_cast<unsigned>(received));
  return {status, received};
}

}  // namespace

Response request(const char* url, const char* postBody, uint8_t* buffer, size_t capacity) {
  char next[kMaxLineChars];
  strlcpy(next, url, sizeof(next));
  for (int hop = 0; hop <= kMaxRedirects; ++hop) {
    Headers headers;
    const Response response = fetchOnce(next, postBody, buffer, capacity, headers);
    if (postBody != nullptr || !isRedirect(response.status)) {
      return response;
    }
    if (headers.location[0] == 0) {
      Serial.println("download: redirect without a location");
      return response;
    }
    strlcpy(next, headers.location, sizeof(next));
  }
  Serial.println("download: too many redirects");
  return {0, 0};
}
