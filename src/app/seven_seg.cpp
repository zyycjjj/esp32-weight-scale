#include "app/seven_seg.h"

namespace aiw {

SevenSeg::SevenSeg(DisplaySt7789 &display) : display_(display) {}

void SevenSeg::clearRect(int x, int y, int w, int h, uint16_t bg) {
  display_.fillRect(x, y, w, h, bg);
}

static void segRect(DisplaySt7789 &display, int x, int y, int w, int h, uint16_t color) {
  display.fillRect(x, y, w, h, color);
}

void SevenSeg::drawChar(int x, int y, char ch, int scale, uint16_t on, uint16_t off) {
  if (scale < 1) scale = 1;
  const int t = scale;
  const int w = 6 * scale;
  const int h = 10 * scale;

  segRect(display_, x, y, w, h, off);

  if (ch == '.') {
    segRect(display_, x + w - t - 1, y + h - t - 1, t, t, on);
    return;
  }

  uint8_t segs = 0;
  if (ch >= '0' && ch <= '9') {
    static const uint8_t map[10] = {
      0b00111111, 0b00000110, 0b01011011, 0b01001111, 0b01100110,
      0b01101101, 0b01111101, 0b00000111, 0b01111111, 0b01101111
    };
    segs = map[ch - '0'];
  } else if (ch == '-') {
    segs = 0b01000000;
  } else {
    return;
  }

  if (segs & 0b00000001) segRect(display_, x + t, y, w - 2 * t, t, on);
  if (segs & 0b00000010) segRect(display_, x + w - t, y + t, t, (h / 2) - t, on);
  if (segs & 0b00000100) segRect(display_, x + w - t, y + (h / 2), t, (h / 2) - t, on);
  if (segs & 0b00001000) segRect(display_, x + t, y + h - t, w - 2 * t, t, on);
  if (segs & 0b00010000) segRect(display_, x, y + (h / 2), t, (h / 2) - t, on);
  if (segs & 0b00100000) segRect(display_, x, y + t, t, (h / 2) - t, on);
  if (segs & 0b01000000) segRect(display_, x + t, y + (h / 2) - (t / 2), w - 2 * t, t, on);
}

void SevenSeg::drawText(int x, int y, const char *text, int scale, uint16_t on, uint16_t off) {
  int cx = x;
  while (*text) {
    drawChar(cx, y, *text, scale, on, off);
    cx += 7 * scale;
    ++text;
  }
}

}  // namespace aiw

