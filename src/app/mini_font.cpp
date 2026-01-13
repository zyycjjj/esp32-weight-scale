#include "app/mini_font.h"

namespace aiw {

static bool glyph5x7(char c, uint8_t out[7]) {
  if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
  switch (c) {
    case ' ': {
      for (int i = 0; i < 7; ++i) out[i] = 0;
      return true;
    }
    case '-': {
      for (int i = 0; i < 7; ++i) out[i] = 0;
      out[3] = 0b11111;
      return true;
    }
    case ':': {
      for (int i = 0; i < 7; ++i) out[i] = 0;
      out[2] = 0b00100;
      out[4] = 0b00100;
      return true;
    }
    case '.': {
      for (int i = 0; i < 7; ++i) out[i] = 0;
      out[6] = 0b00100;
      return true;
    }
    case '/': {
      out[0] = 0b00001;
      out[1] = 0b00010;
      out[2] = 0b00100;
      out[3] = 0b01000;
      out[4] = 0b10000;
      out[5] = 0b00000;
      out[6] = 0b00000;
      return true;
    }
    case '<': {
      out[0] = 0b00010;
      out[1] = 0b00100;
      out[2] = 0b01000;
      out[3] = 0b01000;
      out[4] = 0b01000;
      out[5] = 0b00100;
      out[6] = 0b00010;
      return true;
    }
    case '>': {
      out[0] = 0b01000;
      out[1] = 0b00100;
      out[2] = 0b00010;
      out[3] = 0b00010;
      out[4] = 0b00010;
      out[5] = 0b00100;
      out[6] = 0b01000;
      return true;
    }
    case '0': { uint8_t g[7] = {0b01110,0b10001,0b10011,0b10101,0b11001,0b10001,0b01110}; memcpy(out,g,7); return true; }
    case '1': { uint8_t g[7] = {0b00100,0b01100,0b00100,0b00100,0b00100,0b00100,0b01110}; memcpy(out,g,7); return true; }
    case '2': { uint8_t g[7] = {0b01110,0b10001,0b00001,0b00010,0b00100,0b01000,0b11111}; memcpy(out,g,7); return true; }
    case '3': { uint8_t g[7] = {0b01110,0b10001,0b00001,0b00110,0b00001,0b10001,0b01110}; memcpy(out,g,7); return true; }
    case '4': { uint8_t g[7] = {0b00010,0b00110,0b01010,0b10010,0b11111,0b00010,0b00010}; memcpy(out,g,7); return true; }
    case '5': { uint8_t g[7] = {0b11111,0b10000,0b11110,0b00001,0b00001,0b10001,0b01110}; memcpy(out,g,7); return true; }
    case '6': { uint8_t g[7] = {0b00110,0b01000,0b10000,0b11110,0b10001,0b10001,0b01110}; memcpy(out,g,7); return true; }
    case '7': { uint8_t g[7] = {0b11111,0b00001,0b00010,0b00100,0b01000,0b01000,0b01000}; memcpy(out,g,7); return true; }
    case '8': { uint8_t g[7] = {0b01110,0b10001,0b10001,0b01110,0b10001,0b10001,0b01110}; memcpy(out,g,7); return true; }
    case '9': { uint8_t g[7] = {0b01110,0b10001,0b10001,0b01111,0b00001,0b00010,0b01100}; memcpy(out,g,7); return true; }
    case 'A': { uint8_t g[7] = {0b01110,0b10001,0b10001,0b11111,0b10001,0b10001,0b10001}; memcpy(out,g,7); return true; }
    case 'B': { uint8_t g[7] = {0b11110,0b10001,0b10001,0b11110,0b10001,0b10001,0b11110}; memcpy(out,g,7); return true; }
    case 'C': { uint8_t g[7] = {0b01110,0b10001,0b10000,0b10000,0b10000,0b10001,0b01110}; memcpy(out,g,7); return true; }
    case 'D': { uint8_t g[7] = {0b11110,0b10001,0b10001,0b10001,0b10001,0b10001,0b11110}; memcpy(out,g,7); return true; }
    case 'E': { uint8_t g[7] = {0b11111,0b10000,0b10000,0b11110,0b10000,0b10000,0b11111}; memcpy(out,g,7); return true; }
    case 'F': { uint8_t g[7] = {0b11111,0b10000,0b10000,0b11110,0b10000,0b10000,0b10000}; memcpy(out,g,7); return true; }
    case 'G': { uint8_t g[7] = {0b01110,0b10001,0b10000,0b10111,0b10001,0b10001,0b01110}; memcpy(out,g,7); return true; }
    case 'H': { uint8_t g[7] = {0b10001,0b10001,0b10001,0b11111,0b10001,0b10001,0b10001}; memcpy(out,g,7); return true; }
    case 'I': { uint8_t g[7] = {0b01110,0b00100,0b00100,0b00100,0b00100,0b00100,0b01110}; memcpy(out,g,7); return true; }
    case 'K': { uint8_t g[7] = {0b10001,0b10010,0b10100,0b11000,0b10100,0b10010,0b10001}; memcpy(out,g,7); return true; }
    case 'L': { uint8_t g[7] = {0b10000,0b10000,0b10000,0b10000,0b10000,0b10000,0b11111}; memcpy(out,g,7); return true; }
    case 'N': { uint8_t g[7] = {0b10001,0b11001,0b10101,0b10011,0b10001,0b10001,0b10001}; memcpy(out,g,7); return true; }
    case 'O': { uint8_t g[7] = {0b01110,0b10001,0b10001,0b10001,0b10001,0b10001,0b01110}; memcpy(out,g,7); return true; }
    case 'P': { uint8_t g[7] = {0b11110,0b10001,0b10001,0b11110,0b10000,0b10000,0b10000}; memcpy(out,g,7); return true; }
    case 'R': { uint8_t g[7] = {0b11110,0b10001,0b10001,0b11110,0b10100,0b10010,0b10001}; memcpy(out,g,7); return true; }
    case 'S': { uint8_t g[7] = {0b01111,0b10000,0b10000,0b01110,0b00001,0b00001,0b11110}; memcpy(out,g,7); return true; }
    case 'T': { uint8_t g[7] = {0b11111,0b00100,0b00100,0b00100,0b00100,0b00100,0b00100}; memcpy(out,g,7); return true; }
    case 'U': { uint8_t g[7] = {0b10001,0b10001,0b10001,0b10001,0b10001,0b10001,0b01110}; memcpy(out,g,7); return true; }
    case 'V': { uint8_t g[7] = {0b10001,0b10001,0b10001,0b10001,0b10001,0b01010,0b00100}; memcpy(out,g,7); return true; }
    case 'W': { uint8_t g[7] = {0b10001,0b10001,0b10001,0b10101,0b10101,0b10101,0b01010}; memcpy(out,g,7); return true; }
    case 'X': { uint8_t g[7] = {0b10001,0b10001,0b01010,0b00100,0b01010,0b10001,0b10001}; memcpy(out,g,7); return true; }
    case 'Y': { uint8_t g[7] = {0b10001,0b10001,0b01010,0b00100,0b00100,0b00100,0b00100}; memcpy(out,g,7); return true; }
    default:
      return false;
  }
}

void drawText5x7(DisplaySt7789 &display, int x, int y, const char *text, uint16_t fg, uint16_t bg, int scale) {
  if (!text) return;
  if (scale < 1) scale = 1;
  int cx = x;
  uint8_t rows[7];
  while (*text) {
    char c = *text++;
    if (!glyph5x7(c, rows)) {
      for (int i = 0; i < 7; ++i) rows[i] = 0;
    }
    for (int r = 0; r < 7; ++r) {
      for (int col = 0; col < 5; ++col) {
        bool on = (rows[r] & (1u << (4 - col))) != 0;
        display.fillRect(cx + col * scale, y + r * scale, scale, scale, on ? fg : bg);
      }
    }
    cx += (6 * scale);
  }
}

}
