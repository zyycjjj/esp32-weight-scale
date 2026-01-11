#pragma once

#include <Arduino.h>

namespace aiw {

struct Hx711Pins {
  int dout;
  int sck;
};

class Hx711 {
public:
  explicit Hx711(Hx711Pins pins);

  void begin();
  int32_t readRaw(uint32_t timeoutMs);
  int32_t readAverage(int samples, uint32_t timeoutMs);
  void tare(int samples, uint32_t timeoutMs);

  void setScale(float scale);
  float scale() const;
  int32_t offset() const;

  bool readWeight(float &weight);

private:
  bool isReady() const;

  Hx711Pins pins_;
  int32_t offset_{0};
  float scale_{1000.0f};
};

}  // namespace aiw

