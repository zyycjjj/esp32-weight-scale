#include "app/hx711.h"

namespace aiw {

Hx711::Hx711(Hx711Pins pins) : pins_(pins) {}

void Hx711::begin() {
  pinMode(pins_.dout, INPUT_PULLUP);
  pinMode(pins_.sck, OUTPUT);
  digitalWrite(pins_.sck, LOW);
}

bool Hx711::isReady() const {
  return digitalRead(pins_.dout) == LOW;
}

int32_t Hx711::readRaw(uint32_t timeoutMs) {
  uint32_t start = millis();
  while (!isReady()) {
    if (millis() - start > timeoutMs) return INT32_MIN;
    delay(1);
  }

  uint32_t value = 0;
  for (int i = 0; i < 24; ++i) {
    digitalWrite(pins_.sck, HIGH);
    delayMicroseconds(1);
    value = (value << 1) | (uint32_t)digitalRead(pins_.dout);
    digitalWrite(pins_.sck, LOW);
    delayMicroseconds(1);
  }

  digitalWrite(pins_.sck, HIGH);
  delayMicroseconds(1);
  digitalWrite(pins_.sck, LOW);
  delayMicroseconds(1);

  if (value & 0x800000) value |= 0xFF000000;
  return (int32_t)value;
}

int32_t Hx711::readAverage(int samples, uint32_t timeoutMs) {
  if (samples < 1) samples = 1;
  int64_t sum = 0;
  int got = 0;
  for (int i = 0; i < samples; ++i) {
    int32_t v = readRaw(timeoutMs);
    if (v == INT32_MIN) continue;
    sum += v;
    ++got;
  }
  if (got == 0) return INT32_MIN;
  return (int32_t)(sum / got);
}

void Hx711::tare(int samples, uint32_t timeoutMs) {
  int32_t v = readAverage(samples, timeoutMs);
  if (v != INT32_MIN) offset_ = v;
}

void Hx711::setScale(float scale) {
  if (scale <= 0.0f) return;
  scale_ = scale;
}

float Hx711::scale() const {
  return scale_;
}

int32_t Hx711::offset() const {
  return offset_;
}

bool Hx711::readWeight(float &weight) {
  int32_t raw = readAverage(5, 200);
  if (raw == INT32_MIN) return false;
  int32_t delta = raw - offset_;
  weight = (float)delta / scale_;
  return true;
}

}  // namespace aiw
