#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static void fatal(const char* msg, ...) {
  va_list args;
  va_start(args, msg);
  vfprintf(stderr, msg, args);
  va_end(args);
  exit(1);
}

static void dump(uint8_t* begin, uint8_t* end) {
  uint8_t* cur = begin;
  while (cur < end) {
    uint8_t b0 = *cur++;
    if (b0 != 0xff)
      continue;

    if (cur >= end)
      break;

    uint8_t b1 = *cur++;
    if (b1 == 0)
      continue;

    printf("%02x%02x at offset %ld\n", b0, b1, cur - begin - 2);
  }
}

int main(int argc, char* argv[]) {
  const char* in_name = argv[1];

  // Read input.
  int in_file = open(in_name, O_RDONLY);
  if (!in_file)
    fatal("Unable to read \'%s\'\n", in_name);

  struct stat in_stat;
  if (fstat(in_file, &in_stat))
    fatal("Failed to stat \'%s\'\n", in_name);

  void* contents = mmap(
      /*addr=*/0, in_stat.st_size,
      PROT_READ, MAP_SHARED, in_file, /*offset=*/0);
  if (contents == MAP_FAILED)
    fatal("Failed to mmap: %d (%s)\n", errno, strerror(errno));

  dump(contents, contents + in_stat.st_size);

  munmap(contents, in_stat.st_size);
  close(in_file);
}
