#include "draw_line.h"

#include <assert.h>

#include <algorithm>

#include "framebuffer.h"

void draw_horizontal_line(const Surface& s, size_t x1, size_t y1, size_t x2,
               Pixel color) {
  assert(x1 < s.width);
  assert(y1 < s.height);
  assert(x2 < s.width);

  Pixel* dst = s.scanline(y1);
  std::fill(dst + std::min(x1, x2), dst + std::max(x1, x2) + 1, color);
}

void draw_vertical_line(const Surface& s, size_t x1, size_t y1, size_t y2,
               Pixel color) {
  assert(x1 < s.width);
  assert(y1 < s.height);
  assert(y2 < s.height);

  // clang's auto-vectorizer gets all excited and adds vector code for the
  // rare occurence that the surface has a width <= 8 or even 1. This could
  // be disabled with
  // `#pragma clang loop vectorize(disable) interleave(disable)`.
  for (size_t y = std::min(y1, y2), e = std::max(y1, y2); y <= e; ++y)
    s.scanline(y)[x1] = color;
}

void draw_line(const Surface& s, size_t x1, size_t y1, size_t x2, size_t y2,
               Pixel color) {
  if (y1 == y2)
    return draw_horizontal_line(s, x1, y1, x2, color);
  if (x1 == x2)
    return draw_vertical_line(s, x1, y1, y2, color);

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

    for (int i = 0; i <= dx; ++i) {
      dst[x] = color;
      x += ix;

      // Using `if (D > 0 || (D == 0 && i < dx/2))` here instead would produce
      // the more pleasing  #....  instead of the current unsymmetric  ##...
      //                    .###.                                      ..##.
      //                    ....#                                      ....#
      if (D > 0) {
        dst += iy;
        D -= 2 * dx;
      }
      D += 2 * dy;
    }
  } else {
    ssize_t D = 2 * dx - dy;

    for (int i = 0; i <= dy; ++i) {
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
