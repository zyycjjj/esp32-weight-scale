#pragma once

#include <Arduino.h>

namespace aiw {

struct TouchPoint {
  bool touching = false;
  int x = 0;
  int y = 0;
};

class TouchGt911 {
 public:
  void begin(int sdaPin, int sclPin, uint8_t addr = 0);
  bool detect();
  bool read(TouchPoint &out);
  uint16_t maxX() const { return maxX_; }
  uint16_t maxY() const { return maxY_; }

 private:
  bool readReg16(uint16_t reg, uint8_t *buf, size_t len);
  bool writeReg16(uint16_t reg, const uint8_t *buf, size_t len);
  bool tryDetect();

  int sdaPin_ = -1;
  int sclPin_ = -1;
  uint8_t addr_ = 0;
  bool i2cReady_ = false;
  bool detected_ = false;
  uint16_t maxX_ = 0;
  uint16_t maxY_ = 0;
};

}
