#include <Arduino.h>
#include <HardwareSerial.h>
#include <string.h>
#include <math.h>

#include "app/app_config.h"
#include "app/display_st7789.h"
#include "app/hx711.h"
#include "app/payment_client.h"
#include "app/qr_client.h"
#include "app/qr_renderer.h"
#include "app/seven_seg.h"
#include "app/wifi_manager.h"

static aiw::DisplaySt7789 display({.mosi = 6, .sclk = 7, .cs = 5, .dc = 4, .rst = 48, .blBox = 45, .blBox3 = 47});
static aiw::SevenSeg sevenSeg(display);
static aiw::QrRenderer qrRenderer(display);
static aiw::WifiManager wifi;
static aiw::Hx711 hx711A({.dout = aiw::config::Hx711DoutPin, .sck = aiw::config::Hx711SckPin});
static aiw::Hx711 hx711B({.dout = aiw::config::Hx711SckPin, .sck = aiw::config::Hx711DoutPin});
static aiw::Hx711 *hx711 = &hx711A;
static aiw::PaymentClient payment(aiw::config::BackendBaseUrl);
static aiw::QrClient qrClient(aiw::config::BackendBaseUrl);
static HardwareSerial printerSerial(1);
static int printerTxPin = aiw::config::PrinterTxPin;
static int printerRxPin = aiw::config::PrinterRxPin;
static int printerBaud = aiw::config::PrinterBaud;

static constexpr uint16_t ColorWhite = 0xFFFF;
static constexpr uint16_t ColorBlack = 0x0000;
static constexpr uint16_t ColorRed = 0xF800;
static constexpr uint16_t ColorBlue = 0x001F;
static constexpr uint16_t ColorGreen = 0x07E0;

static constexpr int WeightX = 10;
static constexpr int WeightY = 10;
static constexpr int WeightW = 300;
static constexpr int WeightH = 50;

static constexpr int WifiDotX = 300;
static constexpr int WifiDotY = 0;
static constexpr int WifiDotSize = 20;

static constexpr int QrX = 10;
static constexpr int QrY = 70;
static constexpr int QrSize = 220;

static constexpr int StableWindow = 6;
static constexpr int32_t ZeroSnapDelta = 200;
static constexpr int32_t PayTriggerDelta = 800;
static constexpr float DisplayStep = 0.1f;
static constexpr float DisplayHysteresis = 0.15f;
static constexpr float StableUnlockDelta = 0.3f;
static int32_t deltaWindow[StableWindow];
static int weightWindowCount = 0;
static int weightWindowIndex = 0;
static int stableHits = 0;
static bool hasLastDelta = false;
static int32_t lastDelta = 0;
static uint32_t glitchCount = 0;
static bool filteredInit = false;
static float filteredDelta = 0.0f;
static bool hasLastFilteredDelta = false;
static int32_t lastFilteredDelta = 0;
static bool displayLocked = false;
static float lockedWeight = 0.0f;
static bool hasLastShownWeight = false;
static float lastShownWeight = 0.0f;

static void resetDeltaWindow() {
  weightWindowCount = 0;
  weightWindowIndex = 0;
  stableHits = 0;
  displayLocked = false;
  hasLastShownWeight = false;
}

static float quantize(float v, float step) {
  if (step <= 0.0f) return v;
  return roundf(v / step) * step;
}

enum class AppState : uint8_t {
  Weighing = 0,
  CreatingPayment = 1,
  FetchingQr = 2,
  WaitingPayment = 3,
  Paid = 4,
};

static AppState state = AppState::Weighing;
static float lastStableWeight = 0.0f;
static aiw::PaymentCreateResponse payCreateRes;
static aiw::QrMatrix qrMatrix;
static uint32_t lastPollMs = 0;
static uint32_t lastHx711LogMs = 0;
static uint32_t lastTareMs = 0;

static void pushDelta(int32_t d) {
  deltaWindow[weightWindowIndex] = d;
  weightWindowIndex = (weightWindowIndex + 1) % StableWindow;
  if (weightWindowCount < StableWindow) weightWindowCount++;
}

