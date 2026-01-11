#pragma once

#include <Arduino.h>

#include "app/display_st7789.h"
#include "app/qr_client.h"

namespace aiw {

class QrRenderer {
public:
  explicit QrRenderer(DisplaySt7789 &display);
  bool drawMatrix(const QrMatrix &matrix, int x, int y, int maxSize, uint16_t fg, uint16_t bg);

private:
  bool getModule(const QrMatrix &m, int x, int y) const;
  DisplaySt7789 &display_;
};

}  // namespace aiw

