#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace aiw {

class GachaController {
 public:
  void begin(int pin, bool activeHigh, uint32_t pulseMs);
  void trigger();
  void loop();
  bool isActive() const;

 private:
  int pin_ = -1;
  bool activeHigh_ = true;
  uint32_t pulseMs_ = 0;
  bool active_ = false;
  uint32_t startMs_ = 0;
};

}  // namespace aiw
