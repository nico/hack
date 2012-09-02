#include "blur_box.h"
#include "graymap.h"
#include "graymap_pgm.h"
#include "threshold.h"

#include <stdio.h>

int usage(const char* program_name) {
  fprintf(stderr, "Usage: %s file.pgm\n", program_name);
  return 1;
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

#if 0
  threshold_on_constant(graymap, average_pixel(graymap));
#else
  graymap_t* dst = alloc_graymap(graymap->w, graymap->h);
  blur_box_k(dst, graymap, 11);
  scale(dst, 94, 100);
  save_graymap_to_pgm("avg.pgm", dst);
  threshold_on_local_average(graymap, dst);
  free_graymap(dst);
#endif

  save_graymap_to_pgm("out.pgm", graymap);
  printf("Wrote out.png\n");

  free_graymap(graymap);
}
