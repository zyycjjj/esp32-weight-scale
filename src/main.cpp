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
#include "app/i2c_bus.h"
#include "app/receipt_printer.h"

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

static const char kZhSelectHeight[] = "\xE9\x80\x89\xE6\x8B\xA9\xE8\xBA\xAB\xE9\xAB\x98";
static const char kZhScanPay[] = "\xE6\x89\xAB\xE7\xA0\x81\xE6\x94\xAF\xE4\xBB\x98";

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

static constexpr int HeightSliderX = 30;
static constexpr int HeightSliderY = 132;
static constexpr int HeightSliderW = 260;
static constexpr int HeightSliderH = 26;

static constexpr int HeightBtnY = 164;
static constexpr int HeightBtnH = 68;
static constexpr int HeightLeftX = 14;
static constexpr int HeightLeftW = 86;
static constexpr int HeightNextX = 108;
static constexpr int HeightNextW = 104;
static constexpr int HeightRightX = 220;
static constexpr int HeightRightW = 86;

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
  int x = aiw::DisplaySt7789::Width - 24;
  int y = 6;
  display.fillRect(x, y, 18, 18, ColorWhite);
  uint16_t c = wifi.isConnected() ? ColorGreen : ColorRed;
  display.fillRect(x + 2, y + 12, 3, 4, c);
  display.fillRect(x + 7, y + 9, 3, 7, c);
  display.fillRect(x + 12, y + 6, 3, 10, c);
  display.endWrite();
}

static void fillRoundRect(int x, int y, int w, int h, int r, uint16_t color) {
  if (w <= 0 || h <= 0) return;
  if (r < 1) {
    display.fillRect(x, y, w, h, color);
    return;
  }
  if (r * 2 > w) r = w / 2;
  if (r * 2 > h) r = h / 2;
  display.fillRect(x + r, y, w - 2 * r, h, color);
  display.fillRect(x, y + r, r, h - 2 * r, color);
  display.fillRect(x + w - r, y + r, r, h - 2 * r, color);
  for (int dy = 0; dy < r; ++dy) {
    float fy = (float)dy + 0.5f;
    float fr = (float)r;
    int dx = (int)floorf(sqrtf(fr * fr - fy * fy));
    int sx = r - dx;
    int ww = w - 2 * sx;
    if (ww > 0) {
      display.fillRect(x + sx, y + dy, ww, 1, color);
      display.fillRect(x + sx, y + h - 1 - dy, ww, 1, color);
    }
  }
}

