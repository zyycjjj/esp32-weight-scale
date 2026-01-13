#include <Arduino.h>
#include <HardwareSerial.h>
#include <string.h>
#include <math.h>
#include <Wire.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "app/app_config.h"
#include "app/display_st7789.h"
#include "app/hx711.h"
#include "app/audio_player.h"
#include "app/gacha_controller.h"
#include "app/touch_button.h"
#include "app/touch_gt911.h"
#include "app/base64.h"
#include "app/payment_client.h"
#include "app/qr_client.h"
#include "app/qr_renderer.h"
#include "app/seven_seg.h"
#include "app/wifi_manager.h"
#include "app/ai_client.h"
#include "app/zh_bitmaps.h"
#include "app/mini_font.h"

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
static aiw::TouchButton touchBtn;
static aiw::TouchGt911 touchScreen;
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
static bool heightTouchPrev = false;
static int heightTouchStartX = 0;
static int heightTouchStartY = 0;
static int heightTouchLastX = 0;
static int heightTouchLastY = 0;
static uint32_t heightTouchStartMs = 0;
static uint8_t heightTouchStartZone = 0;
static bool uiTouchPrev = false;
static int uiTouchStartX = 0;
static int uiTouchStartY = 0;
static int uiTouchLastX = 0;
static int uiTouchLastY = 0;
static uint32_t uiTouchStartMs = 0;

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
static constexpr int FooterH = 44;
static constexpr int FooterY = aiw::DisplaySt7789::Height - FooterH - 4;

static constexpr int HeightBtnY = 140;
static constexpr int HeightBtnH = 90;
static constexpr int HeightLeftX = 10;
static constexpr int HeightLeftW = 90;
static constexpr int HeightNextX = 110;
static constexpr int HeightNextW = 100;
static constexpr int HeightRightX = 220;
static constexpr int HeightRightW = 90;

static constexpr int WeighBtnY = FooterY;
static constexpr int WeighBtnH = FooterH;
static constexpr int WeighTareX = 10;
static constexpr int WeighTareW = 120;
static constexpr int WeighBackX = 190;
static constexpr int WeighBackW = 120;

static constexpr int PayCancelX = 90;
static constexpr int PayCancelY = FooterY;
static constexpr int PayCancelW = 140;
static constexpr int PayCancelH = FooterH;

