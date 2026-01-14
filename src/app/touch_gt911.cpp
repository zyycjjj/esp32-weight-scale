#include "app/touch_gt911.h"

#include <Wire.h>

#include "app/i2c_bus.h"

namespace aiw {

void TouchGt911::begin(int sdaPin, int sclPin, uint8_t addr) {
  sdaPin_ = sdaPin;
  sclPin_ = sclPin;
  addr_ = addr;
  i2cReady_ = false;
  detected_ = false;
  maxX_ = 0;
  maxY_ = 0;
}

bool TouchGt911::detect() {
  return tryDetect();
}

static void ensureWire(int sda, int scl, bool &ready) {
  if (ready) return;
  if (sda < 0 || scl < 0) return;
  aiw::i2cBusInit(sda, scl, 100000);
  ready = true;
}

bool TouchGt911::readReg16(uint16_t reg, uint8_t *buf, size_t len) {
  ensureWire(sdaPin_, sclPin_, i2cReady_);
  if (!i2cReady_) return false;
  if (!aiw::i2cBusLock(20)) return false;
  Wire.beginTransmission(addr_);
  Wire.write((uint8_t)((reg >> 8) & 0xFF));
  Wire.write((uint8_t)(reg & 0xFF));
  if (Wire.endTransmission(false) != 0) {
    aiw::i2cBusUnlock();
    return false;
  }
  int got = Wire.requestFrom((int)addr_, (int)len);
  if (got != (int)len) {
    aiw::i2cBusUnlock();
    return false;
  }
  for (size_t i = 0; i < len; ++i) {
    buf[i] = (uint8_t)Wire.read();
  }
  aiw::i2cBusUnlock();
  return true;
}

bool TouchGt911::writeReg16(uint16_t reg, const uint8_t *buf, size_t len) {
  ensureWire(sdaPin_, sclPin_, i2cReady_);
  if (!i2cReady_) return false;
  if (!aiw::i2cBusLock(20)) return false;
  Wire.beginTransmission(addr_);
  Wire.write((uint8_t)((reg >> 8) & 0xFF));
  Wire.write((uint8_t)(reg & 0xFF));
  for (size_t i = 0; i < len; ++i) Wire.write(buf[i]);
  bool ok = Wire.endTransmission(true) == 0;
  aiw::i2cBusUnlock();
  return ok;
}

bool TouchGt911::tryDetect() {
  if (detected_) return true;
  if (sdaPin_ < 0 || sclPin_ < 0) return false;

  const uint8_t addrs[] = {addr_, 0x5D, 0x14};
  for (size_t i = 0; i < sizeof(addrs) / sizeof(addrs[0]); ++i) {
    uint8_t a = addrs[i];
    if (a == 0) continue;
    addr_ = a;
    uint8_t id[4] = {0};
    if (readReg16(0x8140, id, sizeof(id))) {
      detected_ = true;
      uint8_t xy[4] = {0};
      if (readReg16(0x8048, xy, sizeof(xy))) {
        maxX_ = (uint16_t)xy[0] | ((uint16_t)xy[1] << 8);
        maxY_ = (uint16_t)xy[2] | ((uint16_t)xy[3] << 8);
      }
      return true;
    }
  }
  return false;
}

bool TouchGt911::read(TouchPoint &out) {
  out = TouchPoint{};
  if (!tryDetect()) return false;
  uint8_t status = 0;
  if (!readReg16(0x814E, &status, 1)) return false;
  if ((status & 0x80u) == 0) return true;
  uint8_t count = (uint8_t)(status & 0x0Fu);
  if (count == 0) {
    uint8_t z = 0;
    writeReg16(0x814E, &z, 1);
    static bool wasTouching = false;
    wasTouching = false;
    static bool hasLast = false;
    hasLast = false;
    return true;
  }
  if (count > 1) count = 1;
  uint8_t buf[8];
  if (!readReg16(0x8150, buf, sizeof(buf))) {
    uint8_t z = 0;
    writeReg16(0x814E, &z, 1);
    return false;
  }

  uint16_t mx = maxX_ ? maxX_ : 0xFFFFu;
  uint16_t my = maxY_ ? maxY_ : 0xFFFFu;

  struct Candidate {
    uint16_t x = 0;
    uint16_t y = 0;
    uint16_t size = 0;
    uint8_t id = 0;
    bool ok = false;
  };

  auto parseAt = [&](int off) -> Candidate {
    Candidate c;
    if (off < 0 || off + 6 >= (int)sizeof(buf)) return c;
    c.id = buf[off + 0];
    c.x = (uint16_t)buf[off + 1] | ((uint16_t)buf[off + 2] << 8);
    c.y = (uint16_t)buf[off + 3] | ((uint16_t)buf[off + 4] << 8);
    c.size = (uint16_t)buf[off + 5] | ((uint16_t)buf[off + 6] << 8);
    bool idOk = c.id < 0x80;
    bool xyOk = c.x < mx && c.y < my;
    bool sizeOk = c.size > 0 && c.size < 4096;
    c.ok = idOk && xyOk && sizeOk;
    return c;
  };

  Candidate c0 = parseAt(0);
  Candidate c1 = parseAt(1);
  Candidate c2;
  c2.ok = false;
  if (!c0.ok && !c1.ok) {
    c2 = parseAt(2);
  }

  static bool wasTouching = false;
  static bool hasLast = false;
  static uint16_t lastX = 0;
  static uint16_t lastY = 0;
  if (!wasTouching) hasLast = false;
  wasTouching = true;

  Candidate best;
  uint32_t bestScore = 0xFFFFFFFFu;
  Candidate candidates[] = {c0, c1, c2};
  for (int i = 0; i < (int)(sizeof(candidates) / sizeof(candidates[0])); ++i) {
    if (!candidates[i].ok) continue;
    if (!hasLast) {
      best = candidates[i];
      bestScore = 0;
      break;
    }
    int dx = (int)candidates[i].x - (int)lastX;
    int dy = (int)candidates[i].y - (int)lastY;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    uint32_t score = (uint32_t)dx * (uint32_t)dx + (uint32_t)dy * (uint32_t)dy;
    if (score < bestScore) {
      bestScore = score;
      best = candidates[i];
    }
  }
  if (!best.ok) {
    best = c0.ok ? c0 : c1;
  }
  uint16_t x = best.x;
  uint16_t y = best.y;
  if (maxX_ && x >= maxX_) x = (uint16_t)(maxX_ - 1);
  if (maxY_ && y >= maxY_) y = (uint16_t)(maxY_ - 1);

  out.touching = true;
  out.x = (int)x;
  out.y = (int)y;
  hasLast = true;
  lastX = x;
  lastY = y;
  uint8_t z = 0;
  writeReg16(0x814E, &z, 1);
  return true;
}

}
