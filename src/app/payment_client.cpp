#include "app/payment_client.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

namespace aiw {

PaymentClient::PaymentClient(const char *baseUrl) : baseUrl_(baseUrl ? baseUrl : "") {}

static bool isHttpsUrl(const String &url) {
  return url.startsWith("https://");
}

static String urlJoin(const String &base, const char *path) {
  if (base.endsWith("/")) return base + (path[0] == '/' ? (path + 1) : path);
  return base + (path[0] == '/' ? path : String("/") + path);
}

bool PaymentClient::extractJsonStringField(const String &json, const char *field, String &out) {
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

bool PaymentClient::extractJsonBoolField(const String &json, const char *field, bool &out) {
  String key = String("\"") + field + "\":";
  int idx = json.indexOf(key);
  if (idx < 0) return false;
  idx += key.length();
  while (idx < (int)json.length() && (json[idx] == ' ')) idx++;
  if (json.startsWith("true", idx)) {
    out = true;
    return true;
  }
  if (json.startsWith("false", idx)) {
    out = false;
    return true;
  }
  return false;
}

bool PaymentClient::create(const PaymentCreateRequest &req, PaymentCreateResponse &res) {
  String url = urlJoin(baseUrl_, "/payment/create");
  String body = "{";
  body += "\"description\":\"" + String(req.description ? req.description : "") + "\",";
  body += "\"amount\":" + String(req.amount, 2) + ",";
  body += "\"deviceId\":\"" + String(req.deviceId ? req.deviceId : "") + "\",";
  body += "\"deviceName\":\"" + String(req.deviceName ? req.deviceName : "") + "\"";
  body += "}";

  int code = -1;
  String payload;

  if (isHttpsUrl(url)) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    if (!http.begin(client, url)) {
      Serial.printf("pay create begin failed url=%s\n", url.c_str());
      return false;
    }
    http.addHeader("Content-Type", "application/json");
    code = http.POST(body);
    if (code > 0) payload = http.getString();
    http.end();
  } else {
    WiFiClient client;
    HTTPClient http;
    if (!http.begin(client, url)) {
      Serial.printf("pay create begin failed url=%s\n", url.c_str());
      return false;
    }
    http.addHeader("Content-Type", "application/json");
    code = http.POST(body);
    if (code > 0) payload = http.getString();
    http.end();
  }

  if (code < 200 || code >= 300) {
    Serial.printf("pay create http=%d\n", code);
    if (code <= 0) {
      Serial.printf("pay create error=%s\n", HTTPClient::errorToString(code).c_str());
    }
    if (payload.length()) {
      Serial.printf("pay create payload=%s\n", payload.substring(0, 200).c_str());
    }
    return false;
  }

  String codeUrl;
  String outTradeNo;
  if (!extractJsonStringField(payload, "code_url", codeUrl)) {
    Serial.println("pay create no code_url");
    String msg;
    if (extractJsonStringField(payload, "message", msg)) {
      Serial.printf("pay create message=%s\n", msg.c_str());
    } else {
      Serial.printf("pay create payload=%s\n", payload.substring(0, 200).c_str());
    }
    return false;
  }
  if (!extractJsonStringField(payload, "out_trade_no", outTradeNo)) {
    Serial.println("pay create no out_trade_no");
    String msg;
    if (extractJsonStringField(payload, "message", msg)) {
      Serial.printf("pay create message=%s\n", msg.c_str());
    } else {
      Serial.printf("pay create payload=%s\n", payload.substring(0, 200).c_str());
    }
    return false;
  }
  res.codeUrl = codeUrl;
  res.outTradeNo = outTradeNo;
  return true;
}

bool PaymentClient::query(const char *outTradeNo, PaymentQueryResponse &res) {
  String url = urlJoin(baseUrl_, "/payment/query?outTradeNo=") + String(outTradeNo ? outTradeNo : "");
  int code = -1;
  String payload;

  if (isHttpsUrl(url)) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    if (!http.begin(client, url)) return false;
    code = http.GET();
    if (code > 0) payload = http.getString();
    http.end();
  } else {
    WiFiClient client;
    HTTPClient http;
    if (!http.begin(client, url)) return false;
    code = http.GET();
    if (code > 0) payload = http.getString();
    http.end();
  }

  if (code < 200 || code >= 300) {
    Serial.printf("pay query http=%d\n", code);
    if (payload.length()) {
      Serial.printf("pay query payload=%s\n", payload.substring(0, 200).c_str());
    }
    return false;
  }

  bool success = false;
  String tradeState;
  if (!extractJsonBoolField(payload, "success", success)) {
    Serial.println("pay query no success");
    Serial.printf("pay query payload=%s\n", payload.substring(0, 200).c_str());
    return false;
  }
  extractJsonStringField(payload, "trade_state", tradeState);
  res.success = success;
  res.tradeState = tradeState;
  return true;
}

}  // namespace aiw
