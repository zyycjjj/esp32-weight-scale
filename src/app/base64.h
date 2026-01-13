#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>

namespace aiw {

bool base64DecodeToBytes(const String &b64, uint8_t *out, size_t outCap, size_t &outLen);
size_t base64DecodedMaxLen(size_t b64Len);

}  // namespace aiw
