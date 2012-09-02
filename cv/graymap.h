#ifndef GRAYMAP_H_
#define GRAYMAP_H_

#include <stdint.h>

typedef struct graymap_t_ {
  int w, h;
  uint8_t* data;
} graymap_t;

#endif  // GRAYMAP_H_
