#pragma once

#include <stddef.h>

#include "pixel.h"

struct Surface;

// Coordinates must be clipped to surface size already.
void draw_line(const Surface& s, size_t x1, size_t y1, size_t x2, size_t y2,
               Pixel color);