static bool computeStableDelta(int32_t &meanOut, int32_t &rangeOut) {
  const int n = weightWindowCount < StableWindow ? weightWindowCount : StableWindow;
  if (n < 4) return false;
  int32_t minV = deltaWindow[0];
  int32_t maxV = deltaWindow[0];
  int64_t sum = 0;
  for (int i = 0; i < n; ++i) {
    int32_t v = deltaWindow[i];
    if (v < minV) minV = v;
    if (v > maxV) maxV = v;
    sum += v;
  }
  meanOut = (int32_t)(sum / (int64_t)n);
  rangeOut = (int32_t)(maxV - minV);
  int32_t absMean = meanOut < 0 ? -meanOut : meanOut;
  int32_t threshold = absMean / 40;
  if (threshold < 160) threshold = 160;
  if (threshold > 900) threshold = 900;
  if (rangeOut > threshold) return false;
  if (absMean < 30) return false;
  return true;
}

static void drawUiFrame() {
  display.beginWrite();
  display.clear(ColorWhite);
  display.fillRect(0, 70, aiw::DisplaySt7789::Width, 60, ColorRed);
  display.fillRect(0, 130, aiw::DisplaySt7789::Width, 60, ColorBlue);
  display.drawBorder(ColorWhite, 2);
  display.endWrite();
}

static void drawWifiStatus() {
  display.beginWrite();
  uint16_t c = wifi.isConnected() ? ColorGreen : ColorRed;
  display.fillRect(WifiDotX, WifiDotY, WifiDotSize, WifiDotSize, c);
  display.endWrite();
}

static void drawWeight(bool stable, float weight) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%.1f", weight);
  display.beginWrite();
  sevenSeg.clearRect(WeightX, WeightY, WeightW, WeightH, ColorWhite);
  sevenSeg.drawText(WeightX + 6, WeightY + 6, buf, 4, ColorBlack, ColorWhite);
  display.endWrite();
}

static void drawStatusBar(uint16_t color) {
  display.beginWrite();
  display.fillRect(0, 190, aiw::DisplaySt7789::Width, 50, color);
  display.endWrite();
}

static void tryTareNow() {
  uint32_t now = millis();
  if (now - lastTareMs < 1500) return;
  lastTareMs = now;
  hx711->tare(30, 500);
  hasLastDelta = false;
  filteredInit = false;
  hasLastFilteredDelta = false;
  displayLocked = false;
  hasLastShownWeight = false;
  resetDeltaWindow();
  drawStatusBar(ColorBlue);
  Serial.println("tare done");
}

static void clearQrArea() {
  display.beginWrite();
  display.fillRect(QrX, QrY, QrSize, QrSize, ColorWhite);
  display.endWrite();
}

static void printerInit() {
  uint8_t cmd[] = {0x1B, 0x40};
  printerSerial.write(cmd, sizeof(cmd));
}

static void printerSelfTest() {
  printerInit();
  uint8_t cmd[] = {0x12, 0x54};
  printerSerial.write(cmd, sizeof(cmd));
}

static void printerPrintLine(const char *s) {
  if (!s) return;
  printerSerial.write((const uint8_t *)s, strlen(s));
  printerSerial.write((uint8_t)0x0D);
  printerSerial.write((uint8_t)0x0A);
}

static void printerPrintLine(const String &s) { printerPrintLine(s.c_str()); }

static void printerFeed(uint8_t lines) {
  for (uint8_t i = 0; i < lines; ++i) {
    printerSerial.write((uint8_t)0x0D);
    printerSerial.write((uint8_t)0x0A);
  }
}

static void printerPrintDemo(float weight) {
  printerInit();
  printerPrintLine("AI Weight Scale");
  printerPrintLine("Weight:");
  printerPrintLine(String(weight, 1));
  printerFeed(4);
}

static void printerBegin() {
  printerSerial.begin(printerBaud, SERIAL_8N1, printerRxPin, printerTxPin);
  Serial.printf("printer uart tx=%d rx=%d baud=%d\n", printerTxPin, printerRxPin, printerBaud);
}

static void printerSwapPins() {
  int tmp = printerTxPin;
  printerTxPin = printerRxPin;
  printerRxPin = tmp;
  printerBegin();
}

static void printerToggleBaud() {
  printerBaud = (printerBaud == 115200) ? 9600 : 115200;
  printerBegin();
}

