#include <stdio.h>
#include <stdlib.h>

static int delta(int a, int b) {
  if (a < b) return  1;
  if (a > b) return -1;
  return 0;
}

static void update_vel(int* vel, int* pos, const int N) {
  for (int i = 0; i < N; ++i)
    for (int j = 0; j < N; ++j)
      for (int k = 0; k < 3; ++k)
        vel[i*3 + k] += delta(pos[i*3 + k], pos[j*3 + k]);
}

static void update_pos(int* vel, int* pos, const int N) {
  for (int i = 0; i < N; ++i)
    for (int k = 0; k < 3; ++k)
      pos[i*3 + k] += vel[i*3 + k];
}

int abs_sum(int* p) {
  return abs(p[0]) + abs(p[1]) + abs(p[2]);
}

static int total_energy(int* vel, int* pos, const int N) {
  int total = 0;
  for (int i = 0; i < N; ++i)
    total += abs_sum(&pos[3*i]) * abs_sum(&vel[3*i]);
  return total;
}


static void print(int* vel, int* pos, const int N) {
  for (int i = 0; i < N; ++i) {
    printf("pos:");
    for (int k = 0; k < 3; ++k)
      printf(" %3d", pos[i*3 + k]);
    printf(", vel:");
    for (int k = 0; k < 3; ++k)
      printf(" %3d", vel[i*3 + k]);
    printf("\n");
  }
  printf("\n");
}

int main() {
  int pos[] = {
     -1,  -4,  0,
      4,   7, -1,
    -14, -10,  9,
      1,   2, 17,
  };
  const int N = sizeof(pos)/sizeof(pos[0])/3;

  int vel[N*3] = { 0 };

  for (int step = 0; step < 1000; ++step) {
    update_vel(vel, pos, N);
    update_pos(vel, pos, N);
  }
  printf("%d\n", total_energy(vel, pos, N));
}
