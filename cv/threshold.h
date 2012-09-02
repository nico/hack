#ifndef THRESHOLD_H_
#define THRESHOLD_H_

typedef struct graymap_t_ graymap_t;

int average_pixel(graymap_t*);
void threshold_on_average(graymap_t*);
void threshold_on_local_average(graymap_t* dst, graymap_t* src);

#endif  // THRESHOLD_H_

