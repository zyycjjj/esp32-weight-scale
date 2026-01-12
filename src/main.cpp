#include <Arduino.h>
#include <HardwareSerial.h>
#include <string.h>
#include <math.h>

#include "app/app_config.h"
#include "app/display_st7789.h"
#include "app/hx711.h"
#include "app/audio_player.h"
#include "app/gacha_controller.h"
#include "app/payment_client.h"
#include "app/qr_client.h"
#include "app/qr_renderer.h"
#include "app/seven_seg.h"
#include "app/wifi_manager.h"
#include "app/ai_client.h"

static aiw::DisplaySt7789 display({.mosi = 6, .sclk = 7, .cs = 5, .dc = 4, .rst = 48, .blBox = 45, .blBox3 = 47});
static aiw::SevenSeg sevenSeg(display);
static aiw::QrRenderer qrRenderer(display);
static aiw::WifiManager wifi;
static aiw::Hx711 hx711A({.dout = aiw::config::Hx711DoutPin, .sck = aiw::config::Hx711SckPin});
static aiw::Hx711 hx711B({.dout = aiw::config::Hx711SckPin, .sck = aiw::config::Hx711DoutPin});
static aiw::Hx711 *hx711 = &hx711A;

static aiw::PaymentClient payment(aiw::config::BackendBaseUrl);
static aiw::QrClient qrClient(aiw::config::BackendBaseUrl);
static aiw::AiClient aiClient(aiw::config::BackendBaseUrl);
static aiw::AudioPlayer audioPlayer;
static aiw::GachaController gacha;
static HardwareSerial printerSerial(1);
static int printerTxPin = aiw::config::PrinterTxPin;
static int printerRxPin = aiw::config::PrinterRxPin;
static int printerBaud = aiw::config::PrinterBaud;
static int printerBaudIndex = 0;
static int printerPinsIndex = 0;
static constexpr int BootPin = 0;
static int currentHeightCm = 170;
static float lastInputHeightCm = 170.0f;
static bool bootPrevPressed = false;
static uint32_t bootPressStartMs = 0;

static constexpr uint16_t ColorWhite = 0xFFFF;
static constexpr uint16_t ColorBlack = 0x0000;
static constexpr uint16_t ColorRed = 0xF800;
static constexpr uint16_t ColorBlue = 0x001F;
static constexpr uint16_t ColorGreen = 0x07E0;
static constexpr uint16_t ColorGray = 0xC618;

static constexpr int WeightX = 10;
static constexpr int WeightY = 10;
static constexpr int WeightW = 300;
static constexpr int WeightH = 50;

static constexpr int WifiDotX = 300;
static constexpr int WifiDotY = 0;
static constexpr int WifiDotSize = 20;
static constexpr int StateDotX = 300;
static constexpr int StateDotY = 22;
static constexpr int StateDotSize = 16;

static constexpr int HeaderH = 66;
static constexpr int QrMargin = 6;

static void qrLayout(int &x, int &y, int &size) {
  y = HeaderH + QrMargin;
  int maxH = aiw::DisplaySt7789::Height - y - QrMargin;
  int maxW = aiw::DisplaySt7789::Width - 2 * QrMargin;
  size = maxH < maxW ? maxH : maxW;
  if (size < 0) size = 0;
  x = (aiw::DisplaySt7789::Width - size) / 2;
}

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
  InputHeight = 0,
  Weighing = 1,
  CreatingPayment = 2,
  FetchingQr = 3,
  WaitingPayment = 4,
  Paid = 5,
};

static AppState state = AppState::InputHeight;
static float lastStableWeight = 0.0f;
static aiw::PaymentCreateResponse payCreateRes;
static aiw::QrMatrix qrMatrix;
static uint32_t lastPollMs = 0;
static bool paidHandled = false;
static uint32_t rewardStartMs = 0;
static aiw::AiWithTtsResult rewardAi;
static bool rewardAiOk = false;
static uint32_t lastHx711LogMs = 0;
static uint32_t lastTareMs = 0;

static const char *stateName(AppState s) {
  switch (s) {
    case AppState::InputHeight: return "InputHeight";
    case AppState::Weighing: return "Weighing";
    case AppState::CreatingPayment: return "CreatingPayment";
    case AppState::FetchingQr: return "FetchingQr";
    case AppState::WaitingPayment: return "WaitingPayment";
    case AppState::Paid: return "Paid";
    default: return "?";
  }
}