static void qrLayout(int &x, int &y, int &size) {
  y = HeaderH + QrMargin;
  int maxH = FooterY - y - QrMargin;
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
static bool uiDirty = true;
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
static uint32_t stableHoldStartMs = 0;
static uint32_t lastTouchLogMs = 0;
static uint8_t touchMapMode = 0;
static uint32_t lastHeightBtnMs = 0;
static bool touchRawLogEnabled = false;

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
  uiDirty = true;
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

static void drawButton(int x, int y, int w, int h, uint16_t bg, const char *label) {
  display.fillRect(x, y, w, h, bg);
  display.fillRect(x, y, w, 2, ColorGray);
  display.fillRect(x, y + h - 2, w, 2, ColorGray);
  display.fillRect(x, y, 2, h, ColorGray);
  display.fillRect(x + w - 2, y, 2, h, ColorGray);
  if (label && label[0]) {
    int len = (int)strlen(label);
    int scale = 2;
    int tw = len * 6 * scale;
    int tx = x + (w - tw) / 2;
    int ty = y + (h - 7 * scale) / 2;
    aiw::drawText5x7(display, tx, ty, label, ColorBlack, bg, scale);
  }
}

static void drawHeaderLabel(const char *label) {
  display.beginWrite();
  display.fillRect(2, 2, aiw::DisplaySt7789::Width - 4, HeaderH - 4, ColorWhite);
  display.fillRect(80, 6, 1, HeaderH - 12, ColorGray);
  if (label && label[0]) {
    aiw::drawText5x7(display, 10, 46, label, ColorGray, ColorWhite, 2);
  }
  display.endWrite();
  drawWifiStatus();
}

static void drawHeaderScanPay() {
  display.beginWrite();
  display.fillRect(2, 2, aiw::DisplaySt7789::Width - 4, HeaderH - 4, ColorWhite);
  display.fillRect(80, 6, 1, HeaderH - 12, ColorGray);
  aiw::drawZhText16(display, 12, 12, "扫码支付", ColorBlack, ColorWhite);
  display.endWrite();
  drawWifiStatus();
}

static void drawWeighFooter() {
  display.beginWrite();
  display.fillRect(2, FooterY, aiw::DisplaySt7789::Width - 4, FooterH, ColorWhite);
  drawButton(WeighTareX, WeighBtnY, WeighTareW, WeighBtnH, 0xE7FF, "TARE");
  drawButton(WeighBackX, WeighBtnY, WeighBackW, WeighBtnH, 0xF7DE, "BACK");
  display.endWrite();
}

static void drawPayFooter() {
  display.beginWrite();
  display.fillRect(2, FooterY, aiw::DisplaySt7789::Width - 4, FooterH, ColorWhite);
  drawButton(PayCancelX, PayCancelY, PayCancelW, PayCancelH, 0xF7DE, "CANCEL");
  display.endWrite();
}

static void readTouchMapped(bool &touching, int &x, int &y) {
  touching = false;
  x = 0;
  y = 0;
  aiw::TouchPoint p;
  if (!touchScreen.read(p)) return;
  if (!p.touching) return;
  int tx = p.x;
  int ty = p.y;
  uint16_t mx = touchScreen.maxX();
  uint16_t my = touchScreen.maxY();
  if (mx == 0) mx = (uint16_t)aiw::DisplaySt7789::Width;
  if (my == 0) my = (uint16_t)aiw::DisplaySt7789::Height;
  if (tx < 0) tx = 0;
  if (ty < 0) ty = 0;
  if (tx >= (int)mx) tx = (int)mx - 1;
  if (ty >= (int)my) ty = (int)my - 1;
  if (touchMapMode & 0x01u) {
    int t = tx;
    tx = ty;
    ty = t;
    uint16_t tm = mx;
    mx = my;
    my = tm;
  }
  if (touchMapMode & 0x02u) tx = (int)mx - 1 - tx;
  if (touchMapMode & 0x04u) ty = (int)my - 1 - ty;
  tx = (int)((uint32_t)tx * (uint32_t)aiw::DisplaySt7789::Width / (uint32_t)mx);
  ty = (int)((uint32_t)ty * (uint32_t)aiw::DisplaySt7789::Height / (uint32_t)my);
  if (tx >= aiw::DisplaySt7789::Width) tx = aiw::DisplaySt7789::Width - 1;
  if (ty >= aiw::DisplaySt7789::Height) ty = aiw::DisplaySt7789::Height - 1;
  touching = true;
  x = tx;
  y = ty;
}

static bool touchTapInRect(bool touching, int x, int y, int rx, int ry, int rw, int rh, uint32_t nowMs, bool &prev, int &sx, int &sy, int &lx, int &ly, uint32_t &startMs) {
  if (touching && !prev) {
    sx = x;
    sy = y;
    lx = x;
    ly = y;
    startMs = nowMs;
  }
  if (touching) {
    lx = x;
    ly = y;
  }
  bool tapped = false;
  if (!touching && prev) {
    int dx = lx - sx;
    int dy = ly - sy;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    uint32_t dur = nowMs - startMs;
    bool isTap = dur <= 350 && dx <= 12 && dy <= 12;
    bool in = (lx >= rx && lx < rx + rw && ly >= ry && ly < ry + rh);
    tapped = isTap && in;
  }
  prev = touching;
  return tapped;
}

static bool touchTapEvent(bool touching, int x, int y, uint32_t nowMs, bool &prev, int &sx, int &sy, int &lx, int &ly, uint32_t &startMs, int &tapX, int &tapY) {
  if (touching && !prev) {
    sx = x;
    sy = y;
    lx = x;
    ly = y;
    startMs = nowMs;
  }
  if (touching) {
    lx = x;
    ly = y;
  }
  bool tapped = false;
  if (!touching && prev) {
    int dx = lx - sx;
    int dy = ly - sy;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    uint32_t dur = nowMs - startMs;
    bool isTap = dur <= 350 && dx <= 12 && dy <= 12;
    if (isTap) {
      tapped = true;
      tapX = lx;
      tapY = ly;
    }
  }
  prev = touching;
  return tapped;
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
  snprintf(mid, sizeof(mid), "%d", currentHeightCm);

  auto drawBtn = [&](int x, int y, int w, int h, uint16_t bg) {
    display.fillRect(x, y, w, h, bg);
    display.fillRect(x, y, w, 2, ColorGray);
    display.fillRect(x, y + h - 2, w, 2, ColorGray);
    display.fillRect(x, y, 2, h, ColorGray);
    display.fillRect(x + w - 2, y, 2, h, ColorGray);
  };

  display.beginWrite();
  display.clear(ColorWhite);
  display.drawBorder(ColorBlack, 2);

  aiw::drawZhText16(display, 12, 12, "选择身高", ColorBlack, ColorWhite);
  display.fillRect(0, 40, aiw::DisplaySt7789::Width, 1, ColorGray);

  sevenSeg.drawText(108, 52, mid, 5, ColorBlack, ColorWhite);

  drawBtn(HeightLeftX, HeightBtnY, HeightLeftW, HeightBtnH, 0xF7DE);
  aiw::drawText5x7(display, HeightLeftX + 34, HeightBtnY + 18, "<", ColorBlack, 0xF7DE, 8);

  drawBtn(HeightNextX, HeightBtnY, HeightNextW, HeightBtnH, 0xE7FF);
  aiw::drawText5x7(display, HeightNextX + 18, HeightBtnY + 34, "NEXT", ColorBlack, 0xE7FF, 3);

  drawBtn(HeightRightX, HeightBtnY, HeightRightW, HeightBtnH, 0xF7DE);
  aiw::drawText5x7(display, HeightRightX + 34, HeightBtnY + 18, ">", ColorBlack, 0xF7DE, 8);

  display.endWrite();
  drawWifiStatus();
  drawStatusBar(ColorBlue);
}

static void enterWeighingFromHeight() {
  lastInputHeightCm = (float)currentHeightCm;
  resetDeltaWindow();
  drawUiFrame();
  drawWeighFooter();
  drawStatusBar(ColorBlue);
  setState(AppState::Weighing);
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
  display.fillRect(0, y, aiw::DisplaySt7789::Width, FooterY - y, ColorWhite);
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

static bool isAsciiText(const String &s) {
  for (size_t i = 0; i < s.length(); ++i) {
    uint8_t c = (uint8_t)s[i];
    if (c < 0x20 || c > 0x7E) return false;
  }
  return true;
}

static void printerWriteEscPosCnInit() {
  uint8_t cmd0[] = {0x1B, 0x40};
  printerSerial.write(cmd0, sizeof(cmd0));
  uint8_t cmd1[] = {0x1C, 0x26};
  printerSerial.write(cmd1, sizeof(cmd1));
  uint8_t cmd2[] = {0x1C, 0x43, 0x00};
  printerSerial.write(cmd2, sizeof(cmd2));
}

static void printerWriteBytes(const uint8_t *buf, size_t len) {
  if (!buf || !len) return;
  printerSerial.write(buf, len);
}

static void printerWriteAscii(const char *s) {
  if (!s) return;
  printerSerial.write((const uint8_t *)s, strlen(s));
}

static void printerNewLine() {
  printerSerial.write((uint8_t)0x0D);
  printerSerial.write((uint8_t)0x0A);
}

static void printerPrintChineseTestReceipt174_83() {
  static const uint8_t kTitle[] = {0x41, 0x49, 0xCC, 0xE5, 0xD6, 0xD8, 0xB3, 0xD3};
  static const uint8_t kHeight[] = {0xC9, 0xED, 0xB8, 0xDF, 0x28, 0x63, 0x6D, 0x29, 0xA3, 0xBA};
  static const uint8_t kWeight[] = {0xCC, 0xE5, 0xD6, 0xD8, 0x28, 0x6B, 0x67, 0x29, 0xA3, 0xBA};
  static const uint8_t kBmi[] = {0x42, 0x4D, 0x49, 0xA3, 0xBA};
  static const uint8_t kCat[] = {0xB7, 0xD6, 0xC0, 0xE0, 0xA3, 0xBA};
  static const uint8_t kComment[] = {0xCD, 0xC2, 0xB2, 0xDB, 0xA3, 0xBA};
  static const uint8_t kTip[] = {0xBD, 0xA8, 0xD2, 0xE9, 0xA3, 0xBA};
  static const uint8_t kOverweight[] = {0xB3, 0xAC, 0xD6, 0xD8};
  static const uint8_t kCommentText[] = {0xD2, 0xAA, 0xB6, 0xE0, 0xD4, 0xCB, 0xB6, 0xAF, 0xA3, 0xAC, 0xC9, 0xD9, 0xBA, 0xC8, 0xC4, 0xCC, 0xB2, 0xE8, 0xA1, 0xA3};

  printerWriteEscPosCnInit();
  printerWriteBytes(kTitle, sizeof(kTitle));
  printerNewLine();

  printerWriteBytes(kHeight, sizeof(kHeight));
  printerWriteAscii("174");
  printerNewLine();

  printerWriteBytes(kWeight, sizeof(kWeight));
  printerWriteAscii("83.0");
  printerNewLine();

  printerWriteBytes(kBmi, sizeof(kBmi));
  printerWriteAscii("27.4");
  printerNewLine();

  printerWriteBytes(kCat, sizeof(kCat));
  printerWriteBytes(kOverweight, sizeof(kOverweight));
  printerNewLine();

  printerWriteBytes(kComment, sizeof(kComment));
  printerWriteBytes(kCommentText, sizeof(kCommentText));
  printerNewLine();

  printerWriteBytes(kTip, sizeof(kTip));
  printerWriteBytes(kCommentText, sizeof(kCommentText));
  printerNewLine();

  printerNewLine();
  printerNewLine();
  printerSerial.flush();
}

static bool printerPrintPayloadBase64(const String &payloadBase64) {
  if (!payloadBase64.length()) return false;
  size_t cap = aiw::base64DecodedMaxLen(payloadBase64.length());
  uint8_t *buf = (uint8_t *)malloc(cap);
  if (!buf) return false;
  size_t outLen = 0;
  bool ok = aiw::base64DecodeToBytes(payloadBase64, buf, cap, outLen);
  if (ok && outLen > 0) {
    printerSerial.write(buf, outLen);
    printerSerial.flush();
  }
  free(buf);
  return ok && outLen > 0;
}

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
  printerSerial.flush();
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
  if (category.length() && isAsciiText(category)) {
    printerPrintLine("Category:");
    printerPrintLine(category);
  }
  if (comment.length() && isAsciiText(comment)) {
    printerPrintLine("Comment:");
    printerPrintLine(comment);
  }
  if (tip.length() && isAsciiText(tip)) {
    printerPrintLine("Tip:");
    printerPrintLine(tip);
  }
  if ((category.length() && !isAsciiText(category)) || (comment.length() && !isAsciiText(comment)) || (tip.length() && !isAsciiText(tip))) {
    printerPrintLine("CN text omitted");
  }
  printerFeed(4);
  printerSerial.flush();
}

static void printerBegin() {
  printerSerial.begin(printerBaud, SERIAL_8N1, printerRxPin, printerTxPin);
  Serial.printf("printer uart tx=%d rx=%d baud=%d\n", printerTxPin, printerRxPin, printerBaud);
}

struct PrinterPins {
  int tx;
  int rx;
};

static const int kPrinterBaudOptions[] = {9600, 19200, 38400, 57600, 115200, 230400};
static const PrinterPins kPrinterPinsOptions[] = {
    {.tx = 41, .rx = 42},
    {.tx = 42, .rx = 41},
    {.tx = 43, .rx = 44},
    {.tx = 44, .rx = 43},
    {.tx = 10, .rx = 13},
    {.tx = 13, .rx = 10},
};

static void printerSelectBaudIndex(int idx) {
  const int n = (int)(sizeof(kPrinterBaudOptions) / sizeof(kPrinterBaudOptions[0]));
  if (idx < 0) idx = 0;
  if (idx >= n) idx = 0;
  printerBaudIndex = idx;
  printerBaud = kPrinterBaudOptions[printerBaudIndex];
  printerBegin();
}

static void printerSelectPinsIndex(int idx) {
  const int n = (int)(sizeof(kPrinterPinsOptions) / sizeof(kPrinterPinsOptions[0]));
  if (idx < 0) idx = 0;
  if (idx >= n) idx = 0;
  printerPinsIndex = idx;
  printerTxPin = kPrinterPinsOptions[printerPinsIndex].tx;
  printerRxPin = kPrinterPinsOptions[printerPinsIndex].rx;
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

static void printerAutoScan() {
  Serial.println("printer scan: start");
  const int baudN = (int)(sizeof(kPrinterBaudOptions) / sizeof(kPrinterBaudOptions[0]));
  const int pinsN = (int)(sizeof(kPrinterPinsOptions) / sizeof(kPrinterPinsOptions[0]));

  for (int pi = 0; pi < pinsN; ++pi) {
    for (int bi = 0; bi < baudN; ++bi) {
      printerTxPin = kPrinterPinsOptions[pi].tx;
      printerRxPin = kPrinterPinsOptions[pi].rx;
      printerBaud = kPrinterBaudOptions[bi];
      printerBegin();

      printerInit();
      String line = String("TEST tx=") + printerTxPin + " rx=" + printerRxPin + " baud=" + printerBaud;
      printerPrintLine(line);
      printerPrintLine("If you can read this, stop scan.");
      printerFeed(3);
      printerSerial.flush();
      delay(600);
    }
  }
  Serial.println("printer scan: done");
}

static bool i2cReadReg(uint8_t addr, uint8_t reg, uint8_t &val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(true) != 0) return false;
  if (Wire.requestFrom((int)addr, 1) != 1) return false;
  val = (uint8_t)Wire.read();
  return true;
}

static bool probeEs8311(int sda, int scl, uint8_t addr, uint8_t &id1, uint8_t &id2) {
  if (sda < 0 || scl < 0) return false;
  Wire.end();
  pinMode(sda, INPUT_PULLUP);
  pinMode(scl, INPUT_PULLUP);
  Wire.begin(sda, scl);
  Wire.setClock(100000);
  if (!i2cReadReg(addr, 0xFD, id1)) return false;
  if (!i2cReadReg(addr, 0xFE, id2)) return false;
  return true;
}

static bool i2cAck(int sda, int scl, uint8_t addr) {
  Wire.end();
  pinMode(sda, INPUT_PULLUP);
  pinMode(scl, INPUT_PULLUP);
  Wire.begin(sda, scl);
  Wire.setClock(100000);
  Wire.setTimeout(20);
  Wire.beginTransmission(addr);
  return Wire.endTransmission(true) == 0;
}

static void i2cScanBus(int sda, int scl) {
  if (sda < 0 || scl < 0) return;
  Wire.end();
  pinMode(sda, INPUT_PULLUP);
  pinMode(scl, INPUT_PULLUP);
  Wire.begin(sda, scl);
  Wire.setClock(100000);
  Wire.setTimeout(20);
  int found = 0;
  for (uint8_t addr = 0x08; addr <= 0x77; ++addr) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission(true) == 0) {
      Serial.printf("  addr=0x%02X\n", (unsigned)addr);
      found++;
    }
    delay(1);
  }
  Serial.printf("  found=%d\n", found);
}

