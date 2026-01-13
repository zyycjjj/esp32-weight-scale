#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace aiw {

class TouchButton {
 public:
  void begin(int digitalPin, int touchPin, uint16_t threshold);
  void update(bool &shortPress, bool &longPress);
  uint16_t lastTouchValue() const;

 private:
  int digitalPin_{-1};
  int touchPin_{-1};
  uint16_t threshold_{0};

  bool prevPressed_{false};
  uint32_t pressStartMs_{0};
  uint16_t lastTouch_{0};
};

}  // namespace aiw
