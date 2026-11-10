#pragma once

#include <stddef.h>
#include <stdint.h>

bool decodeInto(const uint8_t* jpeg, size_t length, uint16_t* canvas);
