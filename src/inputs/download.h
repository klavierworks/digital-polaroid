#pragma once

#include <stddef.h>
#include <stdint.h>

struct Response {
  int status;
  size_t length;
};

Response request(const char* url, const char* postBody, uint8_t* buffer, size_t capacity);