static void setState(AppState s) {
  if (state == s) return;
  Serial.printf("state %s -> %s\n", stateName(state), stateName(s));
  state = s;
}

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
  display.drawBorder(ColorBlack, 2);
  display.fillRect(0, HeaderH, aiw::DisplaySt7789::Width, 1, ColorGray);
  display.fillRect(80, 6, 1, HeaderH - 12, ColorGray);
  display.endWrite();
}

static void drawWifiStatus() {
  display.beginWrite();
  uint16_t c = wifi.isConnected() ? ColorGreen : ColorRed;
  display.fillRect(WifiDotX, WifiDotY, WifiDotSize, WifiDotSize, c);
  display.endWrite();
}

static void drawWeight(bool stable, float weight) {
  char wbuf[32];
  char hbuf[8];
  snprintf(wbuf, sizeof(wbuf), "%.1f", weight);
  snprintf(hbuf, sizeof(hbuf), "%d", (int)lroundf(lastInputHeightCm));
  display.beginWrite();
  sevenSeg.clearRect(4, 0, aiw::DisplaySt7789::Width - 4, HeaderH, ColorWhite);
  display.fillRect(80, 6, 1, HeaderH - 12, ColorGray);
  sevenSeg.drawText(10, 10, hbuf, 3, ColorGray, ColorWhite);
  sevenSeg.drawText(100, 6, wbuf, 4, ColorBlack, ColorWhite);
  display.endWrite();
  drawWifiStatus();
}

static void drawStatusBar(uint16_t color) {
  display.beginWrite();
  display.fillRect(StateDotX, StateDotY, StateDotSize, StateDotSize, color);
  display.endWrite();
}

static void drawHeightPicker() {
  char mid[8];
  char left[8];
  char right[8];
  snprintf(mid, sizeof(mid), "%d", currentHeightCm);
  snprintf(left, sizeof(left), "%d", currentHeightCm - 1);
  snprintf(right, sizeof(right), "%d", currentHeightCm + 1);

  display.beginWrite();
  display.clear(ColorWhite);
  display.drawBorder(ColorBlack, 2);
  display.fillRect(0, 66, aiw::DisplaySt7789::Width, 1, ColorGray);
  sevenSeg.drawText(30, 22, left, 2, ColorGray, ColorWhite);
  sevenSeg.drawText(110, 10, mid, 4, ColorBlack, ColorWhite);
  sevenSeg.drawText(240, 22, right, 2, ColorGray, ColorWhite);
  display.endWrite();
  drawWifiStatus();
  drawStatusBar(ColorBlue);
}

