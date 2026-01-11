#include "app/qr_client.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

namespace aiw {

static bool isHttpsUrl(const String &url) {
  return url.startsWith("https://");
}

static String urlJoin(const String &base, const char *path) {
  if (base.endsWith("/")) return base + (path[0] == '/' ? (path + 1) : path);
  return base + (path[0] == '/' ? path : String("/") + path);
}

static String urlEncode(const char *s) {
  if (!s) return String("");
  const char *hex = "0123456789ABCDEF";
  String out;
  while (*s) {
    char c = *s++;
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
      out += c;
    } else {
      out += '%';
      out += hex[(c >> 4) & 0x0F];
      out += hex[c & 0x0F];
    }
  }
  return out;
}

QrClient::QrClient(const char *baseUrl) : baseUrl_(baseUrl ? baseUrl : "") {}

bool QrClient::fetchMatrixText(const char *text, QrMatrix &out) {
  if (baseUrl_.length() == 0) return false;

  String url = urlJoin(baseUrl_, "/payment/qrcode?text=") + urlEncode(text);
  HTTPClient http;

  int code = -1;
  String payload;
  if (isHttpsUrl(url)) {
    WiFiClientSecure client;
    client.setInsecure();
    if (!http.begin(client, url)) return false;
    code = http.GET();
    if (code > 0) payload = http.getString();
    http.end();
  } else {
    if (!http.begin(url)) return false;
    code = http.GET();
    if (code > 0) payload = http.getString();
    http.end();
  }

  if (code != 200) return false;

  int nl = payload.indexOf('\n');
  if (nl < 1) return false;
  int size = payload.substring(0, nl).toInt();
  if (size < 21 || size > 177) return false;
  String rows = payload.substring(nl + 1);
  out.size = size;
  out.rows = rows;
  return true;
}

}  // namespace aiw
