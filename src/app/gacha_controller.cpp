#include "app/gacha_controller.h"

namespace aiw {

void GachaController::begin(int pin, bool activeHigh, uint32_t pulseMs) {
  pin_ = pin;
  activeHigh_ = activeHigh;
  pulseMs_ = pulseMs;
  active_ = false;
  startMs_ = 0;
  if (pin_ < 0) return;
  pinMode(pin_, OUTPUT);
  digitalWrite(pin_, activeHigh_ ? LOW : HIGH);
}

void GachaController::trigger() {
  if (pin_ < 0) return;
  if (pulseMs_ == 0) return;
  if (active_) return;
  active_ = true;
  startMs_ = millis();
  digitalWrite(pin_, activeHigh_ ? HIGH : LOW);
}

void GachaController::loop() {
  if (!active_) return;
  uint32_t now = millis();
  if (now - startMs_ < pulseMs_) return;
  active_ = false;
  digitalWrite(pin_, activeHigh_ ? LOW : HIGH);
}

bool GachaController::isActive() const {
  return active_;
}

}  // namespace aiw

