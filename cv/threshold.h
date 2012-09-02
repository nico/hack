#ifndef THRESHOLD_H_
#define THRESHOLD_H_

typedef struct graymap_t_ graymap_t;

int average_pixel(graymap_t*);
void threshold_on_constant(graymap_t*, int c);
void threshold_on_local_average(graymap_t*, graymap_t* thres);

void scale(graymap_t*, int num, int denom);

#endif  // THRESHOLD_H_

