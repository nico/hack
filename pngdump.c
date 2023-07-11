#include <errno.h>
#include <fcntl.h>
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

static bool png_check_size(const char* name, uint32_t size, uint32_t expected) {
  if (size != expected)
    fprintf(stderr, "%s was %u bytes, but expected %u\n", name, size, expected);
  return size == expected;
}

static void png_dump_chunk_IHDR(const uint8_t* begin, uint32_t size) {
  // https://w3c.github.io/PNG-spec/#11IHDR
  if (!png_check_size("IHDR", size, 13))
    return;

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

static void png_dump_chunk_cHRM(const uint8_t* begin, uint32_t size) {
  // https://w3c.github.io/PNG-spec/#11cHRM
  if (!png_check_size("cHRM", size, 32))
    return;

  uint32_t whitepoint_x = be_uint32(begin);
  uint32_t whitepoint_y = be_uint32(begin + 4);
  uint32_t red_x = be_uint32(begin + 8);
  uint32_t red_y = be_uint32(begin + 12);
  uint32_t green_x = be_uint32(begin + 16);
  uint32_t green_y = be_uint32(begin + 20);
  uint32_t blue_x = be_uint32(begin + 24);
  uint32_t blue_y = be_uint32(begin + 28);

  // FIXME: complain if top bit is set
  printf("  whitepoint: %f %f\n", whitepoint_x / 100000.0,
         whitepoint_y / 100000.0);
  printf("  red: %f %f\n", red_x / 100000.0, red_y / 100000.0);
  printf("  green: %f %f\n", green_x / 100000.0, green_y / 100000.0);
  printf("  blue: %f %f\n", blue_x / 100000.0, blue_y / 100000.0);
}

static void png_dump_chunk_cICP(const uint8_t* begin, uint32_t size) {
  // https://www.w3.org/TR/png/#cICP-chunk
  if (!png_check_size("cICP", size, 4))
    return;

  uint8_t color_primaries = begin[0];
  uint8_t transfer_function = begin[1];
  uint8_t matrix_coefficients = begin[2];
  uint8_t video_full_range_flag = begin[3];

  printf("  color primaries: %d\n", color_primaries);
  printf("  transfer function: %d\n", transfer_function);
  printf("  matrix coefficients: %d\n", matrix_coefficients);
  printf("  video full range flag: %d\n", video_full_range_flag);
}

static void png_dump_chunk_eXIf(const uint8_t* begin, uint32_t size) {
  if (size > 0xffff - 8)
    printf("  (larger than what fits in a jpeg APP1 Exif segment)\n");

  // FIXME: call tiff_dump(begin, begin + size)
  (void)begin;
}

static void png_dump_chunk_gAMA(const uint8_t* begin, uint32_t size) {
  // https://w3c.github.io/PNG-spec/#11gAMA
  if (!png_check_size("gAMA", size, 4))
    return;

  uint32_t gamma = be_uint32(begin);
  if (gamma & 0x80000000) {
    fprintf(stderr, "gamma %x invalidly has top bit set\n", gamma);
    return;
  }

  printf("  gamma %d (%f)\n", gamma, gamma / 100000.0);
}

static void png_dump_chunk_iCCP(const uint8_t* begin, uint32_t size) {
  // https://w3c.github.io/PNG-spec/#11iCCP
  size_t profile_name_len_max = 80;
  if (size < profile_name_len_max)
    profile_name_len_max = size;
  size_t profile_name_len = strnlen((const char*)begin, profile_name_len_max);
  if (profile_name_len == 0) {
    fprintf(stderr, "profile name should be at least 1 byte, was 0\n");
    return;
  }
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

static void png_dump_chunk_iTXt(const uint8_t* begin, uint32_t size) {
  // https://w3c.github.io/PNG-spec/#11iTXt
  size_t keyword_len_max = 80;
  if (size < keyword_len_max)
    keyword_len_max = size;
  size_t keyword_len = strnlen((const char*)begin, keyword_len_max);
  if (keyword_len == 0) {
    fprintf(stderr, "keyword should be at least 1 byte, was 0\n");
    return;
  }
  if (keyword_len == keyword_len_max) {
    fprintf(stderr, "keyword should be at most %zu bytes, was longer\n",
            keyword_len_max);
    return;
  }

  if (size - keyword_len < 4) {
    fprintf(stderr,
            "profile with name with length %zd should be at least %zu bytes, "
            "but is %d\n",
            keyword_len, keyword_len + 4, size);
    return;
  }

  // `begin[keyword_len]` is the keyword's \0 terminator.
  uint8_t compression_flag = begin[keyword_len + 1];
  if (compression_flag != 0 && compression_flag != 1) {
    fprintf(stderr, "iTXt: compression flag must be 0 or 1 but was %d\n",
            compression_flag);
    return;
  }

  uint8_t compression_method = begin[keyword_len + 2];
  if (compression_method != 0) {
    fprintf(stderr, "iTXt: compression method must be 0 but was %d\n",
            compression_method);
    return;
  }

  size_t language_tag_len_max = size - keyword_len - 3;
  size_t language_tag_len =
      strnlen((const char*)begin + keyword_len + 3, language_tag_len_max);
  if (language_tag_len == language_tag_len_max) {
    fprintf(stderr, "language tag overflows tag\n");
    return;
  }

  size_t translated_keyword_len_max =
      size - keyword_len - 3 - language_tag_len - 1;
  size_t translated_keyword_len =
      strnlen((const char*)begin + keyword_len + 3 + language_tag_len + 1,
              translated_keyword_len_max);
  if (translated_keyword_len == translated_keyword_len_max) {
    fprintf(stderr, "translated keyword overflows tag\n");
    return;
  }

  unsigned data_size = size - (unsigned)keyword_len - 3 -
                       (unsigned)language_tag_len - 1 -
                       (unsigned)translated_keyword_len - 1;
  // FIXME: Convert keyword latin1 to utf-8.
  // Translated keyword and main text are both utf-8.
  printf("  name: '%s'\n", begin);
  printf("  language tag: '%s'\n", begin + keyword_len + 3);
  printf("  localized name: '%s'\n",
         begin + keyword_len + 3 + language_tag_len + 1);

  if (compression_flag == 0) {
    printf("  uncompressed %u bytes\n", data_size);
    printf("  text: '%.*s'\n", data_size,
           begin + keyword_len + 3 + language_tag_len + 1 +
               translated_keyword_len + 1);
  } else {
    printf("  deflate-compressed %u bytes\n", data_size);
    // FIXME: deflate compressed data, print.
  }
}

static void png_dump_chunk_pHYs(const uint8_t* begin, uint32_t size) {
  // https://w3c.github.io/PNG-spec/#11pHYs
  if (!png_check_size("pHYs", size, 9))
    return;

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

static void png_dump_chunk_sRGB(const uint8_t* begin, uint32_t size) {
  // https://w3c.github.io/PNG-spec/#11sRGB
  if (!png_check_size("sRGB", size, 1))
    return;

  uint8_t rendering_intent = *begin;
  switch (rendering_intent) {
    case 0:
      printf("  rendering intent: perceptual\n");
      break;
    case 1:
      printf("  rendering intent: relative colorimetric\n");
      break;
    case 2:
      printf("  rendering intent: saturation\n");
      break;
    case 3:
      printf("  rendering intent: absolute colorimetric\n");
      break;
    default:
      fprintf(stderr, "rendering intent should be 0, 1, 2, or 3, but was %d\n",
              rendering_intent);
  }
}

static void png_dump_chunk_tIME(const uint8_t* begin, uint32_t size) {
  // https://w3c.github.io/PNG-spec/#11tIME
  if (!png_check_size("tIME", size, 7))
    return;

  uint16_t year = be_uint16(begin);
  uint8_t month = begin[2];
  uint8_t day = begin[3];
  uint8_t hours = begin[4];
  uint8_t minutes = begin[5];
  uint8_t seconds = begin[6];
  printf("  last modified: %04d-%02d-%02dT%02d:%02d:%02dZ\n", year, month, day,
         hours, minutes, seconds);
}

static void png_dump_chunk_tEXt(const uint8_t* begin, uint32_t size) {
  // https://w3c.github.io/PNG-spec/#11tEXt
  size_t keyword_len_max = 80;
  if (size < keyword_len_max)
    keyword_len_max = size;
  size_t keyword_len = strnlen((const char*)begin, keyword_len_max);
  if (keyword_len == 0) {
    fprintf(stderr, "keyword should be at least 1 byte, was 0\n");
    return;
  }
  if (keyword_len == keyword_len_max) {
    fprintf(stderr, "keyword should be at most %zu bytes, was longer\n",
            keyword_len_max);
    return;
  }

  if (size - keyword_len < 1) {
    fprintf(stderr,
            "profile with name with length %zd should be at least %zu bytes, "
            "but is %d\n",
            keyword_len, keyword_len + 1, size);
    return;
  }
  unsigned data_size = size - (unsigned)keyword_len - 1;

  // FIXME: Convert from latin1 to utf-8.
  // `begin[keyword_len]` is the keyword's \0 terminator.
  printf("  '%s': '", begin);
  for (size_t i = 0; i < data_size; ++i) {
    unsigned char c = begin[keyword_len + 1 + i];
    if (c < 0x20)
      printf("\\%03o", c);
    else
      printf("%c", c);
  }
  printf("'\n");
}

static void png_dump_chunk_zTXt(const uint8_t* begin, uint32_t size) {
  // https://w3c.github.io/PNG-spec/#11zTXt
  size_t keyword_len_max = 80;
  if (size < keyword_len_max)
    keyword_len_max = size;
  size_t keyword_len = strnlen((const char*)begin, keyword_len_max);
  if (keyword_len == 0) {
    fprintf(stderr, "keyword should be at least 1 byte, was 0\n");
    return;
  }
  if (keyword_len == keyword_len_max) {
    fprintf(stderr, "keyword should be at most %zu bytes, was longer\n",
            keyword_len_max);
    return;
  }

  if (size - keyword_len < 2) {
    fprintf(stderr,
            "profile with name with length %zd should be at least %zu bytes, "
            "but is %d\n",
            keyword_len, keyword_len + 2, size);
    return;
  }

  // `begin[keyword_len]` is the keyword's \0 terminator.
  uint8_t compression_method = begin[keyword_len + 1];
  if (compression_method != 0) {
    fprintf(stderr, "zTXt: compression method must be 0 but was %d\n",
            compression_method);
    return;
  }

  unsigned data_size = size - (unsigned)keyword_len - 2;
  // FIXME: Convert from latin1 to utf-8.
  printf("  name: '%s'\n", begin);
  printf("  deflate-compressed %u bytes\n", data_size);

  // FIXME: deflate compressed data, convert from latin1 to utf-8, print.
}

static uint32_t png_dump_chunk(const uint8_t* begin, const uint8_t* end) {
  // https://w3c.github.io/PNG-spec/#5Chunk-layout
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
    case 0x6348524d:  // 'cHRM'
      png_dump_chunk_cHRM(begin + 8, length);
      break;
    case 0x63494350:  // 'cICP'
      png_dump_chunk_cICP(begin + 8, length);
      break;
    case 0x65584966:  // 'eXIf'
      png_dump_chunk_eXIf(begin + 8, length);
      break;
    case 0x67414d41:  // 'gAMA'
      png_dump_chunk_gAMA(begin + 8, length);
      break;
    case 0x69434350:  // 'iCCP'
      png_dump_chunk_iCCP(begin + 8, length);
      break;
    case 0x69545874:  // 'iTXt'
      png_dump_chunk_iTXt(begin + 8, length);
      break;
    case 0x70485973:  // 'pHYs'
      png_dump_chunk_pHYs(begin + 8, length);
      break;
    case 0x73524742:  // 'sRGB'
      png_dump_chunk_sRGB(begin + 8, length);
      break;
    case 0x74455874:  // 'tEXt'
      png_dump_chunk_tEXt(begin + 8, length);
      break;
    case 0x74494d45:  // 'tIME'
      png_dump_chunk_tIME(begin + 8, length);
      break;
    case 0x7a545874:  // 'zTXt'
      png_dump_chunk_zTXt(begin + 8, length);
      break;
  }

  return 12 + length;
}

static void png_dump(const uint8_t* begin, const uint8_t* end) {
  // https://w3c.github.io/PNG-spec/#5DataRep
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
