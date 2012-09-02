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

void blur_box1(graymap_t* dst, graymap_t* src) {
  int w = src->w, h = src->h;
  uint8_t* prev = 0,
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

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

#include <stdio.h>

void blur_box_k(graymap_t* dst, graymap_t* src, int k) {
  int w = src->w, h = src->h;
  uint8_t* curr = src->data;
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

void scale(graymap_t* graymap, int num, int denom) {
  int n = graymap->w * graymap->h;
  for (int i = 0; i < n; ++i)
    graymap->data[i] = graymap->data[i] * num / denom;
}
