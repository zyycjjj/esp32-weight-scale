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
  lcd_fill_rect(20, 30, 280, 40, 0xFFFF);
  lcd_fill_rect(24, 34, 272, 32, 0x0000);
  lcd_fill_rect(20, 90, 280, 50, 0xF800);
  lcd_fill_rect(20, 160, 280, 50, 0x001F);
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

  SPI.begin(PIN_LCD_SCLK, -1, PIN_LCD_MOSI, PIN_LCD_CS);
  spi_begin_tx(10000000);

  lcd_reset_active_high();

  init_st7789_basic();
  draw_test_pattern(0x0000);

  spi_end_tx();
}

void loop() {
  delay(1000);
}
