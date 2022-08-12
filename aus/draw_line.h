#pragma once

#include <stddef.h>

#include "pixel.h"

struct Surface;

// Coordinates must be clipped to surface size already.
// Both x1,y1 and x2,y2 are on the line.
// Integer coordinates are in each pixel's center.
void draw_line(const Surface& s, size_t x1, size_t y1, size_t x2, size_t y2,
               Pixel color);

void draw_horizontal_line(const Surface& s, size_t x1, size_t y1, size_t x2,
               Pixel color);