void setup() {
  Serial.begin(115200);
  delay(200);

  display.begin();
  drawUiFrame();

  wifi.begin();
  bool ok = wifi.connect(aiw::config::WifiSsid, aiw::config::WifiPassword, 15000);
  Serial.printf("wifi=%s ip=%s\n", ok ? "connected" : "timeout", wifi.ip().c_str());
  drawWifiStatus();

  hx711A.begin();
  int32_t rawA = hx711A.readRaw(500);
  if (rawA == INT32_MIN) {
    hx711B.begin();
    int32_t rawB = hx711B.readRaw(500);
    if (rawB != INT32_MIN) {
      hx711 = &hx711B;
      Serial.printf("hx711 pins swapped dout=%d sck=%d\n", aiw::config::Hx711SckPin, aiw::config::Hx711DoutPin);
    } else {
      Serial.printf("hx711 no data dout=%d sck=%d\n", aiw::config::Hx711DoutPin, aiw::config::Hx711SckPin);
    }
  } else {
    Serial.printf("hx711 ok dout=%d sck=%d raw=%ld\n", aiw::config::Hx711DoutPin, aiw::config::Hx711SckPin, (long)rawA);
    if (rawA == 0) {
      Serial.println("hx711 raw=0 (dout may be floating/shorted or wrong pins)");
    }
  }
  hx711->tare(20, 200);
  printerBegin();
}

