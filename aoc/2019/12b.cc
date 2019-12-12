#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <numeric>
#include <unordered_set>
#include <string>
using std::lcm, std::string, std::unordered_set;

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

static uint64_t lcm3(uint64_t a, uint64_t b, uint64_t c) {
  return lcm(a, lcm(b, c));
}

static bool check(unordered_set<string>* seen, int* vel, int* pos) {
  char buf[1024];
  sprintf(buf, "%d,%d,%d,%d,%d,%d,%d,%d",
               pos[0], pos[3], pos[6], pos[9],
               vel[0], vel[3], vel[6], vel[9]);
  return !seen->insert(buf).second;
}

static void find_cycle(int step, int bit, int *mask, int *cycle,
                       unordered_set<string>* seen, int* vel, int* pos) {
  if (!(*mask & bit) && check(seen, vel, pos)) {
    *cycle = step;
    *mask |= bit;
  }
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

  unordered_set<string> seen_x, seen_y, seen_z;
  int cycle_x, cycle_y, cycle_z, mask = 0;

  for (int step = 0; mask != 7; ++step) {
    update_vel(vel, pos, N);
    update_pos(vel, pos, N);

    find_cycle(step, 1, &mask, &cycle_x, &seen_x, vel, pos);
    find_cycle(step, 2, &mask, &cycle_y, &seen_y, &vel[1], &pos[1]);
    find_cycle(step, 4, &mask, &cycle_z, &seen_z, &vel[2], &pos[2]);
  }
  printf("%" PRIu64 "\n", lcm3(cycle_x, cycle_y, cycle_z));
}
