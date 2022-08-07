#include "draw_line.h"

#include <assert.h>

#include <algorithm>

#include "framebuffer.h"

void draw_line(const Surface& s, size_t x1, size_t y1, size_t x2, size_t y2,
               Pixel color) {
  assert(x1 < s.width);
  assert(y1 < s.height);
  assert(x2 < s.width);
  assert(y2 < s.height);

  ssize_t dx = x2 - x1;
  ssize_t dy = y2 - y1;

  if (abs(dy) < abs(dx)) {
    int iy = s.pitch;
    if (dx < 0) {
      std::swap(x1, x2);
      std::swap(y1, y2);
      dx = -dx;
      dy = -dy;
    }
    if (dy < 0) {
      iy = -iy;
      dy = -dy;
    }

    ssize_t D = 2 * dy - dx;
    Pixel* dst = s.scanline(y1);

    for (size_t x = x1; x < x2; ++x) {
      dst[x] = color;

      if (D > 0) {
        dst += iy;
        D -= 2 * dx;
      }
      D += 2 * dy;
    }
  } else {
    int ix = 1;
    if (dy < 0) {
      std::swap(x1, x2);
      std::swap(y1, y2);
      dx = -dx;
      dy = -dy;
    }
    if (dx < 0) {
      ix = -ix;
      dx = -dx;
    }

    ssize_t D = 2 * dx - dy;
    size_t x = x1;

    for (size_t y = y1; y < y2; ++y) {
      s.scanline(y)[x] = color;

      if (D > 0) {
        x += ix;
        D -= 2 * dy;
      }
      D += 2 * dx;
    }
  }
}
