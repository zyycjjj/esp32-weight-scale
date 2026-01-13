#pragma once

#include <Arduino.h>
#include "app/display_st7789.h"

namespace aiw {

void drawText5x7(DisplaySt7789 &display, int x, int y, const char *text, uint16_t fg, uint16_t bg, int scale = 1);

}

