#pragma once

#include <Arduino.h>

namespace aiw {

class WifiManager {
public:
  void begin();
  bool connect(const char *ssid, const char *password, uint32_t timeoutMs);
  void loop();
  bool isConnected() const;
  String ip() const;
};

}  // namespace aiw

