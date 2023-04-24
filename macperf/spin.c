#include <stdbool.h>
#include <stdlib.h>
#include <sys/time.h>

static double get_timestamp(void) {
  struct timeval now;
  gettimeofday(&now, NULL);
  return (double)now.tv_sec + (double)now.tv_usec / (1000.0 * 1000.0);
}

int main(int argc, char* argv[]) {
  double length_in_seconds = 1;

  if (argc > 1)
    length_in_seconds = strtod(argv[1], NULL);

  double begin = get_timestamp();

  while (true) {
    // stop when time is up
    double now = get_timestamp();
    if (now - begin > length_in_seconds)
      break;
  }
}
