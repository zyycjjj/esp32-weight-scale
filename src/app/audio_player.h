#pragma once

#include <Arduino.h>

namespace aiw {

class GachaController;

class AudioPlayer {
 public:
  void begin(bool enabled, int bclkPin, int lrckPin, int doutPin, int volume);
  bool playWav(const char *baseUrl, const String &audioUrlOrPath, GachaController *gacha);

 private:
  bool enabled_ = false;
  int bclkPin_ = -1;
  int lrckPin_ = -1;
  int doutPin_ = -1;
  int volume_ = 12;
};

}  // namespace aiw
