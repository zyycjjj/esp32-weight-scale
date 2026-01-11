#include "app/wifi_manager.h"

#include <WiFi.h>

namespace aiw {

void WifiManager::begin() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
}

bool WifiManager::connect(const char *ssid, const char *password, uint32_t timeoutMs) {
  if (!ssid || !ssid[0]) return false;
  WiFi.disconnect(true, true);
  delay(100);
  WiFi.begin(ssid, password);

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > timeoutMs) return false;
    delay(200);
  }
  return true;
}

void WifiManager::loop() {
  if (WiFi.status() == WL_CONNECTED) return;
}

bool WifiManager::isConnected() const {
  return WiFi.status() == WL_CONNECTED;
}

String WifiManager::ip() const {
  if (!isConnected()) return "";
  return WiFi.localIP().toString();
}

}  // namespace aiw

