#ifndef ALLOC_GRAYMAP_H_
#define ALLOC_GRAYMAP_H_

typedef struct graymap_t_ graymap_t;

graymap_t* alloc_graymap(int w, int h);
void free_graymap(graymap_t*);

#endif  // ALLOC_GRAYMAP_H_

