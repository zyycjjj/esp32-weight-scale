#pragma once

#include <Arduino.h>
#include "app/display_st7789.h"

namespace aiw {

class SevenSeg {
public:
  explicit SevenSeg(DisplaySt7789 &display);
  void drawText(int x, int y, const char *text, int scale, uint16_t on, uint16_t off);
  void clearRect(int x, int y, int w, int h, uint16_t bg);

private:
  void drawChar(int x, int y, char ch, int scale, uint16_t on, uint16_t off);
  DisplaySt7789 &display_;
};

}  // namespace aiw