static void bootButtonUpdate(bool &shortPress, bool &longPress) {
  shortPress = false;
  longPress = false;
  bool pressed = digitalRead(BootPin) == LOW;
  uint32_t now = millis();
  if (pressed && !bootPrevPressed) {
    bootPressStartMs = now;
  }
  if (!pressed && bootPrevPressed) {
    uint32_t dur = now - bootPressStartMs;
    if (dur >= 800) {
      longPress = true;
    } else if (dur >= 40) {
      shortPress = true;
    }
  }
  bootPrevPressed = pressed;
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
  int x = 0;
  int y = 0;
  int size = 0;
  qrLayout(x, y, size);
  display.beginWrite();
  display.fillRect(0, y, aiw::DisplaySt7789::Width, aiw::DisplaySt7789::Height - y, ColorWhite);
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

static void printerPrintResult(float weightKg, float heightCm, float bmi, const String &category, const String &comment, const String &tip) {
  printerInit();
  printerPrintLine("AI Weight Scale");
  printerPrintLine("Height(cm):");
  printerPrintLine(String(heightCm, 0));
  printerPrintLine("Weight(kg):");
  printerPrintLine(String(weightKg, 1));
  printerPrintLine("BMI:");
  printerPrintLine(String(bmi, 1));
  if (category.length()) {
    printerPrintLine("Category:");
    printerPrintLine(category);
  }
  if (comment.length()) {
    printerPrintLine("Comment:");
    printerPrintLine(comment);
  }
  if (tip.length()) {
    printerPrintLine("Tip:");
    printerPrintLine(tip);
  }
  printerFeed(4);
}

static void printerBegin() {
  printerSerial.begin(printerBaud, SERIAL_8N1, printerRxPin, printerTxPin);
  Serial.printf("printer uart tx=%d rx=%d baud=%d\n", printerTxPin, printerRxPin, printerBaud);
}

static void printerSelectBaudIndex(int idx) {
  static const int baudOptions[] = {9600, 19200, 38400, 57600, 115200, 230400};
  const int n = (int)(sizeof(baudOptions) / sizeof(baudOptions[0]));
  if (idx < 0) idx = 0;
  if (idx >= n) idx = 0;
  printerBaudIndex = idx;
  printerBaud = baudOptions[printerBaudIndex];
  printerBegin();
}

static void printerSelectPinsIndex(int idx) {
  struct Pins {
    int tx;
    int rx;
  };
  static const Pins pinsOptions[] = {
      {.tx = 41, .rx = 42},
      {.tx = 42, .rx = 41},
      {.tx = 43, .rx = 44},
      {.tx = 44, .rx = 43},
      {.tx = 10, .rx = 13},
      {.tx = 13, .rx = 10},
  };
  const int n = (int)(sizeof(pinsOptions) / sizeof(pinsOptions[0]));
  if (idx < 0) idx = 0;
  if (idx >= n) idx = 0;
  printerPinsIndex = idx;
  printerTxPin = pinsOptions[printerPinsIndex].tx;
  printerRxPin = pinsOptions[printerPinsIndex].rx;
  printerBegin();
}

static void printerSwapPins() {
  int tmp = printerTxPin;
  printerTxPin = printerRxPin;
  printerRxPin = tmp;
  printerBegin();
}

static void printerNextBaud() { printerSelectBaudIndex(printerBaudIndex + 1); }
static void printerNextPins() { printerSelectPinsIndex(printerPinsIndex + 1); }

void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(BootPin, INPUT_PULLUP);

  display.begin();
  drawUiFrame();
  drawHeightPicker();

  wifi.begin();
  bool ok = wifi.connect(aiw::config::WifiSsid, aiw::config::WifiPassword, 15000);
  Serial.printf("wifi=%s ip=%s\n", ok ? "connected" : "timeout", wifi.ip().c_str());
  Serial.printf("backend=%s\n", aiw::config::BackendBaseUrl);
  Serial.printf("gacha pin=%d activeHigh=%d pulseMs=%lu\n", aiw::config::GachaPin, aiw::config::GachaActiveHigh ? 1 : 0, (unsigned long)aiw::config::GachaPulseMs);
  Serial.printf("audio enabled=%d bclk=%d lrck=%d dout=%d vol=%d\n", aiw::config::AudioEnabled ? 1 : 0, aiw::config::I2sBclkPin, aiw::config::I2sLrckPin, aiw::config::I2sDoutPin, aiw::config::AudioVolume);
  drawWifiStatus();

  gacha.begin(aiw::config::GachaPin, aiw::config::GachaActiveHigh, aiw::config::GachaPulseMs);
  audioPlayer.begin(aiw::config::AudioEnabled, aiw::config::I2sBclkPin, aiw::config::I2sLrckPin, aiw::config::I2sDoutPin, aiw::config::AudioVolume);

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
  hx711->setScale(aiw::config::Hx711Scale);
  printerSelectPinsIndex(0);
  printerSelectBaudIndex(0);
}

