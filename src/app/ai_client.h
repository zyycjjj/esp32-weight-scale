#pragma once

#include <Arduino.h>

namespace aiw {

struct AiWithTtsResult {
  bool ok = false;
  float bmi = 0.0f;
  String category;
  String comment;
  String tip;
  String audioUrl;
  String printPayloadBase64;
};

class AiClient {
 public:
  explicit AiClient(const char *baseUrl);
  bool getCommentWithTts(float weightKg, float heightCm, AiWithTtsResult &out);

 private:
  String baseUrl_;
  bool extractJsonStringField(const String &json, const char *field, String &out);
  bool extractJsonNumberField(const String &json, const char *field, float &out);
};

}  // namespace aiw
