#include "find_corners.h"

#include "graymap.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

bool find_corners(graymap_t* graymap, float corners[4][2]) {
  const int kNumAngles = 360;
  const int kNumRadii = graymap->h;

  float kSin[kNumAngles];
  float kCos[kNumAngles];
  for (int a = 0; a < kNumAngles; ++a) {
    float rho = a * M_PI / kNumAngles;
    kSin[a] = sin(rho);
    kCos[a] = cos(rho);
  }

  const float kMaxRadius = sqrt(graymap->w*graymap->w + graymap->h*graymap->h);
  unsigned* houghmap = calloc(kNumRadii * kNumAngles, sizeof(unsigned));
  for (int y = 0; y < graymap->h; ++y) {
    for (int x = 0; x < graymap->w; ++x) {
      if (graymap->data[y*graymap->w + x] == 255) continue;

      for (int a = 0; a < kNumAngles; ++a) {
        float nx = kCos[a];
        float ny = kSin[a];
        float r = fabs(nx*x + ny*y);
        unsigned ri = r * (kNumRadii - 1) / kMaxRadius;
        houghmap[ri*kNumAngles + a]++;
      }
    }
  }

  //graymap_t* grayhough = alloc_graymap(kNumAngles, kNumRadii);
  unsigned maxhough = 1;
  for (int i = 0; i < kNumRadii*kNumAngles; ++i)
    if (houghmap[i] > maxhough)
      maxhough = houghmap[i];
  for (int ri = 0, i = 0; ri < kNumRadii; ++ri) {
    for (int ai = 0; ai < kNumAngles; ++ai, ++i) {
      //grayhough->data[i] = houghmap[i] * 255 / maxhough;
      if (houghmap[i] > 75 * maxhough / 100) {
        // Candidate! Find max in neighborhood, zero out remainder.
        const int k = 31;
        unsigned best = houghmap[i], besta = ai, bestr = ri;
        for (int rd = imax(ri - k/2, 0);
             rd <= imin(ri + k/2, kNumRadii - 1);
             ++rd) {
          for (int ad = ai - k/2; ad <= ai + k/2; ++ad) {
            int aii = ad < 0 ? ai + kNumAngles : ad;
            if (houghmap[rd*kNumAngles + aii] > best) {
              best = houghmap[rd*kNumAngles + aii];
              besta = aii;
              bestr = rd;
            }
            houghmap[rd*kNumAngles + aii] = 0;
          }
        }

        float deg = besta * 180.f / kNumAngles;
        float radius = bestr * kMaxRadius / kNumRadii;
        fprintf(stderr, "r %f a %f\n", radius, deg);
      }
    }
  }
  //save_graymap_to_pgm("hough.pgm", grayhough);
  //free_graymap(grayhough);

  free(houghmap);

  return true;
}
