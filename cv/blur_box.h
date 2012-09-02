#ifndef BLUR_BOX_H_
#define BLUR_BOX_H_

typedef struct graymap_t_ graymap_t;

void blur_box_k(graymap_t* dst, graymap_t* src, int k);

// Computes the average value of the 8 surrounding pixels of each pixel in
// src and stores the result in dst.
void blur_box_n8(graymap_t* dst, graymap_t* src);

#endif  // BLUR_BOX_H_
