#include "alloc_graymap.h"
#include "blur_box.h"
#include "connected_component.h"
#include "find_corners.h"
#include "graymap.h"
#include "graymap_pgm.h"
#include "threshold.h"

#include <stdio.h>

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

  float corners[4][2];
  find_corners(graymap, corners);

  save_graymap_to_pgm("out.pgm", graymap);
  printf("Wrote out.png\n");

  free_graymap(graymap);
}
