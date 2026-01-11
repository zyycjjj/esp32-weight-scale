#pragma once

#include <Arduino.h>

namespace aiw {

struct PaymentCreateRequest {
  float amount;
  const char *description;
  const char *deviceId;
  const char *deviceName;
};

struct PaymentCreateResponse {
  String codeUrl;
  String outTradeNo;
};

struct PaymentQueryResponse {
  bool success;
  String tradeState;
};

class PaymentClient {
public:
  explicit PaymentClient(const char *baseUrl);
  bool create(const PaymentCreateRequest &req, PaymentCreateResponse &res);
  bool query(const char *outTradeNo, PaymentQueryResponse &res);

private:
  bool extractJsonStringField(const String &json, const char *field, String &out);
  bool extractJsonBoolField(const String &json, const char *field, bool &out);

  String baseUrl_;
};

}  // namespace aiw

