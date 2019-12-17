#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Like 16b.py (which is like 16_3.py), but 50x as fast as 100x input.
// No algorithm changes, just translated from Python to C.

int min(int a, int b) {
  if (a < b) return a;
  return b;
}

void step(char* dst, char* src, int n) {
  int* cumsum = malloc((n + 1) * sizeof(int));
  cumsum[0] = 0;
  for (int i = 0; i < n; ++i)
    cumsum[i + 1] = (cumsum[i] + src[i]);

  int start = 0, step = 1, sign = 1;
  for (int i = 0; i < n; ++i) {
    int s = 0;
    for (int k = start; k < n; k += 2*step) {
      s += (sign * (cumsum[min(k+step, n)] - cumsum[k]));
      sign = -sign;
    }
    start += 1;
    step += 1;
    sign = 1;
    dst[i] = abs(s) % 10;
  }

  free(cumsum);
}

int main() {
  char buf[1000];
  if (fgets(buf, sizeof(buf), stdin) == NULL)
    return 1;
  int n = strlen(buf) - 1;
  if (buf[n] != '\n') {
    fprintf(stderr, "line too long\n");
    return 1;
  }
  buf[n] = '\0';
  for (int i = 0; i < n; ++i)
    buf[i] = buf[i] - '0';

  const int N = 10000;
  char* buf1 = malloc(N * n);
  for (int i = 0; i < N; ++i)
    memcpy(buf1 + i*n, buf, n);

  char* buf2 = malloc(N * n);
  char* src = buf1, *dst = buf2;

  char offbuf[8];
  for (int i = 0; i < 7; ++i) offbuf[i] = src[i] + '0';
  offbuf[7] = '\0';
  int offset = atoi(offbuf);

  for (int i = 0; i < 100; ++i) {
    step(dst, src, n*N);
    char* t = src;
    src = dst;
    dst = t;
  }

  for (int i = 0; i < 8; ++i) printf("%d", src[offset + i]); printf("\n");
}
