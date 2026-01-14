#include "app/gacha_controller.h"

namespace aiw {

void GachaController::begin(int pin, bool activeHigh, uint32_t pulseMs) {
  if (timer_ != nullptr) {
    xTimerDelete(timer_, 0);
    timer_ = nullptr;
  }

  pin_ = pin;
  activeHigh_ = activeHigh;
  pulseMs_ = pulseMs;
  active_ = false;
  startMs_ = 0;

  if (pin_ < 0) return;
  pinMode(pin_, OUTPUT);
  digitalWrite(pin_, activeHigh_ ? LOW : HIGH);

  if (pulseMs_ > 0) {
    timer_ = xTimerCreate("gacha", pdMS_TO_TICKS(pulseMs_), pdFALSE, this, timerCallback);
  }
}

void GachaController::trigger() {
  if (pin_ < 0) return;
  if (pulseMs_ == 0) return;
  active_ = true;
  startMs_ = millis();
  digitalWrite(pin_, activeHigh_ ? HIGH : LOW);

  if (timer_ != nullptr) {
    xTimerStop(timer_, 0);
    xTimerChangePeriod(timer_, pdMS_TO_TICKS(pulseMs_), 0);
    xTimerStart(timer_, 0);
  }
}

void GachaController::loop() {
  if (!active_) return;
  uint32_t now = millis();
  if (now - startMs_ < pulseMs_) return;
  endPulse();
}

bool GachaController::isActive() const {
  return active_;
}

void GachaController::endPulse() {
  if (!active_) return;
  active_ = false;
  digitalWrite(pin_, activeHigh_ ? LOW : HIGH);
}

void GachaController::timerCallback(TimerHandle_t t) {
  auto *self = static_cast<GachaController *>(pvTimerGetTimerID(t));
  if (self) self->endPulse();
}

}  // namespace aiw
