#include "app/display_st7789.h"

namespace aiw {

static constexpr int Xoff = 0;
static constexpr int Yoff = 0;
static constexpr uint32_t SpiHz = 10000000;

DisplaySt7789::DisplaySt7789(DisplayPins pins) : pins_(pins) {}

void DisplaySt7789::begin() {
  pinMode(pins_.blBox, OUTPUT);
  pinMode(pins_.blBox3, OUTPUT);
  digitalWrite(pins_.blBox, HIGH);
  digitalWrite(pins_.blBox3, HIGH);

  pinMode(pins_.dc, OUTPUT);
  pinMode(pins_.cs, OUTPUT);
  digitalWrite(pins_.cs, HIGH);

  SPI.begin(pins_.sclk, -1, pins_.mosi, pins_.cs);

  beginWrite();
  resetActiveHigh();
  initN_C0();
  endWrite();
}

void DisplaySt7789::beginWrite() {
  if (writing_) return;
  SPI.beginTransaction(SPISettings(SpiHz, MSBFIRST, SPI_MODE0));
  writing_ = true;
}

void DisplaySt7789::endWrite() {
  if (!writing_) return;
  SPI.endTransaction();
  writing_ = false;
}

void DisplaySt7789::cmd(uint8_t c) {
  digitalWrite(pins_.dc, LOW);
  digitalWrite(pins_.cs, LOW);
  SPI.write(c);
  digitalWrite(pins_.cs, HIGH);
}

void DisplaySt7789::data8(uint8_t d) {
  digitalWrite(pins_.dc, HIGH);
  digitalWrite(pins_.cs, LOW);
  SPI.write(d);
  digitalWrite(pins_.cs, HIGH);
}

void DisplaySt7789::dataBuf(const uint8_t *buf, size_t len) {
  digitalWrite(pins_.dc, HIGH);
  digitalWrite(pins_.cs, LOW);
  SPI.writeBytes(buf, len);
  digitalWrite(pins_.cs, HIGH);
}

void DisplaySt7789::setAddr(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
  x0 = (uint16_t)(x0 + Xoff);
  x1 = (uint16_t)(x1 + Xoff);
  y0 = (uint16_t)(y0 + Yoff);
  y1 = (uint16_t)(y1 + Yoff);

  cmd(0x2A);
  data8(x0 >> 8); data8(x0 & 0xFF);
  data8(x1 >> 8); data8(x1 & 0xFF);
  cmd(0x2B);
  data8(y0 >> 8); data8(y0 & 0xFF);
  data8(y1 >> 8); data8(y1 & 0xFF);
  cmd(0x2C);
}

void DisplaySt7789::writeColor(uint16_t color565, size_t pixelCount) {
  const size_t chunkPixels = 256;
  uint8_t buf[chunkPixels * 2];
  for (size_t i = 0; i < chunkPixels; ++i) {
    buf[i * 2 + 0] = (uint8_t)(color565 >> 8);
    buf[i * 2 + 1] = (uint8_t)(color565 & 0xFF);
  }
  while (pixelCount) {
    size_t batch = pixelCount > chunkPixels ? chunkPixels : pixelCount;
    dataBuf(buf, batch * 2);
    pixelCount -= batch;
  }
}

void DisplaySt7789::fillRect(int x, int y, int w, int h, uint16_t color565) {
  if (w <= 0 || h <= 0) return;
  setAddr((uint16_t)x, (uint16_t)y, (uint16_t)(x + w - 1), (uint16_t)(y + h - 1));
  writeColor(color565, (size_t)w * (size_t)h);
}

void DisplaySt7789::clear(uint16_t color565) {
  fillRect(0, 0, Width, Height, color565);
}

void DisplaySt7789::drawBorder(uint16_t color565, int thickness) {
  if (thickness < 1) thickness = 1;
  fillRect(0, 0, Width, thickness, color565);
  fillRect(0, 0, thickness, Height, color565);
  fillRect(Width - thickness, 0, thickness, Height, color565);
  fillRect(0, Height - thickness, Width, thickness, color565);
}

void DisplaySt7789::resetActiveHigh() {
  pinMode(pins_.rst, OUTPUT);
  digitalWrite(pins_.rst, HIGH);
  delay(20);
  digitalWrite(pins_.rst, LOW);
  delay(150);
}

void DisplaySt7789::initN_C0() {
  cmd(0x11);
  delay(120);
  cmd(0x3A);
  data8(0x55);
  cmd(0x36);
  data8(0xC0);
  cmd(0x21);
  cmd(0x29);
  delay(20);
}

}  // namespace aiw

