#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
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

static uint32_t png_dump_chunk(const uint8_t* begin, const uint8_t* end) {
  size_t size = (size_t)(end - begin);
  if (size < 12)
    fatal("png chunk must be at least 12 bytes but is %zu\n", size);

  uint32_t length = be_uint32(begin);
  if (size - 12 < length)
    fatal("png chunk with size %d must be at least %d bytes but is %zu\n",
          length, 12 + length, size);

  uint32_t type = be_uint32(begin + 4);
  uint32_t crc = be_uint32(begin + 8 + length);

  printf("chunk '%.4s' (%08x), length %d, crc %08x\n", begin + 4, type, length,
         crc);

  return 12 + length;
}

static void png_dump(const uint8_t* begin, const uint8_t* end) {
  const char png_header[] = {(char)0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};

  size_t size = (size_t)(end - begin);
  if (size < sizeof(png_header)) {
    fatal("png file must be at least %zu bytes but is %zu\n",
          sizeof(png_header), size);
  }

  if (strncmp(png_header, (const char*)begin, sizeof(png_header)) != 0)
    fatal("file does not start with png header\n");

  begin += sizeof(png_header);

  while (begin < end) {
    uint32_t chunk_size = png_dump_chunk(begin, end);

    if (chunk_size > (size_t)(end - begin))
      fatal("chunk has size %u but only %zu bytes left\n", chunk_size,
            end - begin);

    begin += chunk_size;
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

  png_dump(contents, (uint8_t*)contents + in_stat.st_size);

  munmap(contents, (size_t)in_stat.st_size);
  close(in_file);
}
