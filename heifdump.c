#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdnoreturn.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(__clang__) || defined(__GNUC__)
#define PRINTF(a, b) __attribute__((format(printf, a, b)))
#else
#define PRINTF(a, b)
#endif

PRINTF(1, 2) static noreturn void fatal(const char* msg, ...) {
  va_list args;
  va_start(args, msg);
  vfprintf(stderr, msg, args);
  va_end(args);
  exit(1);
}

static uint16_t be_uint16(const uint8_t* p) {
  return (uint16_t)(p[0] << 8) | p[1];
}

static uint32_t be_uint32(const uint8_t* p) {
  return (uint32_t)(be_uint16(p) << 16) | be_uint16(p + 2);
}

static uint64_t be_uint64(const uint8_t* p) {
  return ((uint64_t)be_uint32(p) << 32) | be_uint32(p + 4);
}

static uint64_t heif_dump_box(const uint8_t* begin, const uint8_t* end) {
  // XXX good spec link :/
  size_t size = (size_t)(end - begin);
  if (size < 8)
    fatal("heif box must be at least 8 bytes but is %zu\n", size);

  uint64_t length = be_uint32(begin);
  uint32_t type = be_uint32(begin + 4);

  if (length == 1) {
    if (size < 16)
      fatal("heif box wth extended size must be at least 16 bytes but is %zu\n",
            size);
    length = be_uint64(begin + 8);
  } else if (length == 0) {
    // length == 0: Box extends to end of file.
    length = size;
  }

  if (size < length)
    fatal("heif box with size %" PRIu64 " must be at least %" PRIu64
          " bytes but is %zu\n",
          length, length, size);

  printf("box '%.4s' (%08x), length %" PRIu64 "\n", begin + 4, type, length);

  return length;
}

static void heif_dump(const uint8_t* begin, const uint8_t* end) {
  // XXX good spec link :/

  while (begin < end) {
    uint64_t box_size = heif_dump_box(begin, end);

    if (box_size > (size_t)(end - begin))
      fatal("box has size %" PRIu64 " but only %zu bytes left\n", box_size,
            end - begin);

    begin += box_size;
  }
}

int main(int argc, char* argv[]) {
  if (argc != 2)
    fatal("expected 1 arg, got %d\n", argc);

  const char* in_name = argv[1];

  // Read input.
  int in_file = open(in_name, O_RDONLY);
  if (!in_file)
    fatal("Unable to read \'%s\'\n", in_name);

  struct stat in_stat;
  if (fstat(in_file, &in_stat))
    fatal("Failed to stat \'%s\'\n", in_name);
  if (in_stat.st_size < 0)
    fatal("Negative st_size?? (%jd)\n", (intmax_t)in_stat.st_size);

  void* contents = mmap(
      /*addr=*/0, (size_t)in_stat.st_size, PROT_READ, MAP_SHARED, in_file,
      /*offset=*/0);
  if (contents == MAP_FAILED)
    fatal("Failed to mmap: %d (%s)\n", errno, strerror(errno));

  heif_dump(contents, (uint8_t*)contents + in_stat.st_size);

  munmap(contents, (size_t)in_stat.st_size);
  close(in_file);
}