static void drawButton(int x, int y, int w, int h, uint16_t bg, const char *label) {
  int r = 10;
  uint16_t border = 0x7BEF;
  fillRoundRect(x, y, w, h, r, border);
  if (w > 4 && h > 4) {
    fillRoundRect(x + 2, y + 2, w - 4, h - 4, r - 2, bg);
    display.fillRect(x + 4, y + 4, w - 8, 2, 0xFFFF);
    display.fillRect(x + 4, y + h - 6, w - 8, 2, 0xAD55);
  }
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
  aiw::drawZhText28(display, 12, 8, kZhScanPay, ColorBlack, ColorWhite);
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
  static bool prevTouching = false;
  static bool hasStable = false;
  static int stableX = 0;
  static int stableY = 0;
  static uint32_t stableMs = 0;
  static uint8_t stableCount = 0;
  static uint8_t histN = 0;
  static int histX[5];
  static int histY[5];
  static uint8_t jumpPending = 0;
  static int jumpX = 0;
  static int jumpY = 0;

  aiw::TouchPoint p;
  if (!touchScreen.read(p)) return;
  if (!p.touching) {
    prevTouching = false;
    hasStable = false;
    stableCount = 0;
    histN = 0;
    jumpPending = 0;
    return;
  }
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

  uint32_t nowMs = millis();
  if (histN < 5) {
    histX[histN] = tx;
    histY[histN] = ty;
    histN++;
  } else {
    for (int i = 0; i < 4; ++i) {
      histX[i] = histX[i + 1];
      histY[i] = histY[i + 1];
    }
    histX[4] = tx;
    histY[4] = ty;
  }
  auto median5 = [](const int *a, uint8_t n) -> int {
    int b[5];
    for (uint8_t i = 0; i < n; ++i) b[i] = a[i];
    for (uint8_t i = 0; i < n; ++i) {
      for (uint8_t j = (uint8_t)(i + 1); j < n; ++j) {
        if (b[j] < b[i]) {
          int t = b[i];
          b[i] = b[j];
          b[j] = t;
        }
      }
    }
    return b[n / 2];
  };
  int fx = median5(histX, histN);
  int fy = median5(histY, histN);
  tx = fx;
  ty = fy;

  if (!prevTouching) {
    prevTouching = true;
    hasStable = true;
    stableX = tx;
    stableY = ty;
    stableMs = nowMs;
    stableCount = 1;
    jumpPending = 0;
  } else if (!hasStable) {
    hasStable = true;
    stableX = tx;
    stableY = ty;
    stableMs = nowMs;
    stableCount = 1;
    jumpPending = 0;
  } else {
    int dx = tx - stableX;
    int dy = ty - stableY;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    if (stableCount < 2) {
      stableX = (stableX + tx) / 2;
      stableY = (stableY + ty) / 2;
      stableMs = nowMs;
      stableCount = 2;
      jumpPending = 0;
    } else {
      int jumpTh = 28;
      if (dx > jumpTh || dy > jumpTh) {
        if (!jumpPending) {
          jumpPending = 1;
          jumpX = tx;
          jumpY = ty;
          touching = true;
          x = stableX;
          y = stableY;
          return;
        }
        int jdx = tx - jumpX;
        int jdy = ty - jumpY;
        if (jdx < 0) jdx = -jdx;
        if (jdy < 0) jdy = -jdy;
        if (jdx <= 18 && jdy <= 18) {
          stableX = (stableX + tx) / 2;
          stableY = (stableY + ty) / 2;
          stableMs = nowMs;
        }
        jumpX = tx;
        jumpY = ty;
        jumpPending = 1;
        touching = true;
        x = stableX;
        y = stableY;
        return;
      }
      jumpPending = 0;
      stableX = (stableX * 7 + tx) / 8;
      stableY = (stableY * 7 + ty) / 8;
      stableMs = nowMs;
    }
  }

  touching = true;
  x = stableX;
  y = stableY;
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
    bool isTap = dur <= 650 && dx <= 45 && dy <= 45;
    bool in = ((lx >= rx && lx < rx + rw && ly >= ry && ly < ry + rh) || (sx >= rx && sx < rx + rw && sy >= ry && sy < ry + rh));
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
    bool isTap = dur <= 650 && dx <= 45 && dy <= 45;
    if (isTap) {
      tapped = true;
      tapX = (sx + lx) / 2;
      tapY = (sy + ly) / 2;
    }
  }
  prev = touching;
  return tapped;
}

struct TouchHoldState {
  bool in = false;
  bool fired = false;
  uint32_t enterMs = 0;
};

static bool touchHoldInRect(bool touching, int x, int y, int rx, int ry, int rw, int rh, int pad, uint32_t nowMs, uint32_t holdMs, TouchHoldState &st) {
  if (!touching) {
    st.in = false;
    st.fired = false;
    st.enterMs = 0;
    return false;
  }
  bool inside = x >= rx - pad && x < rx + rw + pad && y >= ry - pad && y < ry + rh + pad;
  if (inside && !st.in) {
    st.enterMs = nowMs;
    st.fired = false;
  }
  st.in = inside;
  if (inside && !st.fired && (nowMs - st.enterMs) >= holdMs) {
    st.fired = true;
    return true;
  }
  return false;
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
  (void)color;
}

