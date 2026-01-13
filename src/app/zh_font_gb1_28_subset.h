#pragma once

#include <stddef.h>
#include <stdint.h>

namespace aiw {

struct ZhGlyph28 {
  uint32_t codepoint;
  uint8_t box_w;
  uint8_t box_h;
  int8_t ofs_x;
  int8_t ofs_y;
  const uint8_t *data;
};

const ZhGlyph28 *findZhGlyph28(uint32_t codepoint);

}  // namespace aiw

