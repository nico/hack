#pragma once
#include <stdint.h>

// Premultiplied ARGB
using Pixel = uint32_t;

constexpr Pixel rgb(uint8_t r, uint8_t g, uint8_t b) {
  return (r << 16) | (g << 8) | b;
}
