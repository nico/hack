#include <assert.h>
#include <stddef.h>

#include <memory>

#include "pixel.h"
#include "surface.h"

// Owns its pixels.
struct Framebuffer {
  size_t width = 0;
  size_t height = 0;
  std::unique_ptr<Pixel[]> pixels;

  Framebuffer(size_t width, size_t height);

  Pixel* scanline(size_t y) const {
    assert(y < height);
    return pixels.get() + width * y;
  }

  Surface surface() const { return surfaceForRect(0, 0, width, height); }
  Surface surfaceForRect(size_t x, size_t y, size_t w, size_t h) const;
};
