#include <stdio.h>

int main() {
  int n = 0;
  for (int i = 130254; i <= 678275; ++i) {
    int p1 = (i / 100000) % 10;
    int p2 = (i / 10000) % 10;
    int p3 = (i / 1000) % 10;
    int p4 = (i / 100) % 10;
    int p5 = (i / 10) % 10;
    int p6 = i % 10;
    if (p1 <= p2 && p2 <= p3 && p3 <= p4 && p4 <= p5 && p5 <= p6) {
      if (p1 == p2 || p2 == p3 || p3 == p4 || p4 == p5 || p5 == p6)
        ++n;
    }
  }

  printf("%d\n", n);
}
