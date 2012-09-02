#ifndef THRESHOLD_H_
#define THRESHOLD_H_

typedef struct graymap_t_ graymap_t;

int average_pixel(graymap_t*);
void threshold_on_constant(graymap_t*, int c);
void threshold_on_local_average(graymap_t*, graymap_t* thres);

// Computes the average value of the 8 surrounding pixels of each pixel in
// src and stores the result in dst.
void blur_box1(graymap_t* dst, graymap_t* src);

void blur_box_k(graymap_t* dst, graymap_t* src, int k);

void scale(graymap_t*, int num, int denom);

#endif  // THRESHOLD_H_

