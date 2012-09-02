#ifndef THRESHOLD_H_
#define THRESHOLD_H_

typedef struct graymap_t_ graymap_t;

int average_pixel(graymap_t*);
void threshold_on_constant(graymap_t*, int c);
void threshold_on_local_average(graymap_t*, graymap_t* thres);

// Computes the average value of the 8 surrounding pixels of each pixel in
// srcand stores the result in dst.
void average_8(graymap_t* dst, graymap_t* src);

#endif  // THRESHOLD_H_

