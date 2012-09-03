#include "alloc_graymap.h"
#include "blur_box.h"
#include "connected_component.h"
#include "graymap.h"
#include "graymap_pgm.h"
#include "threshold.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

int usage(const char* program_name) {
  fprintf(stderr, "Usage: %s file.pgm\n", program_name);
  return 1;
}

void threshold_pixels(graymap_t* graymap) {
  graymap_t* threshold = alloc_graymap(graymap->w, graymap->h);
  blur_box_k(threshold, graymap, 11);
  scale(threshold, 94, 100);
  threshold_on_local_average(graymap, threshold);
  free_graymap(threshold);
}

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

int main(int argc, char* argv[]) {
  if (argc != 2) {
    return usage(argv[0]);
    return 1;
  }

  graymap_t* graymap = alloc_graymap_from_pgm(argv[1]);
  if (!graymap) {
    fprintf(stderr, "Failed to load %s\n", argv[1]);
    return 1;
  }
  
  // http://sudokugrab.blogspot.com/2009/07/how-does-it-all-work.html
  threshold_pixels(graymap);
  // TODO: Maybe run a few open iterations to clean up noise pixels?
  find_biggest_connected_component(graymap);

  //hough(graymap);
  const int kNumAngles = 360;
  const int kNumRadii = graymap->h;
  const float kMaxRadius = sqrt(graymap->w*graymap->w + graymap->h*graymap->h);
  unsigned* houghmap = calloc(kNumRadii * kNumAngles, sizeof(unsigned));
  for (int y = 0; y < graymap->h; ++y) {
    for (int x = 0; x < graymap->w; ++x) {
      if (graymap->data[y*graymap->w + x] == 255) continue;

      for (int a = 0; a < kNumAngles; ++a) {
        float rho = a * M_PI / kNumAngles;
        float nx = cos(rho);
        float ny = sin(rho);
        float r = fabs(nx*x + ny*y);
        unsigned ri = r * (kNumRadii - 1) / kMaxRadius;
        houghmap[ri*kNumAngles + a]++;
      }
    }
  }

  graymap_t* grayhough = alloc_graymap(kNumAngles, kNumRadii);
  unsigned maxhough = 1;
  for (int i = 0; i < kNumRadii*kNumAngles; ++i)
    if (houghmap[i] > maxhough)
      maxhough = houghmap[i];
  for (int ri = 0, i = 0; ri < kNumRadii; ++ri) {
    for (int ai = 0; ai < kNumAngles; ++ai, ++i) {
      grayhough->data[i] = houghmap[i] * 255 / maxhough;
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
        fprintf(stderr, "ri %d ai %d\n", bestr, besta);
      }
    }
  }
  save_graymap_to_pgm("hough.pgm", grayhough);
  free_graymap(grayhough);

  free(houghmap);


  save_graymap_to_pgm("out.pgm", graymap);
  printf("Wrote out.png\n");

  free_graymap(graymap);
}
