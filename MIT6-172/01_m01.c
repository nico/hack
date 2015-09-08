#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <sys/time.h>

int64_t now_usec() {
  struct timeval tv;
  if (gettimeofday(&tv, NULL) < 0)
    exit(4);
  return (int64_t)tv.tv_sec * 1000*1000 + tv.tv_usec;
}

// 2d arrays. 8.5s for 1024x1024 * 1024x1024.
uint64_t testMM_2d(const int x, const int y, const int z) {
  double **A, **B, **C;
  int64_t started, ended;
  int i, j, k;
  A = (double**)malloc(sizeof(double*) * x);
  B = (double**)malloc(sizeof(double*) * x);
  C = (double**)malloc(sizeof(double*) * z);

  for (i = 0; i < x; i++)
    A[i] = (double*)malloc(sizeof(double) * y);
  for (i = 0; i < x; i++)
    B[i] = (double*)malloc(sizeof(double) * z);
  for (k = 0; k < z; k++)
    C[k] = (double*)malloc(sizeof(double) * y);

  for (i = 0; i < x; i++)
    for (k = 0; k < z; k++)
      B[i][k] = (double)rand();
  for (k = 0; k < z; k++)
    for (j = 0; j < y; j++)
      C[k][j] = (double)rand();
  for (i = 0; i < x; i++)
    for (j = 0; j < y; j++)
      A[i][j] = 0;
  started = now_usec();
  for (i = 0; i < x; i++)
    for (j = 0; j < y; j++)
      for (k = 0; k < z; k++)
        A[i][j] += B[i][k] + C[k][j];
  ended = now_usec();
  return ended - started;
}

#define IND(A, x, y, d) A[(x) * (d) + (y)]

// 1d arrays. 7.9s for 1024x1024 * 1024x1024.
uint64_t testMM_1d(const int x, const int y, const int z) {
  double *A, *B, *C;
  int64_t started, ended;
  int i, j, k;
  A = (double*)malloc(sizeof(double) * x * y);
  B = (double*)malloc(sizeof(double) * x * z);
  C = (double*)malloc(sizeof(double) * y * z);
  for (i = 0; i < x * z; i++)
    B[i] = (double)rand();
  for (i = 0; i < y * z; i++)
    C[i] = (double)rand();
  for (i = 0; i < x * y; i++)
    A[i] = 0;
  started = now_usec();
  for (i = 0; i < x; i++)
    for (j = 0; j < y; j++)
      for (k = 0; k < z; k++)
        IND(A, i, j, y) += IND(B, i, k, z) * IND(C, k, j, y);
  ended = now_usec();
  return ended - started;
}

// Transposed C. 1.0s for 1024x1024 * 1024x1024.
uint64_t testMM_1dt(const int x, const int y, const int z) {
  double *A, *B, *C, *Ct;
  int64_t started, ended;
  int i, j, k;
  A = (double*)malloc(sizeof(double) * x * y);
  B = (double*)malloc(sizeof(double) * x * z);
  C = (double*)malloc(sizeof(double) * y * z);
  Ct = (double*)malloc(sizeof(double) * z * y);
  for (i = 0; i < x * z; i++)
    B[i] = (double)rand();
  for (i = 0; i < y * z; i++)
    C[i] = (double)rand();
  for (i = 0; i < x * y; i++)
    A[i] = 0;

  started = now_usec();
  for (j = 0; j < y; j++)
    for (k = 0; k < z; k++)
      IND(Ct, j, k, z) = IND(C, k, j, y);
  for (i = 0; i < x; i++)
    for (j = 0; j < y; j++)
      for (k = 0; k < z; k++)
        IND(A, i, j, y) += IND(B, i, k, z) * IND(Ct, j, k, y);
  ended = now_usec();

  return ended - started;
}

// tiled 1d arrays. 1.4s for 1024x1024 * 1024x1024.
int min(int a, int b) {
  if (a < b)
    return a;
  return b;
}
uint64_t testMM_1d_tile(const int x, const int y, const int z) {
  double *A, *B, *C;
  int64_t started, ended;
  int i, j, k, j2, k2;
  A = (double*)malloc(sizeof(double) * x * y);
  B = (double*)malloc(sizeof(double) * x * z);
  C = (double*)malloc(sizeof(double) * y * z);
  for (i = 0; i < x * z; i++)
    B[i] = (double)rand();
  for (i = 0; i < y * z; i++)
    C[i] = (double)rand();
  for (i = 0; i < x * y; i++)
    A[i] = 0;
  started = now_usec();
#define block_x 32
#define block_y 32
  for (j2 = 0; j2 < y; j2 += block_x)
    for (k2 = 0; k2 < z; k2 += block_y)
      for (i = 0; i < x; i++)
        for (j = j2; j < min(j2 + block_x, y); j++)
          for (k = k2; k < min(k2 + block_y, z); k++)
            IND(A, i, j, y) += IND(B, i, k, z) * IND(C, k, j, y);
  ended = now_usec();
  return ended - started;
}

#if __APPLE__
#include <Accelerate/Accelerate.h>

// 30ms!
uint64_t testMM_1d_blas(const int x, const int y, const int z) {
  double *A, *B, *C;
  int64_t started, ended;
  int i, j, k, j2, k2;
  A = (double*)malloc(sizeof(double) * x * y);
  B = (double*)malloc(sizeof(double) * x * z);
  C = (double*)malloc(sizeof(double) * y * z);
  for (i = 0; i < x * z; i++)
    B[i] = (double)rand();
  for (i = 0; i < y * z; i++)
    C[i] = (double)rand();
  for (i = 0; i < x * y; i++)
    A[i] = 0;
  started = now_usec();
  cblas_dgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans,
              x, y, z, 1, B, z, C, y, 0, A, y);
  ended = now_usec();
  return ended - started;
}
#endif

int main() {
  srand(1234);
  printf("%f ms\n", testMM_2d(1024, 1024, 1024) / 1000.0);
  printf("%f ms\n", testMM_1d(1024, 1024, 1024) / 1000.0);
  printf("%f ms\n", testMM_1dt(1024, 1024, 1024) / 1000.0);
  printf("%f ms\n", testMM_1d_tile(1024, 1024, 1024) / 1000.0);
#if __APPLE__
  printf("%f ms\n", testMM_1d_blas(1024, 1024, 1024) / 1000.0);
#endif
}
