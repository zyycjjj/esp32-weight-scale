#include <Arduino.h>
#include <SPI.h>

static const int PIN_LCD_MOSI = 6;
static const int PIN_LCD_SCLK = 7;
static const int PIN_LCD_CS = 5;
static const int PIN_LCD_DC = 4;
static const int PIN_LCD_RST = 48;
static const int PIN_LCD_BL_BOX = 45;
static const int PIN_LCD_BL_BOX3 = 47;

static const int LCD_W = 320;
static const int LCD_H = 240;
static const int LCD_XOFF = 0;
static const int LCD_YOFF = 0;

#ifndef HX711_DOUT_PIN
#define HX711_DOUT_PIN 1
#endif

#ifndef HX711_SCK_PIN
#define HX711_SCK_PIN 2
#endif

static int32_t hx711_offset = 0;
static float hx711_scale = 1000.0f;

static inline void spi_begin_tx(uint32_t hz) {
  SPI.beginTransaction(SPISettings(hz, MSBFIRST, SPI_MODE0));
}

static inline void spi_end_tx() {
  SPI.endTransaction();
}

static inline void lcd_cmd(uint8_t c) {
  digitalWrite(PIN_LCD_DC, LOW);
  digitalWrite(PIN_LCD_CS, LOW);
  SPI.write(c);
  digitalWrite(PIN_LCD_CS, HIGH);
}

static inline void lcd_data8(uint8_t d) {
  digitalWrite(PIN_LCD_DC, HIGH);
  digitalWrite(PIN_LCD_CS, LOW);
  SPI.write(d);
  digitalWrite(PIN_LCD_CS, HIGH);
}

static inline void lcd_data_buf(const uint8_t *buf, size_t len) {
  digitalWrite(PIN_LCD_DC, HIGH);
  digitalWrite(PIN_LCD_CS, LOW);
  SPI.writeBytes(buf, len);
  digitalWrite(PIN_LCD_CS, HIGH);
}

static void lcd_set_addr(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
  x0 = (uint16_t)(x0 + LCD_XOFF);
  x1 = (uint16_t)(x1 + LCD_XOFF);
  y0 = (uint16_t)(y0 + LCD_YOFF);
  y1 = (uint16_t)(y1 + LCD_YOFF);
  lcd_cmd(0x2A);
  lcd_data8(x0 >> 8); lcd_data8(x0 & 0xFF);
  lcd_data8(x1 >> 8); lcd_data8(x1 & 0xFF);
  lcd_cmd(0x2B);
  lcd_data8(y0 >> 8); lcd_data8(y0 & 0xFF);
  lcd_data8(y1 >> 8); lcd_data8(y1 & 0xFF);
  lcd_cmd(0x2C);
}

static void lcd_write_color(uint16_t color565, size_t pixel_count) {
  const size_t chunk_pixels = 256;
  uint8_t buf[chunk_pixels * 2];
  for (size_t i = 0; i < chunk_pixels; ++i) {
    buf[i * 2 + 0] = (uint8_t)(color565 >> 8);
    buf[i * 2 + 1] = (uint8_t)(color565 & 0xFF);
  }
  while (pixel_count) {
    size_t batch = pixel_count > chunk_pixels ? chunk_pixels : pixel_count;
    lcd_data_buf(buf, batch * 2);
    pixel_count -= batch;
  }
}

static void lcd_fill_rect(int x, int y, int w, int h, uint16_t color565) {
  if (w <= 0 || h <= 0) return;
  lcd_set_addr((uint16_t)x, (uint16_t)y, (uint16_t)(x + w - 1), (uint16_t)(y + h - 1));
  lcd_write_color(color565, (size_t)w * (size_t)h);
}

static void seg_rect(int x, int y, int w, int h, uint16_t color) {
  lcd_fill_rect(x, y, w, h, color);
}

static void draw_7seg_char(int x, int y, char ch, int scale, uint16_t on, uint16_t off) {
  if (scale < 1) scale = 1;
  const int t = scale;
  const int w = 6 * scale;
  const int h = 10 * scale;

  seg_rect(x, y, w, h, off);

  if (ch == '.') {
    seg_rect(x + w - t - 1, y + h - t - 1, t, t, on);
    return;
  }

  uint8_t segs = 0;
  if (ch >= '0' && ch <= '9') {
    static const uint8_t map[10] = {
      0b00111111, 0b00000110, 0b01011011, 0b01001111, 0b01100110,
      0b01101101, 0b01111101, 0b00000111, 0b01111111, 0b01101111
    };
    segs = map[ch - '0'];
  } else if (ch == '-') {
    segs = 0b01000000;
  } else {
    return;
  }

  if (segs & 0b00000001) seg_rect(x + t, y, w - 2 * t, t, on);
  if (segs & 0b00000010) seg_rect(x + w - t, y + t, t, (h / 2) - t, on);
  if (segs & 0b00000100) seg_rect(x + w - t, y + (h / 2), t, (h / 2) - t, on);
  if (segs & 0b00001000) seg_rect(x + t, y + h - t, w - 2 * t, t, on);
  if (segs & 0b00010000) seg_rect(x, y + (h / 2), t, (h / 2) - t, on);
  if (segs & 0b00100000) seg_rect(x, y + t, t, (h / 2) - t, on);
  if (segs & 0b01000000) seg_rect(x + t, y + (h / 2) - (t / 2), w - 2 * t, t, on);
}