static void codecDumpRegs(int sda, int scl, uint8_t addr) {
  if (sda < 0 || scl < 0) return;
  Wire.end();
  pinMode(sda, INPUT_PULLUP);
  pinMode(scl, INPUT_PULLUP);
  Wire.begin(sda, scl);
  Wire.setClock(100000);
  Wire.setTimeout(20);

  static const uint8_t regs[] = {0x00, 0x01, 0x02, 0x03, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0D, 0x0E, 0x12, 0x13, 0x31, 0x32, 0x37, 0xFD, 0xFE};
  for (size_t i = 0; i < sizeof(regs) / sizeof(regs[0]); ++i) {
    uint8_t v = 0;
    if (i2cReadReg(addr, regs[i], v)) {
      Serial.printf("  reg 0x%02X = 0x%02X\n", (unsigned)regs[i], (unsigned)v);
    } else {
      Serial.printf("  reg 0x%02X = <err>\n", (unsigned)regs[i]);
    }
    delay(2);
  }
}

static bool extractJsonStringField(const String &json, const char *field, String &out) {
  String key = String("\"") + field + "\":";
  int idx = json.indexOf(key);
  if (idx < 0) return false;
  idx += key.length();
  while (idx < (int)json.length() && (json[idx] == ' ')) idx++;
  if (idx >= (int)json.length() || json[idx] != '\"') return false;
  idx++;
  int end = json.indexOf('\"', idx);
  if (end < 0) return false;
  out = json.substring(idx, end);
  return true;
}

