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

  ssize_t dx = x2 - x1, ix = 1;
  if (dx < 0) {
    dx = -dx;
    ix = -1;
  }

  ssize_t dy = y2 - y1, iy = s.pitch;
  if (dy < 0) {
    dy = -dy;
    iy = -iy;
  }

  size_t x = x1;
  Pixel* dst = s.scanline(y1);

  if (dy < dx) {
    ssize_t D = 2 * dy - dx;

    for (int i = 0; i < dx; ++i) {
      dst[x] = color;
      x += ix;

      if (D > 0) {
        dst += iy;
        D -= 2 * dx;
      }
      D += 2 * dy;
    }
  } else {
    ssize_t D = 2 * dx - dy;

    for (int i = 0; i < dy; ++i) {
      dst[x] = color;
      dst += iy;

      if (D > 0) {
        x += ix;
        D -= 2 * dy;
      }
      D += 2 * dx;
    }
  }
}
