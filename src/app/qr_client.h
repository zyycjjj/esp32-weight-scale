#pragma once

#include <Arduino.h>

namespace aiw {

struct QrMatrix {
  int size{0};
  String rows;
};

class QrClient {
public:
  explicit QrClient(const char *baseUrl);
  bool fetchMatrixText(const char *text, QrMatrix &out);

private:
  String baseUrl_;
};

}  // namespace aiw

