#pragma once

#include <assert.h>
#include <stddef.h>

#include "pixel.h"

// A Surface is a sub-rectangle of a Framebuffer.
struct Surface {
  size_t width = 0;
  size_t height = 0;
  size_t pitch = 0;

  // Points to a width x height rectangle of Pixels whose rows are pitch many
  // Pixels apart.
  Pixel* pixels;

  Pixel* scanline(size_t y) const {
    assert(y < height);
    return pixels + pitch * y;
  }
};
