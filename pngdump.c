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

static void png_dump_chunk_IHDR(const uint8_t* begin, uint32_t size) {
  if (size != 13) {
    fprintf(stderr, "IHDR should be 13 bytes, was %u bytes\n", size);
    return;
  }

  uint32_t width = be_uint32(begin);
  uint32_t height = be_uint32(begin + 4);
  uint8_t bit_depth = begin[8];
  uint8_t color_type = begin[9];
  uint8_t compression_method = begin[10];
  uint8_t filter_method = begin[11];
  uint8_t interlace_method = begin[12];

  if (width & 0x80000000) {
    fprintf(stderr, "width %x invalidly has top bit set\n", width);
    return;
  }
  if (height & 0x80000000) {
    fprintf(stderr, "height %x invalidly has top bit set\n", height);
    return;
  }

  if (bit_depth != 1 && bit_depth != 2 && bit_depth != 4 && bit_depth != 8 &&
      bit_depth != 16) {
    fprintf(stderr, "bit depth must be one of 1, 2, 4, 8, 16 but was %d\n",
            bit_depth);
    return;
  }

  if (color_type != 0 && color_type != 2 && color_type != 3 &&
      color_type != 4 && color_type != 6) {
    fprintf(stderr, "color type must be one of 0, 2, 3, 4, 6 but was %d\n",
            color_type);
    return;
  }

  if ((color_type == 2 || color_type == 4 || color_type == 6) &&
      (bit_depth != 8 && bit_depth != 16)) {
    fprintf(stderr,
            "for color type %d, bit depth must be one of 8, 16 but was %d\n",
            color_type, bit_depth);
    return;
  }
  if (color_type == 3 && bit_depth == 16) {
    fprintf(
        stderr,
        "for color type 3, bit depth must be one of 1, 2, 4, 8 but was 16\n");
    return;
  }

  if (compression_method != 0) {
    fprintf(stderr, "IHDR: compression method must be 0 but was %d\n",
            compression_method);
    return;
  }

  if (filter_method != 0) {
    fprintf(stderr, "filter method must be 0 but was %d\n", filter_method);
    return;
  }

  if (interlace_method != 0 && interlace_method != 1) {
    fprintf(stderr, "interlace method must be 0 or 1 but was %d\n",
            interlace_method);
    return;
  }

  printf("  %ux%u pixels, %d bits per pixel\n", width, height, bit_depth);

  printf("  ");
  if (color_type & 1)
    printf("palletized, ");
  if (color_type & 2)
    printf("rgb");
  else
    printf("grayscale");
  if (color_type & 4)
    printf(", has alpha");
  printf("\n");

  printf("  deflate-compressed\n");
  printf("  adaptive filtering with five basic filter types\n");
  if (interlace_method == 0)
    printf("  no interlacing\n");
  else
    printf("  adam7 interlacing\n");
}

static void png_dump_chunk_iCCP(const uint8_t* begin, uint32_t size) {
  size_t profile_name_len_max = 80;
  if (size < profile_name_len_max)
    profile_name_len_max = size;
  size_t profile_name_len = strnlen((const char*)begin, profile_name_len_max);
  if (profile_name_len == profile_name_len_max) {
    fprintf(stderr, "profile name should be at most %zu bytes, was longer\n",
            profile_name_len_max);
    return;
  }

  if (size - profile_name_len < 2) {
    fprintf(stderr,
            "profile with name with length %zd should be at least %zu bytes, "
            "but is %d\n",
            profile_name_len, profile_name_len + 2, size);
    return;
  }

  // `begin[profile_name_len]` is the profile names's \0 terminator.
  uint8_t compression_method = begin[profile_name_len + 1];
  if (compression_method != 0) {
    fprintf(stderr, "iCCP: compression method must be 0 but was %d\n",
            compression_method);
    return;
  }

  uint32_t compressed_size = size - (uint32_t)profile_name_len - 2;
  printf("  name: '%s'\n", begin);
  printf("  deflate-compressed %u bytes\n", compressed_size);
}

static void png_dump_chunk_pHYs(const uint8_t* begin, uint32_t size) {
  if (size != 9) {
    fprintf(stderr, "pHYs should be 9 bytes, was %u bytes\n", size);
    return;
  }

  uint32_t x_pixels_per_unit = be_uint32(begin);
  uint32_t y_pixels_per_unit = be_uint32(begin + 4);
  uint8_t unit = begin[8];

  if (x_pixels_per_unit & 0x80000000) {
    fprintf(stderr, "x %x invalidly has top bit set\n", x_pixels_per_unit);
    return;
  }
  if (y_pixels_per_unit & 0x80000000) {
    fprintf(stderr, "y %x invalidly has top bit set\n", y_pixels_per_unit);
    return;
  }

  if (unit != 0 && unit != 1) {
    fprintf(stderr, "unit should be 0 or 1, was %d\n", unit);
    return;
  }

  const char* unit_string = unit == 0 ? "unknown unit" : "meter";
  printf("  %d pixels per %s in x direction\n", x_pixels_per_unit, unit_string);
  printf("  %d pixels per %s in y direction\n", y_pixels_per_unit, unit_string);
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

  switch (type) {
    case 0x49484452:  // 'IHDR'
      png_dump_chunk_IHDR(begin + 8, length);
      break;
    case 0x69434350:  // 'iCCP'
      png_dump_chunk_iCCP(begin + 8, length);
      break;
    case 0x70485973:  // 'pHYs'
      png_dump_chunk_pHYs(begin + 8, length);
      break;
  }

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
