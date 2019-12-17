#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int min(int a, int b) {
  if (a < b) return a;
  return b;
}

void step(char* dst, char* src, int n) {
  int cumsum[n + 1];
  cumsum[0] = 0;
  for (int i = 0; i < n; ++i)
    cumsum[i + 1] = (cumsum[i] + src[i]);
  //for (int i = 0; i < n + 1; ++i) printf("%d ", cumsum[i]); printf("\n");

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

  char buf2[n];

  char* src = buf, *dst = buf2;
  for (int i = 0; i < 100; ++i) {
    step(dst, src, n);
    char* t = src;
    src = dst;
    dst = t;
  }

  for (int i = 0; i < 8; ++i) printf("%d", src[i]); printf("\n");
}