void loop() {
  while (Serial.available() > 0) {
    int c = Serial.read();
    if (c == 't' || c == 'T') {
      tryTareNow();
    }
    if (c == 'c') {
      Serial.println("calibrate 500g: place 500g after tare, wait, then press c again");
      static bool waiting = false;
      static int32_t tareOffset = 0;
      if (!waiting) {
        hx711->tare(30, 500);
        tareOffset = hx711->offset();
        waiting = true;
      } else {
        int32_t raw = hx711->readAverage(10, 200);
        if (raw != INT32_MIN) {
          int32_t delta = raw - tareOffset;
          float scale = (float)delta / 0.5f;
          hx711->setScale(scale);
          Serial.printf("calibrated: raw=%ld offset=%ld delta=%ld scale=%.3f counts/kg\n",
                        (long)raw, (long)tareOffset, (long)delta, scale);
        } else {
          Serial.println("calibrate failed: hx711 timeout");
        }
        waiting = false;
      }
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
      Serial.println("printer: next baud");
      printerNextBaud();
    }
    if (c == 'u' || c == 'U') {
      Serial.println("printer: next pin pair");
      printerNextPins();
    }
    if (c == 'q' || c == 'Q') {
      Serial.printf("force pay: weight=%.2f height=%d\n", lastShownWeight, currentHeightCm);
      lastStableWeight = lastShownWeight;
      drawStatusBar(ColorBlue);
      setState(AppState::CreatingPayment);
    }
  }

  if (printerSerial.available() > 0) {
    Serial.print("printer rx: ");
    int n = 0;
    while (printerSerial.available() > 0 && n < 32) {
      uint8_t b = (uint8_t)printerSerial.read();
      char buf[4];
      snprintf(buf, sizeof(buf), "%02X", (unsigned)b);
      Serial.print(buf);
      n++;
      if (printerSerial.available() > 0 && n < 32) Serial.print(" ");
    }
    Serial.println();
  }

  gacha.loop();

  if (state == AppState::InputHeight) {
    bool shortPress = false;
    bool longPress = false;
    bootButtonUpdate(shortPress, longPress);
    if (shortPress) {
      currentHeightCm++;
      if (currentHeightCm > 220) currentHeightCm = 120;
      drawHeightPicker();
    }
    if (longPress) {
      lastInputHeightCm = (float)currentHeightCm;
      resetDeltaWindow();
      drawStatusBar(ColorBlue);
      setState(AppState::Weighing);
    }
    delay(10);
    return;
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
      Serial.printf("trigger pay: weight=%.2f height=%.0f\n", lastStableWeight, lastInputHeightCm);
      setState(AppState::CreatingPayment);
    }

    delay(100);
    return;
  }

  if (state == AppState::CreatingPayment) {
    Serial.printf("pay create: weight=%.2f height=%.0f\n", lastStableWeight, lastInputHeightCm);
    aiw::PaymentCreateRequest req{
      .amount = 0.01f,
      .description = "AI Weight Scale",
      .deviceId = aiw::config::DeviceId,
      .deviceName = aiw::config::DeviceName,
    };

    aiw::PaymentCreateResponse res;
    bool ok = payment.create(req, res);
    if (!ok) {
      Serial.println("pay create failed");
      drawStatusBar(ColorRed);
      delay(2000);
      setState(AppState::Weighing);
      return;
    }

    payCreateRes = res;
    Serial.printf("pay created out_trade_no=%s\n", payCreateRes.outTradeNo.c_str());
    setState(AppState::FetchingQr);
    return;
  }

  if (state == AppState::FetchingQr) {
    Serial.println("qr fetch matrix");
    clearQrArea();
    aiw::QrMatrix m;
    bool ok = qrClient.fetchMatrixText(payCreateRes.codeUrl.c_str(), m);
    if (!ok) {
      Serial.println("qr fetch failed");
      drawStatusBar(ColorRed);
      delay(2000);
      setState(AppState::Weighing);
      return;
    }
    qrMatrix = m;
    int qx = 0;
    int qy = 0;
    int qs = 0;
    qrLayout(qx, qy, qs);
    display.beginWrite();
    bool drawn = qrRenderer.drawMatrix(qrMatrix, qx, qy, qs, ColorBlack, ColorWhite);
    display.endWrite();
    Serial.printf("qr draw ok=%d size=%d\n", drawn ? 1 : 0, qrMatrix.size);
    drawStatusBar(ColorBlue);
    lastPollMs = 0;
    paidHandled = false;
    setState(AppState::WaitingPayment);
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
      setState(AppState::Paid);
      drawStatusBar(ColorGreen);
      drawWeight(true, lastStableWeight);
      return;
    }
    return;
  }

  if (state == AppState::Paid) {
    if (paidHandled) return;
    paidHandled = true;
    rewardStartMs = millis();
    rewardAi = aiw::AiWithTtsResult{};
    rewardAiOk = aiClient.getCommentWithTts(lastStableWeight, lastInputHeightCm, rewardAi);
    Serial.printf("ai ok=%d bmi=%.1f cat=%s audio=%s\n", rewardAiOk ? 1 : 0, rewardAi.bmi, rewardAi.category.c_str(), rewardAi.audioUrl.c_str());
    if (rewardAiOk) {
      printerPrintResult(lastStableWeight, lastInputHeightCm, rewardAi.bmi, rewardAi.category, rewardAi.comment, rewardAi.tip);
      if (rewardAi.audioUrl.length()) {
        audioPlayer.playWav(aiw::config::BackendBaseUrl, rewardAi.audioUrl, &gacha);
      }
    } else {
      printerPrintResult(lastStableWeight, lastInputHeightCm, 0.0f, "", "", "");
    }
    gacha.trigger();
    while (gacha.isActive() && (millis() - rewardStartMs < 5000)) {
      gacha.loop();
      delay(10);
    }
    drawUiFrame();
    drawHeightPicker();
    setState(AppState::InputHeight);
    return;
  }
}
