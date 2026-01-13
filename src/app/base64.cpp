#include "app/base64.h"

namespace aiw {

size_t base64DecodedMaxLen(size_t b64Len) {
  return (b64Len / 4) * 3 + 3;
}

static int b64Val(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  if (c == '=') return -2;
  return -1;
}

bool base64DecodeToBytes(const String &b64, uint8_t *out, size_t outCap, size_t &outLen) {
  outLen = 0;
  int vals[4];
  int vcount = 0;

  for (size_t i = 0; i < b64.length(); ++i) {
    char c = b64[i];
    if (c == '\r' || c == '\n' || c == ' ' || c == '\t') continue;
    int v = b64Val(c);
    if (v < 0 && v != -2) return false;
    vals[vcount++] = v;
    if (vcount == 4) {
      int a = vals[0];
      int b = vals[1];
      int c2 = vals[2];
      int d = vals[3];
      if (a < 0 || b < 0) return false;

      uint8_t o0 = (uint8_t)((a << 2) | (b >> 4));
      if (outLen + 1 > outCap) return false;
      out[outLen++] = o0;

      if (c2 == -2) {
        vcount = 0;
        break;
      }
      if (c2 < 0) return false;
      uint8_t o1 = (uint8_t)(((b & 0x0F) << 4) | (c2 >> 2));
      if (outLen + 1 > outCap) return false;
      out[outLen++] = o1;

      if (d == -2) {
        vcount = 0;
        break;
      }
      if (d < 0) return false;
      uint8_t o2 = (uint8_t)(((c2 & 0x03) << 6) | d);
      if (outLen + 1 > outCap) return false;
      out[outLen++] = o2;

      vcount = 0;
    }
  }
  return true;
}

}  // namespace aiw

