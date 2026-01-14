#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>

namespace aiw {

class GachaController {
 public:
  void begin(int pin, bool activeHigh, uint32_t pulseMs);
  void trigger();
  void loop();
  bool isActive() const;

 private:
  void endPulse();
  static void timerCallback(TimerHandle_t t);

  int pin_ = -1;
  bool activeHigh_ = true;
  uint32_t pulseMs_ = 0;
  bool active_ = false;
  uint32_t startMs_ = 0;
  TimerHandle_t timer_ = nullptr;
};

}  // namespace aiw
