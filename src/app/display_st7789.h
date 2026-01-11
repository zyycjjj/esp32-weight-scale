#pragma once

#include <Arduino.h>
#include <SPI.h>

namespace aiw {

struct DisplayPins {
  int mosi;
  int sclk;
  int cs;
  int dc;
  int rst;
  int blBox;
  int blBox3;
};

class DisplaySt7789 {
public:
  static constexpr int Width = 320;
  static constexpr int Height = 240;

  explicit DisplaySt7789(DisplayPins pins);

  void begin();
  void clear(uint16_t color565);
  void fillRect(int x, int y, int w, int h, uint16_t color565);
  void drawBorder(uint16_t color565, int thickness);

  void beginWrite();
  void endWrite();

private:
  void cmd(uint8_t c);
  void data8(uint8_t d);
  void dataBuf(const uint8_t *buf, size_t len);
  void setAddr(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
  void writeColor(uint16_t color565, size_t pixelCount);
  void resetActiveHigh();
  void initN_C0();

  DisplayPins pins_;
  bool writing_{false};
};

}  // namespace aiw