static void draw_7seg_text(int x, int y, const char *s, int scale, uint16_t on, uint16_t off) {
  int cx = x;
  while (*s) {
    draw_7seg_char(cx, y, *s, scale, on, off);
    cx += 7 * scale;
    ++s;
  }
}

static bool hx711_is_ready() {
  return digitalRead(HX711_DOUT_PIN) == LOW;
}

static int32_t hx711_read_raw() {
  uint32_t start = millis();
  while (!hx711_is_ready()) {
    if (millis() - start > 200) return INT32_MIN;
    delay(1);
  }

  uint32_t value = 0;
  for (int i = 0; i < 24; ++i) {
    digitalWrite(HX711_SCK_PIN, HIGH);
    delayMicroseconds(1);
    value = (value << 1) | (uint32_t)digitalRead(HX711_DOUT_PIN);
    digitalWrite(HX711_SCK_PIN, LOW);
    delayMicroseconds(1);
  }

  digitalWrite(HX711_SCK_PIN, HIGH);
  delayMicroseconds(1);
  digitalWrite(HX711_SCK_PIN, LOW);
  delayMicroseconds(1);

  if (value & 0x800000) value |= 0xFF000000;
  return (int32_t)value;
}

static int32_t hx711_read_average(int samples) {
  if (samples < 1) samples = 1;
  int64_t sum = 0;
  int got = 0;
  for (int i = 0; i < samples; ++i) {
    int32_t v = hx711_read_raw();
    if (v == INT32_MIN) continue;
    sum += v;
    ++got;
  }
  if (got == 0) return INT32_MIN;
  return (int32_t)(sum / got);
}

static void hx711_tare(int samples) {
  int32_t v = hx711_read_average(samples);
  if (v != INT32_MIN) hx711_offset = v;
}

static void lcd_reset_active_high() {
  pinMode(PIN_LCD_RST, OUTPUT);
  digitalWrite(PIN_LCD_RST, HIGH);
  delay(20);
  digitalWrite(PIN_LCD_RST, LOW);
  delay(150);
}

static void init_st7789_basic() {
  lcd_cmd(0x11); delay(120);
  lcd_cmd(0x3A); lcd_data8(0x55);
  lcd_cmd(0x36); lcd_data8(0xC0);
  lcd_cmd(0x21);
  lcd_cmd(0x29); delay(20);
}

static void draw_test_pattern(uint16_t bg) {
  lcd_fill_rect(0, 0, LCD_W, LCD_H, bg);
  lcd_fill_rect(0, 70, LCD_W, 60, 0xF800);
  lcd_fill_rect(0, 130, LCD_W, 60, 0x001F);
  lcd_fill_rect(0, 0, LCD_W, 2, 0xFFFF);
  lcd_fill_rect(0, 0, 2, LCD_H, 0xFFFF);
  lcd_fill_rect(LCD_W - 2, 0, 2, LCD_H, 0xFFFF);
  lcd_fill_rect(0, LCD_H - 2, LCD_W, 2, 0xFFFF);
}

void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(PIN_LCD_BL_BOX, OUTPUT);
  pinMode(PIN_LCD_BL_BOX3, OUTPUT);
  digitalWrite(PIN_LCD_BL_BOX, HIGH);
  digitalWrite(PIN_LCD_BL_BOX3, HIGH);

  pinMode(PIN_LCD_DC, OUTPUT);
  pinMode(PIN_LCD_CS, OUTPUT);
  digitalWrite(PIN_LCD_CS, HIGH);

  pinMode(HX711_DOUT_PIN, INPUT);
  pinMode(HX711_SCK_PIN, OUTPUT);
  digitalWrite(HX711_SCK_PIN, LOW);

  SPI.begin(PIN_LCD_SCLK, -1, PIN_LCD_MOSI, PIN_LCD_CS);
  spi_begin_tx(10000000);

  lcd_reset_active_high();

  init_st7789_basic();
  draw_test_pattern(0xFFFF);

  spi_end_tx();

  hx711_tare(20);
}

void loop() {
  int32_t raw = hx711_read_average(5);
  if (raw == INT32_MIN) {
    delay(200);
    return;
  }

  int32_t delta = raw - hx711_offset;
  float weight = (float)delta / hx711_scale;

  char buf[32];
  snprintf(buf, sizeof(buf), "%.2f", weight);

  spi_begin_tx(10000000);
  lcd_fill_rect(10, 10, 300, 50, 0xFFFF);
  draw_7seg_text(16, 16, buf, 4, 0x0000, 0xFFFF);
  spi_end_tx();

  Serial.printf("raw=%ld offset=%ld delta=%ld weight=%.2f\n", (long)raw, (long)hx711_offset, (long)delta, weight);
  delay(200);
}