static void drawHeightPicker() {
  char mid[8];
  snprintf(mid, sizeof(mid), "%d", currentHeightCm);

  display.beginWrite();
  display.clear(ColorWhite);
  display.drawBorder(ColorBlack, 2);

  aiw::drawZhText28(display, 12, 8, kZhSelectHeight, ColorBlack, ColorWhite);
  display.fillRect(0, 40, aiw::DisplaySt7789::Width, 1, ColorGray);

  sevenSeg.drawText(108, 52, mid, 5, ColorBlack, ColorWhite);

  display.fillRect(HeightSliderX, HeightSliderY, HeightSliderW, HeightSliderH, 0xEF7D);
  display.fillRect(HeightSliderX, HeightSliderY + HeightSliderH - 2, HeightSliderW, 2, 0xC618);
  display.fillRect(HeightSliderX, HeightSliderY, HeightSliderW, 2, 0xFFFF);
  aiw::drawText5x7(display, HeightSliderX - 2, HeightSliderY - 12, "120", ColorGray, ColorWhite, 2);
  aiw::drawText5x7(display, HeightSliderX + HeightSliderW - 2 - 3 * 12, HeightSliderY - 12, "220", ColorGray, ColorWhite, 2);
  int knobX = HeightSliderX + (currentHeightCm - 120) * (HeightSliderW - 14) / 100;
  fillRoundRect(knobX, HeightSliderY - 4, 14, HeightSliderH + 8, 6, 0x5ACB);
  fillRoundRect(knobX + 2, HeightSliderY - 2, 10, HeightSliderH + 4, 5, 0xE7FF);

  drawButton(HeightLeftX, HeightBtnY, HeightLeftW, HeightBtnH, 0xF7DE, "<");

  drawButton(HeightNextX, HeightBtnY, HeightNextW, HeightBtnH, 0xE7FF, "NEXT");

  drawButton(HeightRightX, HeightBtnY, HeightRightW, HeightBtnH, 0xF7DE, ">");

  display.endWrite();
  drawWifiStatus();
  drawStatusBar(ColorBlue);
}

