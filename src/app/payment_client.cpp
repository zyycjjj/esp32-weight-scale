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
  HTTPClient http;
  String url = urlJoin(baseUrl_, "/payment/create");
  if (isHttpsUrl(url)) {
    WiFiClientSecure client;
    client.setInsecure();
    if (!http.begin(client, url)) return false;
  } else {
    if (!http.begin(url)) return false;
  }

  http.addHeader("Content-Type", "application/json");

  String body = "{";
  body += "\"description\":\"" + String(req.description ? req.description : "") + "\",";
  body += "\"amount\":" + String(req.amount, 2) + ",";
  body += "\"deviceId\":\"" + String(req.deviceId ? req.deviceId : "") + "\",";
  body += "\"deviceName\":\"" + String(req.deviceName ? req.deviceName : "") + "\"";
  body += "}";

  int code = http.POST(body);
  if (code <= 0) {
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  if (code != 200) return false;

  String codeUrl;
  String outTradeNo;
  if (!extractJsonStringField(payload, "code_url", codeUrl)) return false;
  if (!extractJsonStringField(payload, "out_trade_no", outTradeNo)) return false;
  res.codeUrl = codeUrl;
  res.outTradeNo = outTradeNo;
  return true;
}

bool PaymentClient::query(const char *outTradeNo, PaymentQueryResponse &res) {
  HTTPClient http;
  String url = urlJoin(baseUrl_, "/payment/query?outTradeNo=") + String(outTradeNo ? outTradeNo : "");
  if (isHttpsUrl(url)) {
    WiFiClientSecure client;
    client.setInsecure();
    if (!http.begin(client, url)) return false;
  } else {
    if (!http.begin(url)) return false;
  }
  int code = http.GET();
  if (code <= 0) {
    http.end();
    return false;
  }
  String payload = http.getString();
  http.end();
  if (code != 200) return false;

  bool success = false;
  String tradeState;
  if (!extractJsonBoolField(payload, "success", success)) return false;
  extractJsonStringField(payload, "trade_state", tradeState);
  res.success = success;
  res.tradeState = tradeState;
  return true;
}

}  // namespace aiw
