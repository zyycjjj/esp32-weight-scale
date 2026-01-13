#pragma once

#include <stdint.h>

namespace aiw {

struct MonoGlyph16 {
  uint32_t codepoint;
  const uint8_t *rows;
};

const MonoGlyph16 *findZhGlyph(uint32_t codepoint);
void drawZhText16(class DisplaySt7789 &display, int x, int y, const char *utf8, uint16_t fg, uint16_t bg);

}  // namespace aiw

