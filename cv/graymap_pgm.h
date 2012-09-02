#ifndef GRAYMAP_PGM_H_
#define GRAYMAP_PGM_H_

#include <stdbool.h>

typedef struct graymap_t_ graymap_t;

graymap_t* alloc_graymap_from_pgm(const char* filename);
bool save_graymap_to_pgm(const char* filename, const graymap_t*);

#endif  // GRAYMAP_PGM_H_
