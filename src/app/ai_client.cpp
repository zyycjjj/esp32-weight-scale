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

static bool extractJsonObjectField(const String &json, const char *field, String &out) {
  out = "";
  String key = String("\"") + field + "\":";
  int idx = json.indexOf(key);
  if (idx < 0) return false;
  idx += key.length();
  while (idx < (int)json.length() && json[idx] == ' ') idx++;
  if (idx >= (int)json.length() || json[idx] != '{') return false;
  int depth = 0;
  int start = idx;
  for (int i = idx; i < (int)json.length(); ++i) {
    char c = json[i];
    if (c == '{') depth++;
    else if (c == '}') {
      depth--;
      if (depth == 0) {
        out = json.substring(start, i + 1);
        return true;
      }
    }
  }
  return false;
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

  String dataObj;
  bool hasData = extractJsonObjectField(payload, "data", dataObj);
  const String &src = hasData ? dataObj : payload;

  extractJsonNumberField(src, "bmi", out.bmi);
  extractJsonStringField(src, "category", out.category);
  extractJsonStringField(src, "comment", out.comment);
  extractJsonStringField(src, "tip", out.tip);

  out.audioUrl = "";
  int ttsIdx = src.indexOf("\"tts\":");
  if (ttsIdx >= 0) {
    String tail = src.substring(ttsIdx);
    extractJsonStringField(tail, "audioUrl", out.audioUrl);
  } else {
    extractJsonStringField(src, "audioUrl", out.audioUrl);
  }

  bool hasPrint = extractJsonStringField(src, "printPayloadBase64", out.printPayloadBase64);
  Serial.printf("ai fields: catLen=%u cmtLen=%u tipLen=%u audioLen=%u printB64Len=%u\n",
                (unsigned)out.category.length(),
                (unsigned)out.comment.length(),
                (unsigned)out.tip.length(),
                (unsigned)out.audioUrl.length(),
                (unsigned)out.printPayloadBase64.length());
  if (!hasPrint) {
    int idx = src.indexOf("\"printPayloadBase64\":");
    if (idx >= 0) {
      Serial.printf("ai printPayloadBase64 not string near=%s\n", src.substring(idx, idx + 80).c_str());
    } else {
      Serial.println("ai printPayloadBase64 missing");
    }
  }
  out.ok = true;
  return true;
}

}  // namespace aiw