static bool isHttpsUrl(const String &url) { return url.startsWith("https://"); }

static bool postJson(const String &url, const String &body, int &outCode, String &outPayload) {
  outCode = -1;
  outPayload = "";
  if (isHttpsUrl(url)) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    if (!http.begin(client, url)) return false;
    http.addHeader("Content-Type", "application/json");
    outCode = http.POST(body);
    if (outCode > 0) outPayload = http.getString();
    http.end();
    return true;
  } else {
    WiFiClient client;
    HTTPClient http;
    if (!http.begin(client, url)) return false;
    http.addHeader("Content-Type", "application/json");
    outCode = http.POST(body);
    if (outCode > 0) outPayload = http.getString();
    http.end();
    return true;
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);

  display.begin();
  drawUiFrame();
  drawHeightPicker();

  wifi.begin();
  bool ok = wifi.connect(aiw::config::WifiSsid, aiw::config::WifiPassword, 15000);
  Serial.printf("wifi=%s ip=%s\n", ok ? "connected" : "timeout", wifi.ip().c_str());
  Serial.printf("backend=%s\n", aiw::config::BackendBaseUrl);
  Serial.printf("gacha pin=%d activeHigh=%d pulseMs=%lu\n", aiw::config::GachaPin, aiw::config::GachaActiveHigh ? 1 : 0, (unsigned long)aiw::config::GachaPulseMs);
  Serial.printf("audio enabled=%d bclk=%d lrck=%d dout=%d mclk=%d pa=%d i2c_sda=%d i2c_scl=%d codec=0x%02X vol=%d\n", aiw::config::AudioEnabled ? 1 : 0, aiw::config::I2sBclkPin, aiw::config::I2sLrckPin, aiw::config::I2sDoutPin, aiw::config::I2sMclkPin, aiw::config::PaCtrlPin, aiw::config::I2cSdaPin, aiw::config::I2cSclPin, (unsigned)aiw::config::CodecI2cAddr, aiw::config::AudioVolume);
  Serial.printf("touch pin=%d threshold=%u\n", aiw::config::TouchPin, (unsigned)aiw::config::TouchThreshold);
  drawWifiStatus();

  gacha.begin(aiw::config::GachaPin, aiw::config::GachaActiveHigh, aiw::config::GachaPulseMs);
  audioPlayer.begin(aiw::config::AudioEnabled, aiw::config::I2sBclkPin, aiw::config::I2sLrckPin, aiw::config::I2sDoutPin, aiw::config::I2sMclkPin, aiw::config::PaCtrlPin, aiw::config::I2cSdaPin, aiw::config::I2cSclPin, aiw::config::CodecI2cAddr, aiw::config::AudioVolume);
  touchBtn.begin(BootPin, aiw::config::TouchPin, aiw::config::TouchThreshold);
  touchScreen.begin(aiw::config::I2cSdaPin, aiw::config::I2cSclPin, 0);
  bool touchOk = touchScreen.detect();
  Serial.printf("touch gt911 detect=%d sda=%d scl=%d maxX=%u maxY=%u\n", touchOk ? 1 : 0, aiw::config::I2cSdaPin, aiw::config::I2cSclPin, (unsigned)touchScreen.maxX(), (unsigned)touchScreen.maxY());
  if (!touchOk) {
    Serial.println("touch i2c scan:");
    i2cScanBus(aiw::config::I2cSdaPin, aiw::config::I2cSclPin);
  }

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
  if (aiw::config::PrinterTxPin >= 0 && aiw::config::PrinterRxPin >= 0) {
    printerTxPin = aiw::config::PrinterTxPin;
    printerRxPin = aiw::config::PrinterRxPin;
  } else {
    printerTxPin = 41;
    printerRxPin = 42;
  }
  if (aiw::config::PrinterBaud > 0) {
    printerBaud = aiw::config::PrinterBaud;
  } else {
    printerBaud = 9600;
  }
  printerBegin();
}

