#include "app/ai_client.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

namespace aiw {

AiClient::AiClient(const char *baseUrl) : baseUrl_(baseUrl ? baseUrl : "") {}

static bool isHttpsUrl(const String &url) { return url.startsWith("https://"); }

static String urlJoin(const String &base, const char *path) {
  if (base.endsWith("/")) return base + (path[0] == '/' ? (path + 1) : path);
  return base + (path[0] == '/' ? path : String("/") + path);
}

bool AiClient::extractJsonStringField(const String &json, const char *field, String &out) {
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

bool AiClient::extractJsonNumberField(const String &json, const char *field, float &out) {
  String key = String("\"") + field + "\":";
  int idx = json.indexOf(key);
  if (idx < 0) return false;
  idx += key.length();
  while (idx < (int)json.length() && (json[idx] == ' ')) idx++;
  int end = idx;
  while (end < (int)json.length()) {
    char c = json[end];
    if ((c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+') {
      end++;
      continue;
    }
    break;
  }
  if (end <= idx) return false;
  out = json.substring(idx, end).toFloat();
  return true;
}

bool AiClient::getCommentWithTts(float weightKg, float heightCm, AiWithTtsResult &out) {
  out = AiWithTtsResult{};
  String url = urlJoin(baseUrl_, "/api/get_ai_comment_with_tts");
  String body = "{";
  body += "\"weight\":" + String(weightKg, 1) + ",";
  body += "\"height\":" + String(heightCm, 0);
  body += "}";

  int code = -1;
  String payload;

  if (isHttpsUrl(url)) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    if (!http.begin(client, url)) return false;
    http.addHeader("Content-Type", "application/json");
    code = http.POST(body);
    if (code > 0) payload = http.getString();
    http.end();
  } else {
    WiFiClient client;
    HTTPClient http;
    if (!http.begin(client, url)) return false;
    http.addHeader("Content-Type", "application/json");
    code = http.POST(body);
    if (code > 0) payload = http.getString();
    http.end();
  }

  if (code < 200 || code >= 300) {
    Serial.printf("ai http=%d\n", code);
    if (payload.length()) Serial.printf("ai payload=%s\n", payload.substring(0, 200).c_str());
    return false;
  }

  bool success = false;
  {
    String key = "\"success\":";
    int idx = payload.indexOf(key);
    if (idx >= 0) {
      idx += key.length();
      while (idx < (int)payload.length() && payload[idx] == ' ') idx++;
      success = payload.startsWith("true", idx);
    }
  }
  if (!success) {
    Serial.printf("ai success=false payload=%s\n", payload.substring(0, 200).c_str());
    return false;
  }

  extractJsonNumberField(payload, "bmi", out.bmi);
  extractJsonStringField(payload, "category", out.category);
  extractJsonStringField(payload, "comment", out.comment);
  extractJsonStringField(payload, "tip", out.tip);
  if (!extractJsonStringField(payload, "audioUrl", out.audioUrl)) {
    out.audioUrl = "";
  }
  if (!out.audioUrl.length()) {
    int ttsIdx = payload.indexOf("\"tts\":");
    if (ttsIdx >= 0) {
      String tail = payload.substring(ttsIdx);
      extractJsonStringField(tail, "audioUrl", out.audioUrl);
    }
  }
  extractJsonStringField(payload, "printPayloadBase64", out.printPayloadBase64);
  out.ok = true;
  return true;
}

}  // namespace aiw
