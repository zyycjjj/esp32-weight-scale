#include "app/receipt_printer.h"

#include "app/base64.h"

namespace aiw {

bool printerInit(HardwareSerial &printer) {
  uint8_t cmd[] = {0x1b, 0x40};
  size_t written = printer.write(cmd, sizeof(cmd));
  printer.flush();
  return written == sizeof(cmd);
}

void printerPrintLine(HardwareSerial &printer, const char *s) {
  if (!s) return;
  printer.write((const uint8_t *)s, strlen(s));
  printer.write((uint8_t)0x0D);
  printer.write((uint8_t)0x0A);
}

void printerPrintLine(HardwareSerial &printer, const String &s) { printerPrintLine(printer, s.c_str()); }

void printerFeed(HardwareSerial &printer, uint8_t lines) {
  for (uint8_t i = 0; i < lines; ++i) {
    printer.write((uint8_t)0x0D);
    printer.write((uint8_t)0x0A);
  }
}

static bool isAsciiText(const String &s) {
  for (size_t i = 0; i < s.length(); ++i) {
    uint8_t c = (uint8_t)s[i];
    if (c < 0x20 || c > 0x7E) return false;
  }
  return true;
}

bool printerPrintPayloadBase64(HardwareSerial &printer, const String &payloadBase64) {
  if (!payloadBase64.length()) return false;
  size_t cap = base64DecodedMaxLen(payloadBase64.length());
  uint8_t *buf = (uint8_t *)malloc(cap);
  if (!buf) return false;
  size_t outLen = 0;
  bool ok = base64DecodeToBytes(payloadBase64, buf, cap, outLen);
  if (ok && outLen > 0) {
    printer.write(buf, outLen);
    printer.flush();
  }
  free(buf);
  return ok && outLen > 0;
}

void printerPrintDemo(HardwareSerial &printer, float weight) {
  printerInit(printer);
  printerPrintLine(printer, "AI Weight Scale");
  printerPrintLine(printer, "Weight:");
  printerPrintLine(printer, String(weight, 1));
  printerFeed(printer, 4);
  printer.flush();
}

void printerPrintResultEnglish(HardwareSerial &printer, float weightKg, float heightCm, float bmi, const String &category, const String &comment, const String &tip) {
  printerInit(printer);
  printerPrintLine(printer, "AI Weight Scale");
  printerPrintLine(printer, "Height(cm):");
  printerPrintLine(printer, String(heightCm, 0));
  printerPrintLine(printer, "Weight(kg):");
  printerPrintLine(printer, String(weightKg, 1));
  printerPrintLine(printer, "BMI:");
  printerPrintLine(printer, String(bmi, 1));
  if (category.length() && isAsciiText(category)) {
    printerPrintLine(printer, "Category:");
    printerPrintLine(printer, category);
  }
  if (comment.length() && isAsciiText(comment)) {
    printerPrintLine(printer, "Comment:");
    printerPrintLine(printer, comment);
  }
  if (tip.length() && isAsciiText(tip)) {
    printerPrintLine(printer, "Tip:");
    printerPrintLine(printer, tip);
  }
  printerFeed(printer, 4);
  printer.flush();
}

}  // namespace aiw
