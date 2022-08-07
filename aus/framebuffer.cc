#include "framebuffer.h"

Framebuffer::Framebuffer(size_t width, size_t height)
    : width(width), height(height) {
  // Note: make_unique() initializes the array.
  pixels = std::make_unique<Pixel[]>(width * height);
}

Surface Framebuffer::surfaceForRect(size_t x, size_t y, size_t w,
                                    size_t h) const {
  return Surface{w, h, width, scanline(y) + x};
}
