#pragma once

#include <memory>
#include <stddef.h>

// Premultiplied ARGB
using Pixel = uint32_t;

// A Surface is a sub-rectangle of a Framebuffer.
struct Surface {
  size_t width = 0;
  size_t height = 0;
  size_t pitch = 0;

  // Points to a width x height rectangle of Pixels whose rows are pitch many
  // Pixels apart.
  Pixel* pixels;
};

// Owns its pixels.
struct Framebuffer {
  size_t width = 0;
  size_t height = 0;
  std::unique_ptr<Pixel[]> pixels;

  Framebuffer(size_t width, size_t height);

  Pixel* scanline(size_t y) {
    assert(y < height);
    return pixels.get() + width * y;
  }

  Surface surface() { return surfaceForRect(0, 0, width, height); }
  Surface surfaceForRect(size_t x, size_t y, size_t w, size_t h);
};

