#ifndef FIND_CORNERS_H_
#define FIND_CORNERS_H_

#include <stdbool.h>

typedef struct graymap_t_ graymap_t;

bool find_corners(graymap_t*, float corners[4][2]);

#endif  // FIND_CORNERS_H_
