#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace aiw {

class GachaController;

class AudioPlayer {
 public:
  void begin(bool enabled, int bclkPin, int lrckPin, int doutPin, int mclkPin, int paCtrlPin, int i2cSdaPin, int i2cSclPin, int codecI2cAddr, int volume);
  bool playWavAsync(const char *baseUrl, const String &audioUrlOrPath);
  bool playWav(const char *baseUrl, const String &audioUrlOrPath, GachaController *gacha);
  bool playBeep(int freqHz, int ms);
  bool isPlaying() const;
  void stop();

 private:
  friend void audioTask(void *pv);
  bool enabled_ = false;
  int bclkPin_ = -1;
  int lrckPin_ = -1;
  int doutPin_ = -1;
  int mclkPin_ = -1;
  int paCtrlPin_ = -1;
  int i2cSdaPin_ = -1;
  int i2cSclPin_ = -1;
  int codecI2cAddr_ = 0x18;
  int volume_ = 12;
  TaskHandle_t task_{nullptr};
  volatile bool playing_{false};
  bool codecReady_ = false;
};

}  // namespace aiw
