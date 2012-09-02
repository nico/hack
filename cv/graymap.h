#ifndef GRAYMAP_H_
#define GRAYMAP_H_

#include <stdint.h>

typedef struct {
  int w, h;
  uint8_t* data;
} graymap_t;

graymap_t* alloc_graymap(int w, int h);
void free_graymap(graymap_t*);

#endif  // GRAYMAP_H_
