#include "app/qr_renderer.h"

namespace aiw {

QrRenderer::QrRenderer(DisplaySt7789 &display) : display_(display) {}

static int nextLineEnd(const String &s, int start) {
  int idx = s.indexOf('\n', start);
  return idx < 0 ? s.length() : idx;
}

bool QrRenderer::getModule(const QrMatrix &m, int x, int y) const {
  if (x < 0 || y < 0 || x >= m.size || y >= m.size) return false;
  int lineStart = 0;
  for (int row = 0; row < y; ++row) {
    int end = nextLineEnd(m.rows, lineStart);
    lineStart = end + 1;
    if (lineStart >= (int)m.rows.length()) return false;
  }
  int lineEnd = nextLineEnd(m.rows, lineStart);
  if (lineEnd - lineStart < m.size) return false;
  char c = m.rows[lineStart + x];
  return c == '1';
}

bool QrRenderer::drawMatrix(const QrMatrix &m, int x, int y, int maxSize, uint16_t fg, uint16_t bg) {
  if (m.size <= 0) return false;
  int scale = maxSize / m.size;
  if (scale < 1) return false;
  int drawSize = m.size * scale;

  display_.fillRect(x, y, drawSize, drawSize, bg);

  for (int row = 0; row < m.size; ++row) {
    int runStart = -1;
    for (int col = 0; col < m.size; ++col) {
      bool on = getModule(m, col, row);
      if (on) {
        if (runStart < 0) runStart = col;
      } else {
        if (runStart >= 0) {
          int runW = col - runStart;
          display_.fillRect(x + runStart * scale, y + row * scale, runW * scale, scale, fg);
          runStart = -1;
        }
      }
    }
    if (runStart >= 0) {
      int runW = m.size - runStart;
      display_.fillRect(x + runStart * scale, y + row * scale, runW * scale, scale, fg);
    }
  }
  return true;
}

}  // namespace aiw

