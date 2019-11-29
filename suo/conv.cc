// Like conv.py, but in C, and computes DIFAT blocks too.
// Also includes an extra-naive algorithm.
// clang -O2 conv.cc -Wall -Wextra -Wconversion && ./a.out

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

typedef uint64_t N;

void naive(N n_blocks, N* out_n_difat, N* out_n_fat) {
  N n_difat = 0, n_fat = 0;

  while (true) {
    N n_possible_fat = 109 + 127 * n_difat;
    if (n_possible_fat < n_fat) {
      ++n_difat;
      continue;
    }

    N n_possible_blocks = 128 * n_fat;
    if (n_possible_blocks < n_blocks + n_fat + n_difat) {
      ++n_fat;
      continue;
    }

    break;
  }
  *out_n_difat = n_difat;
  *out_n_fat = n_fat;
}

int main() {
  for (N k = 4000000000; k < 4000001000; ++k) {
    N n_fat, n_difat;
    naive(k, &n_difat, &n_fat);
    printf("%" PRIu64 " %" PRIu64 " %" PRIu64 "\n", k, n_difat, n_fat);
  }
}
