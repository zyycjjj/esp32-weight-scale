#include "app/touch_gt911.h"

#include <Wire.h>

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
  Wire.end();
  pinMode(sda, INPUT_PULLUP);
  pinMode(scl, INPUT_PULLUP);
  Wire.begin(sda, scl);
  Wire.setClock(100000);
  Wire.setTimeout(20);
  ready = true;
}

bool TouchGt911::readReg16(uint16_t reg, uint8_t *buf, size_t len) {
  ensureWire(sdaPin_, sclPin_, i2cReady_);
  if (!i2cReady_) return false;
  Wire.beginTransmission(addr_);
  Wire.write((uint8_t)((reg >> 8) & 0xFF));
  Wire.write((uint8_t)(reg & 0xFF));
  if (Wire.endTransmission(false) != 0) return false;
  int got = Wire.requestFrom((int)addr_, (int)len);
  if (got != (int)len) return false;
  for (size_t i = 0; i < len; ++i) {
    buf[i] = (uint8_t)Wire.read();
  }
  return true;
}

bool TouchGt911::writeReg16(uint16_t reg, const uint8_t *buf, size_t len) {
  ensureWire(sdaPin_, sclPin_, i2cReady_);
  if (!i2cReady_) return false;
  Wire.beginTransmission(addr_);
  Wire.write((uint8_t)((reg >> 8) & 0xFF));
  Wire.write((uint8_t)(reg & 0xFF));
  for (size_t i = 0; i < len; ++i) Wire.write(buf[i]);
  return Wire.endTransmission(true) == 0;
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
  auto inRange = [&](uint16_t x, uint16_t y) -> bool { return x < mx && y < my; };

  uint16_t x1 = (uint16_t)buf[1] | ((uint16_t)buf[2] << 8);
  uint16_t y1 = (uint16_t)buf[3] | ((uint16_t)buf[4] << 8);
  uint16_t x2 = (uint16_t)buf[2] | ((uint16_t)buf[1] << 8);
  uint16_t y2 = (uint16_t)buf[4] | ((uint16_t)buf[3] << 8);
  uint16_t x3 = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
  uint16_t y3 = (uint16_t)buf[2] | ((uint16_t)buf[3] << 8);
  uint16_t x4 = (uint16_t)buf[1] | ((uint16_t)buf[0] << 8);
  uint16_t y4 = (uint16_t)buf[3] | ((uint16_t)buf[2] << 8);

  uint16_t x = x1;
  uint16_t y = y1;
  if (inRange(x2, y2)) {
    x = x2;
    y = y2;
  } else if (inRange(x3, y3)) {
    x = x3;
    y = y3;
  } else if (inRange(x4, y4)) {
    x = x4;
    y = y4;
  }
  out.touching = true;
  out.x = (int)x;
  out.y = (int)y;
  uint8_t z = 0;
  writeReg16(0x814E, &z, 1);
  return true;
}

}
