#include "alloc_graymap.h"
#include "blur_box.h"
#include "connected_component.h"
#include "find_corners.h"
#include "graymap.h"
#include "graymap_pgm.h"
#include "linear.h"
#include "threshold.h"

#include <stdio.h>
#include <string.h>

int usage(const char* program_name) {
  fprintf(stderr, "Usage: %s file.pgm\n", program_name);
  return 1;
}

void threshold_pixels(graymap_t* graymap) {
  graymap_t* threshold = alloc_graymap(graymap->w, graymap->h);
  blur_box_k(threshold, graymap, 11);
  scale(threshold, 94, 100);
  threshold_on_local_average(graymap, threshold);
  free_graymap(threshold);
}

int main(int argc, char* argv[]) {
  if (argc != 2) {
    return usage(argv[0]);
    return 1;
  }

  graymap_t* graymap = alloc_graymap_from_pgm(argv[1]);
  if (!graymap) {
    fprintf(stderr, "Failed to load %s\n", argv[1]);
    return 1;
  }
  
  // http://sudokugrab.blogspot.com/2009/07/how-does-it-all-work.html
  threshold_pixels(graymap);
  save_graymap_to_pgm("1_thresh.pgm", graymap);
  // TODO: Maybe run a few open iterations to clean up noise pixels?
  find_biggest_connected_component(graymap);
  save_graymap_to_pgm("2_component.pgm", graymap);

  float projected_corners[4][2];
  if (!find_corners(graymap, projected_corners)) {
    fprintf(stderr, "Failed to find corners\n");
    return 1;
  }

  for (int i = 0; i < 4; ++i) {
    const int k = 11;
    for (int y = projected_corners[i][1] - k/2;
         y <= projected_corners[i][1] + k/2;
         ++y) {
      for (int x = projected_corners[i][0] - k/2;
           x <= projected_corners[i][0] + k/2;
           ++x) {
        graymap->data[y*graymap->w + x] = 128 + 30 * i;
      }
    }
  }

  save_graymap_to_pgm("3_corners.pgm", graymap);

  free_graymap(graymap);

  const int kSudokuWidth = 16 * 9;
  const int kSudokuHeight = 16 * 9;
  float unprojected_corners[4][2] = {
    { 0, 0 },
    { kSudokuWidth - 1 , 0 },
    { 0, kSudokuHeight - 1 },
    { kSudokuWidth - 1, kSudokuHeight - 1 },
  };
  float projmat[3][3];
  compute_projection_matrix(projmat, unprojected_corners, projected_corners);

  graymap_t* orig = alloc_graymap_from_pgm(argv[1]);
  graymap_t* sudoku = alloc_graymap(kSudokuWidth, kSudokuHeight);
  for (int y = 0; y < kSudokuHeight; ++y) {
    for (int x = 0; x < kSudokuWidth; ++x) {
      float p[3] = {
        projmat[0][0]*x + projmat[0][1]*y + projmat[0][2],
        projmat[1][0]*x + projmat[1][1]*y + projmat[1][2],
        projmat[2][0]*x + projmat[2][1]*y + projmat[2][2],
      };
      int sx = p[0] / p[2];
      int sy = p[1] / p[2];
      // XXX nicer sampling
      sudoku->data[y * kSudokuWidth + x] = orig->data[sy * graymap->w + sx];
    }
  }
  free_graymap(orig);

  save_graymap_to_pgm("4_sudoku.pgm", sudoku);

  threshold_pixels(sudoku);
  save_graymap_to_pgm("5_thresh.pgm", sudoku);

  graymap_t* tile = alloc_graymap(16, 16);
  for (int r = 0; r < 9; ++r) {
    for (int c = 0; c < 9; ++c) {
      // XXX give graymap a stride? Then this copying isn't needed.
      for (int y = 0; y < tile->h; ++y)
        for (int x = 0; x < tile->w; ++x) {
          tile->data[y*tile->w + x] =
              sudoku->data[(y + r*16)*sudoku->w + (x + c*16)];
        }

      // Clean 2px wide border.
      memset(tile->data, 255, 2*16);
      memset(&tile->data[14 * 16], 255, 2*16);
      for (int i = 2; i < 14; ++i) {
        tile->data[i*16 + 0]  = 255;
        tile->data[i*16 + 1]  = 255;
        tile->data[i*16 + 14] = 255;
        tile->data[i*16 + 15] = 255;
      }

      for (int y = 0; y < tile->h; ++y)
        for (int x = 0; x < tile->w; ++x) {
          sudoku->data[(y + r*16)*sudoku->w + (x + c*16)] =
              tile->data[y*tile->w + x];
        }
    }
  }
  free_graymap(tile);
  save_graymap_to_pgm("6_comp.pgm", sudoku);

  free_graymap(sudoku);
}
