#include "app/touch_button.h"

namespace aiw {

void TouchButton::begin(int digitalPin, int touchPin, uint16_t threshold) {
  digitalPin_ = digitalPin;
  touchPin_ = touchPin;
  threshold_ = threshold;
  prevPressed_ = false;
  pressStartMs_ = 0;
  lastTouch_ = 0;
  if (digitalPin_ >= 0) {
    pinMode(digitalPin_, INPUT_PULLUP);
  }
  if (touchPin_ >= 0 && threshold_ == 0) {
    uint32_t sum = 0;
    const int n = 20;
    for (int i = 0; i < n; ++i) {
      sum += (uint32_t)touchRead(touchPin_);
      delay(8);
    }
    uint16_t base = (uint16_t)(sum / (uint32_t)n);
    uint16_t margin = (uint16_t)(base / 8u);
    if (margin < 200) margin = 200;
    if (margin > 8000) margin = 8000;
    threshold_ = base > margin ? (uint16_t)(base - margin) : (uint16_t)1;
  }
}

static bool readPressed(int digitalPin, int touchPin, uint16_t threshold, bool prevPressed, uint16_t &touchValue) {
  bool pressed = false;
  if (digitalPin >= 0) {
    pressed = pressed || (digitalRead(digitalPin) == LOW);
  }
  if (touchPin >= 0) {
    touchValue = (uint16_t)touchRead(touchPin);
    if (threshold > 0) {
      uint16_t hysteresis = (uint16_t)(threshold / 20u);
      if (hysteresis < 30) hysteresis = 30;
      if (hysteresis > 1500) hysteresis = 1500;
      uint16_t thDown = threshold;
      uint16_t thUp = (uint16_t)(threshold + hysteresis);
      if (!prevPressed) {
        pressed = pressed || (touchValue < thDown);
      } else {
        pressed = pressed || (touchValue < thUp);
      }
    }
  }
  return pressed;
}

void TouchButton::update(bool &shortPress, bool &longPress) {
  shortPress = false;
  longPress = false;

  uint32_t now = millis();
  bool pressed = readPressed(digitalPin_, touchPin_, threshold_, prevPressed_, lastTouch_);

  if (pressed && !prevPressed_) {
    pressStartMs_ = now;
  }

  if (!pressed && prevPressed_) {
    uint32_t dur = now - pressStartMs_;
    if (dur >= 800) {
      longPress = true;
    } else if (dur >= 40) {
      shortPress = true;
    }
  }

  prevPressed_ = pressed;
}

uint16_t TouchButton::lastTouchValue() const {
  return lastTouch_;
}

}  // namespace aiw
