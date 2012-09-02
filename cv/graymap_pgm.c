#include "graymap_pgm.h"

#include "alloc_graymap.h"
#include "graymap.h"

#include <stdio.h>

// This doesn't ignore comments, and is less flexible with respect to
// whitespace than the pgm spec allows.
const char kPgmHeader[] = "P5\n%d %d\n%d\n";

graymap_t* alloc_graymap_from_pgm(const char* filename) {
  FILE* f = fopen(filename, "rb");
  if (!f) return NULL;

  int w, h, d;
  fscanf(f, kPgmHeader, &w, &h, &d);
  if (d != 255) return NULL;

  graymap_t* graymap = alloc_graymap(w, h);
  int read = fread(graymap->data, 1, w*h, f);
  fclose(f);

  if (read != w*h) {
    free_graymap(graymap);
    return NULL;
  }
  return graymap;
}

bool save_graymap_to_pgm(const char* filename, const graymap_t* graymap) {
  FILE* f = fopen(filename, "wb");
  if (!f) return false;

  fprintf(f, kPgmHeader, graymap->w, graymap->h, 255);
  fwrite(graymap->data, 1, graymap->w*graymap->h, f);

  fclose(f);
  return true;
}
