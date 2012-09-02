#include "threshold.h"

#include "graymap.h"

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

void scale(graymap_t* graymap, int num, int denom) {
  int n = graymap->w * graymap->h;
  for (int i = 0; i < n; ++i)
    graymap->data[i] = graymap->data[i] * num / denom;
}