static void updateHeightPickerValueOnly() {
  char mid[8];
  snprintf(mid, sizeof(mid), "%d", currentHeightCm);
  display.beginWrite();
  sevenSeg.clearRect(4, 40, aiw::DisplaySt7789::Width - 8, 92, ColorWhite);
  sevenSeg.drawText(108, 52, mid, 5, ColorBlack, ColorWhite);
  display.fillRect(HeightSliderX, HeightSliderY, HeightSliderW, HeightSliderH, 0xEF7D);
  display.fillRect(HeightSliderX, HeightSliderY + HeightSliderH - 2, HeightSliderW, 2, 0xC618);
  display.fillRect(HeightSliderX, HeightSliderY, HeightSliderW, 2, 0xFFFF);
  aiw::drawText5x7(display, HeightSliderX - 2, HeightSliderY - 12, "120", ColorGray, ColorWhite, 2);
  aiw::drawText5x7(display, HeightSliderX + HeightSliderW - 2 - 3 * 12, HeightSliderY - 12, "220", ColorGray, ColorWhite, 2);
  int knobX = HeightSliderX + (currentHeightCm - 120) * (HeightSliderW - 14) / 100;
  fillRoundRect(knobX, HeightSliderY - 4, 14, HeightSliderH + 8, 6, 0x5ACB);
  fillRoundRect(knobX + 2, HeightSliderY - 2, 10, HeightSliderH + 4, 5, 0xE7FF);
  display.endWrite();
  drawWifiStatus();
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

static void printerSelfTest() {
  aiw::printerInit(printerSerial);
  uint8_t cmd[] = {0x12, 0x54};
  printerSerial.write(cmd, sizeof(cmd));
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

      aiw::printerInit(printerSerial);
      String line = String("TEST tx=") + printerTxPin + " rx=" + printerRxPin + " baud=" + printerBaud;
      aiw::printerPrintLine(printerSerial, line);
      aiw::printerPrintLine(printerSerial, "If you can read this, stop scan.");
      aiw::printerFeed(printerSerial, 3);
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
  aiw::i2cBusInit(aiw::config::I2cSdaPin, aiw::config::I2cSclPin, 100000);

  display.begin();
  aiw::setZhRenderMode(3);
  drawUiFrame();
  drawHeightPicker();
  uiDirty = false;
  heightTouchPrev = false;

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
  if (!touchOk) {
    aiw::i2cBusInit(aiw::config::I2cSdaPin, aiw::config::I2cSclPin, 400000);
    touchScreen.begin(aiw::config::I2cSdaPin, aiw::config::I2cSclPin, 0);
    touchOk = touchScreen.detect();
  }
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
  if (touchRawLogEnabled) {
    aiw::TouchPoint p;
    bool ok = touchScreen.read(p);
    if (ok && p.touching) {
      uint32_t now = millis();
      if (now - lastTouchLogMs > 200) {
        lastTouchLogMs = now;
        Serial.printf("touch raw x=%d y=%d\n", p.x, p.y);
      }
    }
  }

  while (Serial.available() > 0) {
    int c = Serial.read();
    if (c == 'p') {
      touchRawLogEnabled = !touchRawLogEnabled;
      Serial.printf("touch raw log=%d\n", touchRawLogEnabled ? 1 : 0);
    }
    if (c == 'm') {
      touchMapMode = (uint8_t)((touchMapMode + 1) & 0x07u);
      Serial.printf("touch map mode=%u (swap=%u mx=%u my=%u)\n", (unsigned)touchMapMode, (unsigned)((touchMapMode & 0x01u) ? 1 : 0), (unsigned)((touchMapMode & 0x02u) ? 1 : 0), (unsigned)((touchMapMode & 0x04u) ? 1 : 0));
      uiDirty = true;
      if (state == AppState::InputHeight) drawHeightPicker();
    }
    if (c == 'Z') {
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
    delay(10);
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
      aiw::printerFeed(printerSerial, 2);
    }
    if (c == 'P') {
      Serial.println("printer: demo");
      aiw::printerPrintDemo(printerSerial, lastShownWeight);
    }
    if (c == 'f' || c == 'F') {
      Serial.println("printer: feed");
      aiw::printerFeed(printerSerial, 6);
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
    if (c == 'z') {
      printerAutoScan();
    }
    if (c == 'q' || c == 'Q') {
      Serial.printf("force pay: weight=%.2f height=%d\n", lastShownWeight, currentHeightCm);
      lastStableWeight = lastShownWeight;
      drawStatusBar(ColorBlue);
      setState(AppState::CreatingPayment);
    }
    if (c == 'o' || c == 'O') {
      Serial.println("gacha: trigger");
      gacha.trigger();
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
    static uint8_t heightTouchFrames = 0;
    if (touching) {
      if (heightTouchFrames < 255) heightTouchFrames++;
    } else {
      heightTouchFrames = 0;
    }
    if (touching) {
      if (heightTouchFrames >= 2) {
        int px = tx - 1;
        int py = ty - 1;
        if (px < 0) px = 0;
        if (py < 0) py = 0;
        int pw = 3;
        int ph = 3;
        if (px + pw > aiw::DisplaySt7789::Width) pw = aiw::DisplaySt7789::Width - px;
        if (py + ph > aiw::DisplaySt7789::Height) ph = aiw::DisplaySt7789::Height - py;
        display.beginWrite();
        display.fillRect(px, py, pw, ph, ColorBlack);
        display.endWrite();
      }
    }

    uint32_t now = millis();
    bool inSlider = touching && (tx >= HeightSliderX && tx < HeightSliderX + HeightSliderW && ty >= HeightSliderY - 24 && ty < HeightSliderY + HeightSliderH + 24);
    static bool heightTouchLock = false;
    static int heightLockX = 0;
    static int heightLockY = 0;
    static int heightLockW = 0;
    static int heightLockH = 0;
    if (touching && !heightTouchPrev) {
      heightTouchStartX = tx;
      heightTouchStartY = ty;
      heightTouchLastX = tx;
      heightTouchLastY = ty;
      heightTouchStartMs = now;
      heightTouchStartZone = 0;
      heightTouchLock = false;
      heightLockX = 0;
      heightLockY = 0;
      heightLockW = 0;
      heightLockH = 0;
    }
    if (touching && heightTouchFrames >= 2 && heightTouchStartZone == 0) {
      if (inSlider) {
        heightTouchStartZone = 4;
      } else if (tx >= 70 && tx < 250 && ty >= 46 && ty < HeightSliderY - 6) {
        heightTouchStartZone = 5;
      }
      int padX = 10;
      int padY = 10;
      if (ty >= HeightBtnY - padY && ty < HeightBtnY + HeightBtnH + padY) {
        if (tx >= HeightLeftX - padX && tx < HeightLeftX + HeightLeftW + padX) heightTouchStartZone = 1;
        else if (tx >= HeightNextX - padX && tx < HeightNextX + HeightNextW + padX) heightTouchStartZone = 2;
        else if (tx >= HeightRightX - padX && tx < HeightRightX + HeightRightW + padX) heightTouchStartZone = 3;
      }
      if (heightTouchStartZone == 1) {
        heightTouchLock = true;
        heightLockX = HeightLeftX;
        heightLockY = HeightBtnY;
        heightLockW = HeightLeftW;
        heightLockH = HeightBtnH;
      } else if (heightTouchStartZone == 2) {
        heightTouchLock = true;
        heightLockX = HeightNextX;
        heightLockY = HeightBtnY;
        heightLockW = HeightNextW;
        heightLockH = HeightBtnH;
      } else if (heightTouchStartZone == 3) {
        heightTouchLock = true;
        heightLockX = HeightRightX;
        heightLockY = HeightBtnY;
        heightLockW = HeightRightW;
        heightLockH = HeightBtnH;
      }
    }
    if (touching) {
      if (heightTouchLock) {
        int pad = 30;
        bool inPad = tx >= heightLockX - pad && tx < heightLockX + heightLockW + pad && ty >= heightLockY - pad && ty < heightLockY + heightLockH + pad;
        if (!inPad) {
          tx = heightTouchLastX;
          ty = heightTouchLastY;
        }
      }
      heightTouchLastX = tx;
      heightTouchLastY = ty;
      if (inSlider) {
        if (now - lastHeightBtnMs > 25) {
          int pos = tx - HeightSliderX;
          if (pos < 0) pos = 0;
          if (pos > HeightSliderW - 1) pos = HeightSliderW - 1;
          int nh = 120 + (pos * 100) / (HeightSliderW - 1);
          if (nh < 120) nh = 120;
          if (nh > 220) nh = 220;
          if (nh != currentHeightCm) {
            currentHeightCm = nh;
            updateHeightPickerValueOnly();
            lastHeightBtnMs = now;
          }
        }
      }
    }
    bool heightChanged = false;
    if (!touching && heightTouchPrev) {
      int dx = heightTouchLastX - heightTouchStartX;
      int dy = heightTouchLastY - heightTouchStartY;
      if (dx < 0) dx = -dx;
      if (dy < 0) dy = -dy;
      int rawDx = heightTouchLastX - heightTouchStartX;
      int rawDy = heightTouchLastY - heightTouchStartY;
      uint32_t dur = now - heightTouchStartMs;

      bool isTap = dur <= 650 && dx <= 45 && dy <= 45;
      bool isSwipe = dur <= 700 && ((dx >= 60 && dy <= 35) || (dy >= 60 && dx <= 35));

      if (isSwipe) {
        if (dy >= 60 && dx <= 35 && heightTouchStartZone == 5) {
          if (rawDy < 0) currentHeightCm++;
          else currentHeightCm--;
        } else {
          if (rawDx > 0) currentHeightCm++;
          else currentHeightCm--;
        }
        if (currentHeightCm > 220) currentHeightCm = 220;
        if (currentHeightCm < 120) currentHeightCm = 120;
        heightChanged = true;
      } else if (isTap) {
        bool tapInSlider = (heightTouchStartZone == 4) || (heightTouchLastX >= HeightSliderX && heightTouchLastX < HeightSliderX + HeightSliderW && heightTouchLastY >= HeightSliderY - 24 && heightTouchLastY < HeightSliderY + HeightSliderH + 24);
        if (tapInSlider) {
          int pos = heightTouchLastX - HeightSliderX;
          if (pos < 0) pos = 0;
          if (pos > HeightSliderW - 1) pos = HeightSliderW - 1;
          int nh = 120 + (pos * 100) / (HeightSliderW - 1);
          if (nh < 120) nh = 120;
          if (nh > 220) nh = 220;
          if (nh != currentHeightCm) {
            currentHeightCm = nh;
            heightChanged = true;
          }
        } else if (heightTouchStartZone == 1) {
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
        } else if (heightTouchStartZone == 5) {
          currentHeightCm++;
          if (currentHeightCm > 220) currentHeightCm = 120;
          heightChanged = true;
        }
      }
    }
    heightTouchPrev = touching;

    bool shortPress = false;
    bool longPress = false;
    touchBtn.update(shortPress, longPress);
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
      updateHeightPickerValueOnly();
    }
    delay(5);
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
    bool shortPress = false;
    bool longPress = false;
    touchBtn.update(shortPress, longPress);
    if (shortPress) {
      tryTareNow();
      stableHoldStartMs = 0;
    }
    if (longPress) {
      stableHoldStartMs = 0;
      heightTouchPrev = false;
      setState(AppState::InputHeight);
      delay(10);
      return;
    }
    static TouchHoldState weighHoldTare;
    static TouchHoldState weighHoldBack;
    bool holdTare = touchHoldInRect(touching, tx, ty, WeighTareX, WeighBtnY, WeighTareW, WeighBtnH, 18, nowTap, 120, weighHoldTare);
    bool holdBack = touchHoldInRect(touching, tx, ty, WeighBackX, WeighBtnY, WeighBackW, WeighBtnH, 18, nowTap, 120, weighHoldBack);
    if (holdTare) {
      tryTareNow();
      stableHoldStartMs = 0;
    } else if (holdBack) {
      stableHoldStartMs = 0;
      heightTouchPrev = false;
      setState(AppState::InputHeight);
      delay(10);
      return;
    }
    bool tapped = touchTapEvent(touching, tx, ty, nowTap, uiTouchPrev, uiTouchStartX, uiTouchStartY, uiTouchLastX, uiTouchLastY, uiTouchStartMs, tapX, tapY);
    if (tapped) {
      auto inRect = [&](int px, int py, int rx, int ry, int rw, int rh) -> bool { return px >= rx && px < rx + rw && py >= ry && py < ry + rh; };
      auto inRectPad = [&](int px, int py, int rx, int ry, int rw, int rh, int pad) -> bool { return px >= rx - pad && px < rx + rw + pad && py >= ry - pad && py < ry + rh + pad; };
      bool inTare = inRectPad(tapX, tapY, WeighTareX, WeighBtnY, WeighTareW, WeighBtnH, 14) || inRectPad(uiTouchStartX, uiTouchStartY, WeighTareX, WeighBtnY, WeighTareW, WeighBtnH, 14);
      bool inBack = inRectPad(tapX, tapY, WeighBackX, WeighBtnY, WeighBackW, WeighBtnH, 14) || inRectPad(uiTouchStartX, uiTouchStartY, WeighBackX, WeighBtnY, WeighBackW, WeighBtnH, 14);
      if (inTare) {
          tryTareNow();
          stableHoldStartMs = 0;
      } else if (inBack) {
        stableHoldStartMs = 0;
        heightTouchPrev = false;
        setState(AppState::InputHeight);
        delay(10);
        return;
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
      bool shortPress = false;
      bool longPress = false;
      touchBtn.update(shortPress, longPress);
      if (shortPress || longPress) {
        setState(AppState::Weighing);
        delay(10);
        return;
      }
      static TouchHoldState payHoldCancel;
      bool holdCancel = touchHoldInRect(touching, tx, ty, PayCancelX, PayCancelY, PayCancelW, PayCancelH, 18, nowTap, 120, payHoldCancel);
      if (holdCancel) {
        setState(AppState::Weighing);
        delay(10);
        return;
      }
      bool tapped = touchTapEvent(touching, tx, ty, nowTap, uiTouchPrev, uiTouchStartX, uiTouchStartY, uiTouchLastX, uiTouchLastY, uiTouchStartMs, tapX, tapY);
      if (tapped) {
        auto inRect = [&](int px, int py, int rx, int ry, int rw, int rh) -> bool { return px >= rx && px < rx + rw && py >= ry && py < ry + rh; };
        auto inRectPad = [&](int px, int py, int rx, int ry, int rw, int rh, int pad) -> bool { return px >= rx - pad && px < rx + rw + pad && py >= ry - pad && py < ry + rh + pad; };
        bool inCancel = inRectPad(tapX, tapY, PayCancelX, PayCancelY, PayCancelW, PayCancelH, 14) || inRectPad(uiTouchStartX, uiTouchStartY, PayCancelX, PayCancelY, PayCancelW, PayCancelH, 14);
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
      gacha.trigger();
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
        printed = aiw::printerPrintPayloadBase64(printerSerial, rewardAi.printPayloadBase64);
      }
      if (!printed) {
        aiw::printerPrintResultEnglish(printerSerial, lastStableWeight, lastInputHeightCm, rewardAi.bmi, rewardAi.category, rewardAi.comment, rewardAi.tip);
      }
      Serial.println("printer: print result done");
      uint32_t waitStart = millis();
      while (audioStarted && audioPlayer.isPlaying() && (millis() - waitStart < 25000)) {
        delay(10);
      }
    } else {
      Serial.println("printer: print fallback start");
      aiw::printerPrintResultEnglish(printerSerial, lastStableWeight, lastInputHeightCm, 0.0f, "", "", "");
      Serial.println("printer: print fallback done");
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
      static TouchHoldState paidHoldRestart;
      bool holdRestart = touchHoldInRect(touching, tx, ty, 90, FooterY, 140, FooterH, 18, nowTap, 120, paidHoldRestart);
      if (holdRestart) break;
      bool tapped = touchTapEvent(touching, tx, ty, nowTap, uiTouchPrev, uiTouchStartX, uiTouchStartY, uiTouchLastX, uiTouchLastY, uiTouchStartMs, tapX, tapY);
      if (tapped) {
        auto inRect = [&](int px, int py, int rx, int ry, int rw, int rh) -> bool { return px >= rx && px < rx + rw && py >= ry && py < ry + rh; };
        auto inRectPad = [&](int px, int py, int rx, int ry, int rw, int rh, int pad) -> bool { return px >= rx - pad && px < rx + rw + pad && py >= ry - pad && py < ry + rh + pad; };
        bool inRestart = inRectPad(tapX, tapY, 90, FooterY, 140, FooterH, 14) || inRectPad(uiTouchStartX, uiTouchStartY, 90, FooterY, 140, FooterH, 14);
        if (inRestart) break;
      }
      bool sp = false;
      bool lp = false;
      touchBtn.update(sp, lp);
      if (sp || lp) break;
      delay(10);
    }

    uiTouchPrev = false;
    setState(AppState::InputHeight);
    return;
  }
}
