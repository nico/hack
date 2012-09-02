#include "alloc_graymap.h"

#include "graymap.h"

#include <stdlib.h>

graymap_t* alloc_graymap(int w, int h) {
  graymap_t* graymap = malloc(sizeof(graymap));
  graymap->w = w;
  graymap->h = h;
  graymap->data = malloc(w * h);
  return graymap;
}

void free_graymap(graymap_t* graymap) {
  free(graymap->data);
  free(graymap);
}
