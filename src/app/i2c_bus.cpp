#include "app/i2c_bus.h"

#include <Wire.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace aiw {

static SemaphoreHandle_t g_mutex = nullptr;
static bool g_inited = false;
static int g_sda = -1;
static int g_scl = -1;
static uint32_t g_freq = 100000;

static void ensureMutex() {
  if (g_mutex) return;
  g_mutex = xSemaphoreCreateMutex();
}

void i2cBusInit(int sda, int scl, uint32_t freqHz) {
  ensureMutex();
  uint32_t freq = freqHz ? freqHz : 100000;
  if (!g_inited) {
    g_sda = sda;
    g_scl = scl;
    g_freq = freq;
    Wire.begin(g_sda, g_scl);
    Wire.setClock(g_freq);
    Wire.setTimeOut(20);
    g_inited = true;
    return;
  }
  if (sda >= 0 && scl >= 0 && (sda != g_sda || scl != g_scl)) {
    g_sda = sda;
    g_scl = scl;
    Wire.begin(g_sda, g_scl);
  }
  if (freq != g_freq) {
    g_freq = freq;
    Wire.setClock(g_freq);
  }
  Wire.setTimeOut(20);
}

bool i2cBusLock(uint32_t timeoutMs) {
  ensureMutex();
  if (!g_mutex) return false;
  TickType_t ticks = timeoutMs ? pdMS_TO_TICKS(timeoutMs) : 0;
  return xSemaphoreTake(g_mutex, ticks) == pdTRUE;
}

void i2cBusUnlock() {
  if (!g_mutex) return;
  xSemaphoreGive(g_mutex);
}

}  // namespace aiw
