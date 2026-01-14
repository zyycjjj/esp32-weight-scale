#pragma once

#include <Arduino.h>

namespace aiw {

bool printerInit(HardwareSerial &printer);
void printerPrintLine(HardwareSerial &printer, const char *s);
void printerPrintLine(HardwareSerial &printer, const String &s);
void printerFeed(HardwareSerial &printer, uint8_t lines);

bool printerPrintPayloadBase64(HardwareSerial &printer, const String &payloadBase64);
void printerPrintDemo(HardwareSerial &printer, float weight);
void printerPrintResultEnglish(HardwareSerial &printer, float weightKg, float heightCm, float bmi, const String &category, const String &comment, const String &tip);

}  // namespace aiw