void loop() {
  {
    aiw::TouchPoint p;
    bool ok = touchScreen.read(p);
    if (touchRawLogEnabled && ok && p.touching) {
      uint32_t now = millis();
      if (now - lastTouchLogMs > 200) {
        lastTouchLogMs = now;
        Serial.printf("touch raw x=%d y=%d\n", p.x, p.y);
      }
    }
  }

  while (Serial.available() > 0) {
    int c = Serial.read();
    if (c == 'p' || c == 'P') {
      touchRawLogEnabled = !touchRawLogEnabled;
      Serial.printf("touch raw log=%d\n", touchRawLogEnabled ? 1 : 0);
    }
    if (c == 'm' || c == 'M') {
      touchMapMode = (uint8_t)((touchMapMode + 1) & 0x07u);
      Serial.printf("touch map mode=%u (swap=%u mx=%u my=%u)\n", (unsigned)touchMapMode, (unsigned)((touchMapMode & 0x01u) ? 1 : 0), (unsigned)((touchMapMode & 0x02u) ? 1 : 0), (unsigned)((touchMapMode & 0x04u) ? 1 : 0));
      uiDirty = true;
      if (state == AppState::InputHeight) drawHeightPicker();
    }
    if (c == 'z' || c == 'Z') {
      aiw::setZhRenderMode(aiw::zhRenderMode() + 1);
      Serial.printf("zh render mode=%u\n", (unsigned)aiw::zhRenderMode());
      uiDirty = true;
      if (state == AppState::InputHeight) drawHeightPicker();
      if (state == AppState::WaitingPayment || state == AppState::FetchingQr) drawHeaderScanPay();
    }
    if (c == 't' || c == 'T') {
      tryTareNow();
    }
    if (c == 'v' || c == 'V') {
      Serial.printf("touch value=%u\n", (unsigned)touchBtn.lastTouchValue());
    }
    if (c == 'g' || c == 'G') {
      Serial.println("touch scan (GPIO 1..14):");
      for (int pin = 1; pin <= 14; ++pin) {
        uint16_t v = (uint16_t)touchRead(pin);
        Serial.printf("  pin=%d value=%u\n", pin, (unsigned)v);
        delay(20);
      }
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
    if (c == 'z' || c == 'Z') {
      printerAutoScan();
    }
    if (c == 'q' || c == 'Q') {
      Serial.printf("force pay: weight=%.2f height=%d\n", lastShownWeight, currentHeightCm);
      lastStableWeight = lastShownWeight;
      drawStatusBar(ColorBlue);
      setState(AppState::CreatingPayment);
    }
    if (c == '1') {
      Serial.println("test: local cn receipt 174/83");
      printerPrintChineseTestReceipt174_83();
    }
    if (c == '2') {
      Serial.println("test: audio beep 880Hz 800ms");
      bool ok = audioPlayer.playBeep(880, 800);
      Serial.printf("test: beep ok=%d\n", ok ? 1 : 0);
    }
    if (c == '3') {
      struct Pins {
        int bclk;
        int lrck;
        int dout;
        int mclk;
      };
      static const Pins opts[] = {
          {.bclk = 17, .lrck = 45, .dout = 15, .mclk = 2},
          {.bclk = 17, .lrck = 46, .dout = 15, .mclk = 2},
          {.bclk = 17, .lrck = 18, .dout = 15, .mclk = 2},
          {.bclk = 12, .lrck = 11, .dout = 10, .mclk = -1},
          {.bclk = 14, .lrck = 13, .dout = 12, .mclk = -1},
          {.bclk = 16, .lrck = 18, .dout = 15, .mclk = 2},
      };
      Serial.println("test: audio scan start");
      for (size_t i = 0; i < sizeof(opts) / sizeof(opts[0]); ++i) {
        Serial.printf("audio try #%u bclk=%d lrck=%d dout=%d mclk=%d\n", (unsigned)i, opts[i].bclk, opts[i].lrck, opts[i].dout, opts[i].mclk);
        audioPlayer.begin(true, opts[i].bclk, opts[i].lrck, opts[i].dout, opts[i].mclk, aiw::config::PaCtrlPin, aiw::config::I2cSdaPin, aiw::config::I2cSclPin, aiw::config::CodecI2cAddr, aiw::config::AudioVolume);
        audioPlayer.playBeep(880, 500);
        delay(700);
      }
      Serial.println("test: audio scan done");
    }
    if (c == '4') {
      struct I2cPins {
        int sda;
        int scl;
      };
      static const I2cPins opts[] = {
          {.sda = aiw::config::I2cSdaPin, .scl = aiw::config::I2cSclPin},
          {.sda = 8, .scl = 18},
          {.sda = 18, .scl = 8},
          {.sda = 41, .scl = 40},
          {.sda = 40, .scl = 41},
          {.sda = 10, .scl = 9},
          {.sda = 9, .scl = 10},
          {.sda = 17, .scl = 18},
          {.sda = 18, .scl = 17},
          {.sda = 1, .scl = 2},
          {.sda = 2, .scl = 1},
      };
      static const uint8_t addrs[] = {0x18, 0x19, 0x30, 0x32};
      Serial.println("test: codec scan start");
      bool found = false;
      int foundSda = -1;
      int foundScl = -1;
      uint8_t foundAddr = 0;
      uint8_t id1 = 0, id2 = 0;
      for (size_t i = 0; i < sizeof(opts) / sizeof(opts[0]) && !found; ++i) {
        if (opts[i].sda < 0 || opts[i].scl < 0) continue;
        for (size_t j = 0; j < sizeof(addrs) / sizeof(addrs[0]); ++j) {
          uint8_t a = addrs[j];
          uint8_t x = 0, y = 0;
          if (probeEs8311(opts[i].sda, opts[i].scl, a, x, y)) {
            found = true;
            foundSda = opts[i].sda;
            foundScl = opts[i].scl;
            foundAddr = a;
            id1 = x;
            id2 = y;
            break;
          }
        }
      }
      if (found) {
        Serial.printf("codec found: sda=%d scl=%d addr=0x%02X id=%02X%02X\n", foundSda, foundScl, (unsigned)foundAddr, (unsigned)id1, (unsigned)id2);
        audioPlayer.begin(true, aiw::config::I2sBclkPin, aiw::config::I2sLrckPin, aiw::config::I2sDoutPin, aiw::config::I2sMclkPin, aiw::config::PaCtrlPin, foundSda, foundScl, foundAddr, aiw::config::AudioVolume);
        bool ok = audioPlayer.playBeep(880, 800);
        Serial.printf("codec beep ok=%d\n", ok ? 1 : 0);
      } else {
        Serial.println("codec not found");
      }
      Serial.println("test: codec scan done");
    }
    if (c == '5') {
      Serial.println("test: codec bruteforce scan start");
      static const int pins[] = {1, 2, 3, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 35, 36, 37, 38, 43, 44};
      static const int banned[] = {4, 5, 6, 7, 39, 40, 41, 42, 45, 47, 48};
      static const uint8_t addrs[] = {0x18, 0x19, 0x30, 0x32};

      auto isBanned = [&](int p) -> bool {
        for (size_t i = 0; i < sizeof(banned) / sizeof(banned[0]); ++i) {
          if (banned[i] == p) return true;
        }
        return false;
      };

      bool found = false;
      int foundSda = -1;
      int foundScl = -1;
      uint8_t foundAddr = 0;
      uint8_t id1 = 0, id2 = 0;

      for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]) && !found; ++i) {
        int sda = pins[i];
        if (isBanned(sda)) continue;
        for (size_t j = 0; j < sizeof(pins) / sizeof(pins[0]) && !found; ++j) {
          int scl = pins[j];
          if (scl == sda) continue;
          if (isBanned(scl)) continue;
          for (size_t k = 0; k < sizeof(addrs) / sizeof(addrs[0]); ++k) {
            uint8_t a = addrs[k];
            if (!i2cAck(sda, scl, a)) continue;
            uint8_t x = 0, y = 0;
            if (!probeEs8311(sda, scl, a, x, y)) continue;
            found = true;
            foundSda = sda;
            foundScl = scl;
            foundAddr = a;
            id1 = x;
            id2 = y;
            break;
          }
        }
      }

      if (found) {
        Serial.printf("codec found: sda=%d scl=%d addr=0x%02X id=%02X%02X\n", foundSda, foundScl, (unsigned)foundAddr, (unsigned)id1, (unsigned)id2);
        audioPlayer.begin(true, aiw::config::I2sBclkPin, aiw::config::I2sLrckPin, aiw::config::I2sDoutPin, aiw::config::I2sMclkPin, aiw::config::PaCtrlPin, foundSda, foundScl, foundAddr, aiw::config::AudioVolume);
        bool ok = audioPlayer.playBeep(880, 800);
        Serial.printf("codec beep ok=%d\n", ok ? 1 : 0);
      } else {
        Serial.println("codec not found");
      }
      Serial.println("test: codec bruteforce scan done");
    }
    if (c == '6') {
      Serial.printf("test: i2c bus scan sda=%d scl=%d\n", aiw::config::I2cSdaPin, aiw::config::I2cSclPin);
      i2cScanBus(aiw::config::I2cSdaPin, aiw::config::I2cSclPin);
      Serial.println("test: i2c bus scan done");
    }
    if (c == '7') {
      if (aiw::config::PaCtrlPin >= 0) {
        Serial.printf("test: pa toggle pin=%d\n", aiw::config::PaCtrlPin);
        pinMode(aiw::config::PaCtrlPin, OUTPUT);
        digitalWrite(aiw::config::PaCtrlPin, LOW);
        delay(50);
        audioPlayer.playBeep(880, 300);
        delay(100);
        digitalWrite(aiw::config::PaCtrlPin, HIGH);
        delay(50);
        audioPlayer.playBeep(880, 800);
        Serial.println("test: pa toggle done");
      } else {
        Serial.println("test: pa pin=-1");
      }
    }
    if (c == '8') {
      Serial.printf("test: codec dump sda=%d scl=%d addr=0x%02X\n", aiw::config::I2cSdaPin, aiw::config::I2cSclPin, (unsigned)aiw::config::CodecI2cAddr);
      codecDumpRegs(aiw::config::I2cSdaPin, aiw::config::I2cSclPin, (uint8_t)aiw::config::CodecI2cAddr);
      Serial.println("test: codec dump done");
    }
    if (c == '9') {
      Serial.println("test: aliyun tts start");
      String url = String(aiw::config::BackendBaseUrl) + "/tts/synthesis";
      String body = "{";
      body += "\"text\":\"欢迎使用AI体重秤，现在开始阿里云语音合成播放测试。\",";
      body += "\"voice\":\"xiaoyun\",";
      body += "\"format\":\"wav\",";
      body += "\"sampleRate\":16000,";
      body += "\"volume\":65,";
      body += "\"speechRate\":0,";
      body += "\"pitchRate\":0";
      body += "}";
      int code = -1;
      String payload;
      bool reqOk = postJson(url, body, code, payload);
      Serial.printf("tts http ok=%d code=%d\n", reqOk ? 1 : 0, code);
      if (!reqOk || code < 200 || code >= 300) {
        if (payload.length()) Serial.printf("tts payload=%s\n", payload.substring(0, 200).c_str());
        Serial.println("test: aliyun tts done");
      } else {
        String audioUrl;
        if (!extractJsonStringField(payload, "audioUrl", audioUrl) || !audioUrl.length()) {
          Serial.printf("tts parse audioUrl failed payload=%s\n", payload.substring(0, 200).c_str());
          Serial.println("test: aliyun tts done");
        } else {
          Serial.printf("tts audioUrl=%s\n", audioUrl.c_str());
          bool started = audioPlayer.playWavAsync(aiw::config::BackendBaseUrl, audioUrl);
          Serial.printf("tts play started=%d\n", started ? 1 : 0);
          uint32_t waitStart = millis();
          while (started && audioPlayer.isPlaying() && (millis() - waitStart < 30000)) {
            delay(10);
          }
          Serial.println("test: aliyun tts done");
        }
      }
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
    if (uiDirty) {
      uiDirty = false;
      heightTouchPrev = false;
      drawHeightPicker();
    }

    bool touching = false;
    int tx = 0;
    int ty = 0;
    readTouchMapped(touching, tx, ty);

    uint32_t now = millis();
    if (touching && !heightTouchPrev) {
      if (now - lastHeightBtnMs > 180) {
        if (ty >= HeightBtnY && ty < HeightBtnY + HeightBtnH) {
          if (tx >= HeightLeftX && tx < HeightLeftX + HeightLeftW) {
            currentHeightCm--;
            if (currentHeightCm < 120) currentHeightCm = 120;
            drawHeightPicker();
            lastHeightBtnMs = now;
          } else if (tx >= HeightRightX && tx < HeightRightX + HeightRightW) {
            currentHeightCm++;
            if (currentHeightCm > 220) currentHeightCm = 220;
            drawHeightPicker();
            lastHeightBtnMs = now;
          } else if (tx >= HeightNextX && tx < HeightNextX + HeightNextW) {
            lastHeightBtnMs = now;
            enterWeighingFromHeight();
            delay(10);
            return;
          }
        }
      }
    }
    if (touching && !heightTouchPrev) {
      heightTouchStartX = tx;
      heightTouchStartY = ty;
      heightTouchLastX = tx;
      heightTouchLastY = ty;
      heightTouchStartMs = now;
      heightTouchStartZone = 0;
      if (ty >= HeightBtnY && ty < HeightBtnY + HeightBtnH) {
        if (tx >= HeightLeftX && tx < HeightLeftX + HeightLeftW) heightTouchStartZone = 1;
        else if (tx >= HeightNextX && tx < HeightNextX + HeightNextW) heightTouchStartZone = 2;
        else if (tx >= HeightRightX && tx < HeightRightX + HeightRightW) heightTouchStartZone = 3;
      }
    }
    if (touching) {
      heightTouchLastX = tx;
      heightTouchLastY = ty;
    }
    bool heightChanged = false;
    if (!touching && heightTouchPrev) {
      int dx = heightTouchLastX - heightTouchStartX;
      int dy = heightTouchLastY - heightTouchStartY;
      if (dx < 0) dx = -dx;
      if (dy < 0) dy = -dy;
      int rawDx = heightTouchLastX - heightTouchStartX;
      uint32_t dur = now - heightTouchStartMs;

      bool isTap = dur <= 350 && dx <= 12 && dy <= 12;
      bool isSwipe = dur <= 700 && dx >= 60 && dy <= 35;

      if (isSwipe) {
        if (rawDx > 0) currentHeightCm++;
        else currentHeightCm--;
        if (currentHeightCm > 220) currentHeightCm = 220;
        if (currentHeightCm < 120) currentHeightCm = 120;
        heightChanged = true;
      } else if (isTap) {
        if (heightTouchStartZone == 1) {
          currentHeightCm--;
          if (currentHeightCm < 120) currentHeightCm = 120;
          heightChanged = true;
        } else if (heightTouchStartZone == 3) {
          currentHeightCm++;
          if (currentHeightCm > 220) currentHeightCm = 220;
          heightChanged = true;
        } else if (heightTouchStartZone == 2) {
          enterWeighingFromHeight();
          delay(10);
          return;
        }
      }
    }
    heightTouchPrev = touching;

    bool shortPress = false;
    bool longPress = false;
    bootButtonUpdate(shortPress, longPress);
    if (shortPress) {
      currentHeightCm++;
      if (currentHeightCm > 220) currentHeightCm = 120;
      heightChanged = true;
    }
    if (longPress) {
      enterWeighingFromHeight();
      delay(10);
      return;
    }

    if (heightChanged) {
      drawHeightPicker();
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
    if (uiDirty) {
      uiDirty = false;
      uiTouchPrev = false;
      stableHoldStartMs = 0;
      drawUiFrame();
      drawWeighFooter();
      drawStatusBar(ColorBlue);
    }

    bool touching = false;
    int tx = 0;
    int ty = 0;
    readTouchMapped(touching, tx, ty);
    int tapX = 0;
    int tapY = 0;
    uint32_t nowTap = millis();
    bool tapped = touchTapEvent(touching, tx, ty, nowTap, uiTouchPrev, uiTouchStartX, uiTouchStartY, uiTouchLastX, uiTouchLastY, uiTouchStartMs, tapX, tapY);
    if (tapped) {
      if (tapY >= WeighBtnY && tapY < WeighBtnY + WeighBtnH) {
        if (tapX >= WeighTareX && tapX < WeighTareX + WeighTareW) {
          tryTareNow();
          stableHoldStartMs = 0;
        } else if (tapX >= WeighBackX && tapX < WeighBackX + WeighBackW) {
          stableHoldStartMs = 0;
          heightTouchPrev = false;
          setState(AppState::InputHeight);
          delay(10);
          return;
        }
      }
    }

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

    if (!stable) {
      stableHoldStartMs = 0;
    }
    if (stable && wifi.isConnected()) {
      if (absDisplayDelta < PayTriggerDelta) {
        stableHoldStartMs = 0;
        delay(100);
        return;
      }
      if (stableHoldStartMs == 0) {
        stableHoldStartMs = millis();
        drawStatusBar(ColorBlue);
      }
      if (millis() - stableHoldStartMs < 900) {
        delay(60);
        return;
      }
      stableHoldStartMs = 0;
      lastStableWeight = shownWeight;
      drawStatusBar(ColorBlue);
      Serial.printf("trigger pay: weight=%.2f height=%.0f\n", lastStableWeight, lastInputHeightCm);
      setState(AppState::CreatingPayment);
    } else if (!wifi.isConnected()) {
      stableHoldStartMs = 0;
    }

    delay(100);
    return;
  }

  if (state == AppState::CreatingPayment) {
    if (uiDirty) {
      uiDirty = false;
      uiTouchPrev = false;
      drawUiFrame();
      drawHeaderLabel("PAY");
      drawStatusBar(ColorBlue);
    }
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
    if (uiDirty) {
      uiDirty = false;
      uiTouchPrev = false;
      drawUiFrame();
      drawHeaderScanPay();
      drawStatusBar(ColorBlue);
      drawPayFooter();
      clearQrArea();
    }
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
    drawPayFooter();
    lastPollMs = 0;
    paidHandled = false;
    setState(AppState::WaitingPayment);
    return;
  }

  if (state == AppState::WaitingPayment) {
    if (uiDirty) {
      uiDirty = false;
      uiTouchPrev = false;
      drawHeaderScanPay();
      drawStatusBar(ColorBlue);
      drawPayFooter();
    }

    bool touching = false;
    int tx = 0;
    int ty = 0;
    readTouchMapped(touching, tx, ty);
    int tapX = 0;
    int tapY = 0;
    uint32_t nowTap = millis();
    bool tapped = touchTapEvent(touching, tx, ty, nowTap, uiTouchPrev, uiTouchStartX, uiTouchStartY, uiTouchLastX, uiTouchLastY, uiTouchStartMs, tapX, tapY);
    if (tapped) {
      bool inCancel = (tapX >= PayCancelX && tapX < PayCancelX + PayCancelW && tapY >= PayCancelY && tapY < PayCancelY + PayCancelH);
      if (inCancel) {
        setState(AppState::Weighing);
        delay(10);
        return;
      }
    }

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
      drawUiFrame();
      drawStatusBar(ColorGreen);
      drawWeight(true, lastStableWeight);
      return;
    }
    return;
  }

  if (state == AppState::Paid) {
    if (paidHandled) return;
    paidHandled = true;
    drawUiFrame();
    drawHeaderLabel("RESULT");
    drawStatusBar(ColorGreen);
    rewardStartMs = millis();
    rewardAi = aiw::AiWithTtsResult{};
    rewardAiOk = aiClient.getCommentWithTts(lastStableWeight, lastInputHeightCm, rewardAi);
    Serial.printf("ai ok=%d bmi=%.1f cat=%s audio=%s\n", rewardAiOk ? 1 : 0, rewardAi.bmi, rewardAi.category.c_str(), rewardAi.audioUrl.c_str());
    if (rewardAiOk) {
      drawHeaderLabel("PRINT AUDIO");
      Serial.println("printer: print result start");
      if (!rewardAi.audioUrl.length()) {
        Serial.println("tts audioUrl empty (backend may be returning tts:null)");
      }
      bool audioStarted = false;
      if (rewardAi.audioUrl.length()) {
        audioStarted = audioPlayer.playWavAsync(aiw::config::BackendBaseUrl, rewardAi.audioUrl);
      }
      bool printed = false;
      if (rewardAi.printPayloadBase64.length()) {
        printed = printerPrintPayloadBase64(rewardAi.printPayloadBase64);
      }
      if (!printed) {
        printerPrintResult(lastStableWeight, lastInputHeightCm, rewardAi.bmi, rewardAi.category, rewardAi.comment, rewardAi.tip);
      }
      Serial.println("printer: print result done");
      uint32_t waitStart = millis();
      while (audioStarted && audioPlayer.isPlaying() && (millis() - waitStart < 25000)) {
        delay(10);
      }
    } else {
      Serial.println("printer: print fallback start");
      printerPrintResult(lastStableWeight, lastInputHeightCm, 0.0f, "", "", "");
      Serial.println("printer: print fallback done");
    }
    gacha.trigger();
    while (gacha.isActive() && (millis() - rewardStartMs < 5000)) {
      gacha.loop();
      delay(10);
    }
    stableHoldStartMs = 0;
    heightTouchPrev = false;

    drawUiFrame();
    drawHeaderLabel("DONE");
    drawStatusBar(ColorGreen);
    display.beginWrite();
    drawButton(90, FooterY, 140, FooterH, 0xE7FF, "RESTART");
    display.endWrite();

    uiTouchPrev = false;
    uint32_t waitStart = millis();
    while (millis() - waitStart < 8000) {
      bool touching = false;
      int tx = 0;
      int ty = 0;
      readTouchMapped(touching, tx, ty);
      int tapX = 0;
      int tapY = 0;
      uint32_t nowTap = millis();
      bool tapped = touchTapEvent(touching, tx, ty, nowTap, uiTouchPrev, uiTouchStartX, uiTouchStartY, uiTouchLastX, uiTouchLastY, uiTouchStartMs, tapX, tapY);
      if (tapped) {
        bool inRestart = (tapX >= 90 && tapX < 230 && tapY >= FooterY && tapY < FooterY + FooterH);
        if (inRestart) break;
      }
      bool sp = false;
      bool lp = false;
      bootButtonUpdate(sp, lp);
      if (sp || lp) break;
      delay(20);
    }

    uiTouchPrev = false;
    setState(AppState::InputHeight);
    return;
  }
}
