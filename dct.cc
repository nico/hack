#include <math.h>
#include <stdio.h>
#include <string.h>

// itu-t81.pdf
// A.3.3 FDCT and IDCT (informative)
double dct(double* d, int u, int v) {
  double sum = 0;
  for (int y = 0; y < 8; ++y) {
    for (int x = 0; x < 8; ++x) {
      double cx = cos((2 * x + 1) * u * M_PI / 16);
      double cy = cos((2 * y + 1) * v * M_PI / 16);
      sum += d[y * 8 + x] * cx * cy;
    }
  }
  double cu = u == 0 ? M_SQRT1_2 : 1;
  double cv = v == 0 ? M_SQRT1_2 : 1;
  return cu * cv * sum / 4;
}

void dct_8x8(double* d) {
  double tmp[64];
  for (int v = 0; v < 8; ++v)
    for (int u = 0; u < 8; ++u)
      tmp[v * 8 + u] = dct(d, u, v);
  memcpy(d, tmp, sizeof(tmp));
}

double idct(double* d, int x, int y) {
  double sum = 0;
  for (int v = 0; v < 8; ++v) {
    for (int u = 0; u < 8; ++u) {
      double cu = u == 0 ? M_SQRT1_2 : 1;
      double cv = v == 0 ? M_SQRT1_2 : 1;
      double cx = cos((2 * x + 1) * u * M_PI / 16);
      double cy = cos((2 * y + 1) * v * M_PI / 16);
      sum += d[v * 8 + u] * cu * cv * cx * cy;
    }
  }
  return sum / 4;
}

void idct_8x8(double* d) {
  double tmp[64];
  for (int y = 0; y < 8; ++y)
    for (int x = 0; x < 8; ++x)
      tmp[y * 8 + x] = idct(d, x, y);
  memcpy(d, tmp, sizeof(tmp));
}

void print(double* d) {
  for (int y = 0; y < 8; ++y) {
    for (int x = 0; x < 8; ++x)
      printf("%8.2f ", d[y * 8 + x]);
    printf("\n");
  }
  printf("\n");
}

int main() {
  double d[64];
  for (int i = 0; i < 64; ++i)
    d[i] = 255;

  printf("input:\n");
  print(d);

  for (int i = 0; i < 64; ++i)
    d[i] -= 128;
  dct_8x8(d);

  printf("after level shift and dft:\n");
  print(d);

  idct_8x8(d);
  for (int i = 0; i < 64; ++i)
    d[i] += 128;

  printf("after idft and level unshift:\n");
  print(d);
}
