#include "threshold.h"

#include "graymap.h"

#include <stdint.h>

int average_pixel(graymap_t* graymap) {
  uint64_t avg = 0;
  int n = graymap->w * graymap->h;
  for (int i = 0; i < n; ++i)
    avg += graymap->data[i];
  return (int)((avg + n/2) / n);
}

void threshold_on_constant(graymap_t* graymap, int c) {
  int n = graymap->w * graymap->h;
  for (int i = 0; i < n; ++i)
    if (graymap->data[i] < c) graymap->data[i] = 0;
    else                        graymap->data[i] = 255;
}

void threshold_on_local_average(graymap_t* graymap, graymap_t* thres) {
  int n = graymap->w * graymap->h;
  for (int i = 0; i < n; ++i)
    if (graymap->data[i] < thres->data[i]) graymap->data[i] = 0;
    else                                   graymap->data[i] = 255;
}

void average_8(graymap_t* dst, graymap_t* src) {
  int w = src->w, h = src->h;
  uint8_t* prev = 0,
         * curr = src->data,
         * next = src->data + w;
  uint8_t* curr_dst = dst->data;

  // +4 to round up.
  int local_avg = curr[1] + next[0] + next[1] + 4;
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      curr_dst[x] = local_avg / 8;

      local_avg += curr[x];
      if (x - 1 >= 0) {
        local_avg -= curr[x-1];
        if (prev) local_avg -= prev[x-1];
        if (next) local_avg -= next[x-1];
      }
      if (x + 1 < w)
        local_avg -= curr[x+1];
      if (x + 2 < w) {
        local_avg += curr[x+2];
        if (prev) local_avg += prev[x+2];
        if (next) local_avg += next[x+2];
      }
    }
    prev = curr;
    curr = next;
    next = (y + 1 == src->h) ? 0 : next + w;

    local_avg = curr[1];
    if (prev) local_avg += prev[0] + prev[1];
    if (next) local_avg += next[0] + next[1];

    curr_dst += w;
  }
}
