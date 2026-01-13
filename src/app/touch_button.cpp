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
}

static bool readPressed(int digitalPin, int touchPin, uint16_t threshold, uint16_t &touchValue) {
  bool pressed = false;
  if (digitalPin >= 0) {
    pressed = pressed || (digitalRead(digitalPin) == LOW);
  }
  if (touchPin >= 0) {
    touchValue = (uint16_t)touchRead(touchPin);
    if (threshold > 0) {
      pressed = pressed || (touchValue > threshold);
    }
  }
  return pressed;
}

void TouchButton::update(bool &shortPress, bool &longPress) {
  shortPress = false;
  longPress = false;

  uint32_t now = millis();
  bool pressed = readPressed(digitalPin_, touchPin_, threshold_, lastTouch_);

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
