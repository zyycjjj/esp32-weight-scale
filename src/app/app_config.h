#pragma once

#include <stdint.h>

#ifndef AIW_WIFI_SSID
#define AIW_WIFI_SSID ""
#endif

#ifndef AIW_WIFI_PASSWORD
#define AIW_WIFI_PASSWORD ""
#endif

#ifndef AIW_BACKEND_BASE_URL
#define AIW_BACKEND_BASE_URL "https://ai.youtirj.com"
#endif

#ifndef AIW_DEVICE_ID
#define AIW_DEVICE_ID "espbox-001"
#endif

#ifndef AIW_DEVICE_NAME
#define AIW_DEVICE_NAME "ESP-BOX"
#endif

#ifndef AIW_HX711_DOUT_PIN
#define AIW_HX711_DOUT_PIN 1
#endif

#ifndef AIW_HX711_SCK_PIN
#define AIW_HX711_SCK_PIN 2
#endif

#ifndef AIW_HX711_SCALE
#define AIW_HX711_SCALE 1000.0f
#endif

#ifndef AIW_PRINTER_TX_PIN
#define AIW_PRINTER_TX_PIN 41
#endif

#ifndef AIW_PRINTER_RX_PIN
#define AIW_PRINTER_RX_PIN 42
#endif

#ifndef AIW_PRINTER_BAUD
#define AIW_PRINTER_BAUD 9600
#endif

#ifndef AIW_GACHA_PIN
#define AIW_GACHA_PIN -1
#endif

#ifndef AIW_GACHA_ACTIVE_HIGH
#define AIW_GACHA_ACTIVE_HIGH 1
#endif

#ifndef AIW_GACHA_PULSE_MS
#define AIW_GACHA_PULSE_MS 1200
#endif

#ifndef AIW_AUDIO_ENABLED
#define AIW_AUDIO_ENABLED 0
#endif

#ifndef AIW_I2S_BCLK_PIN
#define AIW_I2S_BCLK_PIN -1
#endif

#ifndef AIW_I2S_LRCK_PIN
#define AIW_I2S_LRCK_PIN -1
#endif

#ifndef AIW_I2S_DOUT_PIN
#define AIW_I2S_DOUT_PIN -1
#endif

#ifndef AIW_AUDIO_VOLUME
#define AIW_AUDIO_VOLUME 12
#endif

#ifndef AIW_TOUCH_PIN
#define AIW_TOUCH_PIN -1
#endif

#ifndef AIW_TOUCH_THRESHOLD
#define AIW_TOUCH_THRESHOLD 0
#endif

namespace aiw::config {
static const char *WifiSsid = AIW_WIFI_SSID;
static const char *WifiPassword = AIW_WIFI_PASSWORD;
static const char *BackendBaseUrl = AIW_BACKEND_BASE_URL;
static const char *DeviceId = AIW_DEVICE_ID;
static const char *DeviceName = AIW_DEVICE_NAME;
static const int Hx711DoutPin = AIW_HX711_DOUT_PIN;
static const int Hx711SckPin = AIW_HX711_SCK_PIN;
static constexpr float Hx711Scale = AIW_HX711_SCALE;
static const int PrinterTxPin = AIW_PRINTER_TX_PIN;
static const int PrinterRxPin = AIW_PRINTER_RX_PIN;
static const int PrinterBaud = AIW_PRINTER_BAUD;
static const int GachaPin = AIW_GACHA_PIN;
static const bool GachaActiveHigh = (AIW_GACHA_ACTIVE_HIGH != 0);
static const uint32_t GachaPulseMs = (uint32_t)AIW_GACHA_PULSE_MS;
static const bool AudioEnabled = (AIW_AUDIO_ENABLED != 0);
static const int I2sBclkPin = AIW_I2S_BCLK_PIN;
static const int I2sLrckPin = AIW_I2S_LRCK_PIN;
static const int I2sDoutPin = AIW_I2S_DOUT_PIN;
static const int AudioVolume = AIW_AUDIO_VOLUME;
static const int TouchPin = AIW_TOUCH_PIN;
static const uint16_t TouchThreshold = (uint16_t)AIW_TOUCH_THRESHOLD;
}