void loop() {
  while (Serial.available() > 0) {
    int c = Serial.read();
    if (c == 't' || c == 'T') {
      tryTareNow();
    }
    if (c == 's' || c == 'S') {
      Serial.println("printer: selftest");
      printerSelfTest();
      printerFeed(2);
    }
    if (c == 'p' || c == 'P') {
      Serial.println("printer: demo");
      printerPrintDemo(lastShownWeight);
    }
    if (c == 'f' || c == 'F') {
      Serial.println("printer: feed");
      printerFeed(6);
    }
    if (c == 'x' || c == 'X') {
      Serial.println("printer: swap tx/rx pins");
      printerSwapPins();
    }
    if (c == 'b' || c == 'B') {
      Serial.println("printer: toggle baud 115200/9600");
      printerToggleBaud();
    }
  }

  static uint32_t lastWifiRetry = 0;
  if (!wifi.isConnected()) {
    uint32_t now = millis();
    if (now - lastWifiRetry > 10000) {
      lastWifiRetry = now;
      wifi.connect(aiw::config::WifiSsid, aiw::config::WifiPassword, 8000);
      drawWifiStatus();
    }
  }

  if (state == AppState::Weighing) {
    int32_t raw = hx711->readAverage(3, 120);
    if (raw == INT32_MIN) {
      uint32_t now = millis();
      if (now - lastHx711LogMs > 1000) {
        lastHx711LogMs = now;
        Serial.printf("hx711 timeout dout=%d sck=%d\n", hx711 == &hx711A ? aiw::config::Hx711DoutPin : aiw::config::Hx711SckPin, hx711 == &hx711A ? aiw::config::Hx711SckPin : aiw::config::Hx711DoutPin);
        drawStatusBar(ColorRed);
      }
      delay(100);
      return;
    }

    int32_t delta = raw - hx711->offset();
    float w = (float)delta / hx711->scale();

    if (hasLastDelta) {
      int32_t diff = delta - lastDelta;
      if (diff < 0) diff = -diff;
      if (diff > 2000) {
        glitchCount++;
        resetDeltaWindow();
        lastDelta = delta;
        filteredInit = false;
        hasLastFilteredDelta = false;
        uint32_t now = millis();
        if (now - lastHx711LogMs > 1000) {
          lastHx711LogMs = now;
          Serial.printf("hx711 jump raw=%ld delta=%ld last=%ld diff=%ld reset=%lu\n", (long)raw, (long)delta, (long)lastDelta, (long)diff, (unsigned long)glitchCount);
        }
      }
    }
    hasLastDelta = true;
    lastDelta = delta;

    pushDelta(delta);
    if (!filteredInit) {
      filteredDelta = (float)delta;
      filteredInit = true;
    } else {
      filteredDelta = filteredDelta + 0.25f * ((float)delta - filteredDelta);
    }

    int32_t displayDelta = (int32_t)lroundf(filteredDelta);
    int32_t absDisplayDelta = displayDelta < 0 ? -displayDelta : displayDelta;
    if (absDisplayDelta < ZeroSnapDelta) displayDelta = 0;
    float displayWeight = (float)displayDelta / hx711->scale();
    float quantizedWeight = quantize(displayWeight, DisplayStep);
    if (hasLastShownWeight) {
      if (fabsf(quantizedWeight - lastShownWeight) < DisplayHysteresis) {
        quantizedWeight = lastShownWeight;
      }
    }

    bool stableNow = false;
    int32_t diffDisplay = 0;
    int32_t stableThreshold = 0;
    if (hasLastFilteredDelta) {
      diffDisplay = displayDelta - lastFilteredDelta;
      if (diffDisplay < 0) diffDisplay = -diffDisplay;
      stableThreshold = 120 + absDisplayDelta / 500;
      if (stableThreshold > 500) stableThreshold = 500;
      stableNow = diffDisplay <= stableThreshold;
    }
    hasLastFilteredDelta = true;
    lastFilteredDelta = displayDelta;

    if (stableNow) {
      if (stableHits < 255) stableHits++;
    } else {
      stableHits = 0;
    }
    bool stable = stableHits >= 6;
    float shownWeight = quantizedWeight;
    if (stable) {
      if (!displayLocked) {
        displayLocked = true;
        lockedWeight = quantizedWeight;
      }
      shownWeight = lockedWeight;
    } else if (displayLocked) {
      if (fabsf(quantizedWeight - lockedWeight) > StableUnlockDelta) {
        displayLocked = false;
      } else {
        shownWeight = lockedWeight;
      }
    }
    hasLastShownWeight = true;
    lastShownWeight = shownWeight;
    drawWeight(stable, shownWeight);

    if (stable && wifi.isConnected()) {
      if (absDisplayDelta < PayTriggerDelta) {
        delay(100);
        return;
      }
      lastStableWeight = shownWeight;
      drawStatusBar(ColorBlue);
      state = AppState::CreatingPayment;
    }

    delay(100);
    return;
  }

  if (state == AppState::CreatingPayment) {
    aiw::PaymentCreateRequest req{
      .amount = 0.01f,
      .description = "AI Weight Scale",
      .deviceId = aiw::config::DeviceId,
      .deviceName = aiw::config::DeviceName,
    };

    aiw::PaymentCreateResponse res;
    bool ok = payment.create(req, res);
    if (!ok) {
      drawStatusBar(ColorRed);
      delay(2000);
      state = AppState::Weighing;
      return;
    }

    payCreateRes = res;
    Serial.printf("pay created out_trade_no=%s\n", payCreateRes.outTradeNo.c_str());
    state = AppState::FetchingQr;
    return;
  }

  if (state == AppState::FetchingQr) {
    clearQrArea();
    aiw::QrMatrix m;
    bool ok = qrClient.fetchMatrixText(payCreateRes.codeUrl.c_str(), m);
    if (!ok) {
      drawStatusBar(ColorRed);
      delay(2000);
      state = AppState::Weighing;
      return;
    }
    qrMatrix = m;
    display.beginWrite();
    qrRenderer.drawMatrix(qrMatrix, QrX, QrY, QrSize, ColorBlack, ColorWhite);
    display.endWrite();
    drawStatusBar(ColorBlue);
    lastPollMs = 0;
    state = AppState::WaitingPayment;
    return;
  }

  if (state == AppState::WaitingPayment) {
    if (!wifi.isConnected()) {
      drawWifiStatus();
      delay(500);
      return;
    }
    uint32_t now = millis();
    if (now - lastPollMs < 2000) {
      delay(100);
      return;
    }
    lastPollMs = now;

    aiw::PaymentQueryResponse qres;
    bool ok = payment.query(payCreateRes.outTradeNo.c_str(), qres);
    Serial.printf("pay poll ok=%d success=%d state=%s\n", ok ? 1 : 0, qres.success ? 1 : 0, qres.tradeState.c_str());
    if (ok && qres.success) {
      state = AppState::Paid;
      drawStatusBar(ColorGreen);
      drawWeight(true, lastStableWeight);
      return;
    }
    return;
  }

  if (state == AppState::Paid) {
    delay(500);
    return;
  }
}
