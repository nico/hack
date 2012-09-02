#include "blur_box.h"

#include "graymap.h"

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

void blur_box_k(graymap_t* dst, graymap_t* src, int k) {
  // TODO: Exploit separability.
  const int w = src->w, h = src->h;
  const uint8_t* curr = src->data;
  uint8_t* curr_dst = dst->data;

  for (int y = 0; y < h; ++y) {
    const int lymin = imax(   -y, -k/2);
    const int lymax = imin(h-1-y, +k/2);

    int accum = k * k / 2;  // To round.
    for (int ly = lymin; ly <= lymax; ++ly)
      for (int x = 0; x <= imin(k/2, w-1); ++x)
        accum += curr[x + ly * w];

    for (int x = 0; x < w; ++x) {
      curr_dst[x] = accum / (k * k);

      if (x - k/2 >= 0)
        for (int ly = lymin; ly <= lymax; ++ly)
          accum -= curr[x - k/2 + ly * w];
      if (x + k/2 + 1 < w)
        for (int ly = lymin; ly <= lymax; ++ly)
          accum += curr[x + k/2 + 1 + ly * w];
    }

    curr += w;
    curr_dst += w;
  }
}

void blur_box_n8(graymap_t* dst, graymap_t* src) {
  const int w = src->w, h = src->h;
  const uint8_t* prev = 0,
               * curr = src->data,
               * next = src->data + w;
  uint8_t* curr_dst = dst->data;

  // +4 to round up.
  for (int y = 0; y < h; ++y) {
    int local_avg = curr[1] + 4;  // To round.
    if (prev) local_avg += prev[0] + prev[1];
    if (next) local_avg += next[0] + next[1];

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

    curr_dst += w;
  }
}

