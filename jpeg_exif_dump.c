#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdarg.h>
#include <stdbool.h>
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

static uint16_t be_uint16(uint8_t* p) {
  return p[0] << 8 | p[1];
}

static uint32_t be_uint32(uint8_t* p) {
  return be_uint16(p) << 16 | be_uint16(p + 2);
}

static uint16_t le_uint16(uint8_t* p) {
  return p[1] << 8 | p[0];
}

static uint32_t le_uint32(uint8_t* p) {
  return le_uint16(p + 2) << 16 | le_uint16(p);
}

// TIFF dumping ///////////////////////////////////////////////////////////////

enum TiffDataFormat {
  kUnsignedByte = 1,
  kAscii = 2,
  kUnsignedShort = 3,
  kUnsignedLong = 4,
  kUnsignedRational = 5,
  kSignedByte = 6,
  kUndefined = 7,
  kSignedShort = 8,
  kSignedLong = 9,
  kSignedRational = 10,
  kFloat = 11,
  kDouble = 11,
  kLastEntry = kDouble,
};

static int TiffDataFormatSizes[] = {
    -1,
    1,   // kUnsignedByte
    1,   // kAscii
    2,   // kUnsignedShort
    4,   // kUnsignedLong
    8,   // kUnsignedRational
    1,   // kSignedByte
    -1,  // kUndefined
    2,   // kSignedShort
    4,   // kSignedLong
    8,   // kSignedRational
    4,   // kFloat
    8,   // kDouble
};

static const char* TiffDataFormatNames[] = {
    "",
    "unsigned byte",
    "ascii",
    "unsigned short",
    "unsigned long",
    "unsigned rational",
    "signed byte",
    "undefined",
    "signed short",
    "signed long",
    "signed rational",
    "float",
    "double",
};

static void tiff_dump(uint8_t* begin, uint8_t* end) {
  ssize_t size = end - begin;
  if (size < 8) {
    fprintf(stderr, "tiff data should be at least 8 bytes, is %zu\n", size);
    return;
  }

  enum {
    kLittle,
    kBig,
  } endianness;
  if (strncmp((char*)begin, "II", 2) == 0)
    endianness = kLittle;
  else if (strncmp((char*)begin, "MM", 2) == 0)
    endianness = kBig;
  else {
    fprintf(stderr, "unknown endianness id '%.2s'\n", begin);
    return;
  }

  uint16_t (*uint16)(uint8_t*);
  uint32_t (*uint32)(uint8_t*);
  if (endianness == kBig) {
    uint16 = be_uint16;
    uint32 = be_uint32;
  } else {
    uint16 = le_uint16;
    uint32 = le_uint32;
  }

  uint16_t check = uint16(begin + 2);
  if (check != 0x2a) {
    fprintf(stderr, "expected 0x2a, got 0x%x\n", check);
    return;
  }

  // IFD is short for 'Image File Directory'.
  uint32_t ifd_offset = uint32(begin + 4);
  if (size - ifd_offset < 6) {
    fprintf(stderr, "IFD needs at least 6 bytes, has %zu\n", size - ifd_offset);
    return;
  }
  if (ifd_offset != 8) {
    fprintf(stderr, "IFD offset is surprisingly not 8 but %u\n", ifd_offset);
    fprintf(stderr, "continuing anyway\n");
  }

  uint16_t num_ifd_entries = uint16(begin + ifd_offset);
  if (size - ifd_offset - 6 < num_ifd_entries * 12) {
    fprintf(stderr, "%d IFD entries need least %d bytes, have %zu\n",
            num_ifd_entries, num_ifd_entries * 12, size - ifd_offset - 2);
    return;
  }
  for (int i = 0; i < num_ifd_entries; ++i) {
    size_t this_ifd_offset = ifd_offset + 2 + i * 12;
    uint16_t tag = uint16(begin + this_ifd_offset);
    uint16_t format = uint16(begin + this_ifd_offset + 2);
    uint32_t count = uint32(begin + this_ifd_offset + 4);

    if (format == 0 || format > kLastEntry) {
      fprintf(stderr, "  ifd entry %i invalid format %i, ignoring\n", i,
              format);
      continue;
    }

    size_t total_size = count * TiffDataFormatSizes[format];
    fprintf(stderr, "  tag %d format %d (%s): data size %zu\n", tag, format,
            TiffDataFormatNames[format], total_size);
  }

  uint32_t next_ifd_offset =
      uint32(begin + ifd_offset + 2 + num_ifd_entries * 12);
  fprintf(stderr, "  next IFD at %d\n", next_ifd_offset);
}

// JPEG dumping ///////////////////////////////////////////////////////////////

static void jpeg_dump_exif(uint8_t* begin, uint16_t size) {
  // https://www.cipa.jp/std/documents/e/DC-X008-Translation-2019-E.pdf
  tiff_dump(begin + 8, begin + size);
}

static void jpeg_dump_icc(uint8_t* begin, uint16_t size) {
  // https://www.color.org/technotes/ICC-Technote-ProfileEmbedding.pdf
  //   (In particular, an ICC profile can be split across several APP2 marker
  //   chunks: "...a mechanism is required to break the profile into chunks...")
  // https://www.color.org/specification/ICC.1-2022-05.pdf
  // TODO
}

static void jpeg_dump_xmp(uint8_t* begin, uint16_t size) {
  // http://www.npes.org/pdf/xmpspecification-Jun05.pdf
  // TODO
}

static const char* jpeg_dump_app_id(uint8_t* begin,
                                    uint8_t* end,
                                    bool has_size,
                                    uint16_t size) {
  if (!has_size) {
    printf("  no size?!\n");
    return NULL;
  }

  if (size < 2) {
    printf("  invalid size, must be at least 2, but was %u\n", size);
    return NULL;
  }

  if (end - begin < size) {
    printf("  size is %u, but only %zu bytes left\n", size, end - begin);
    return NULL;
  }

  const char* app_id = (const char*)(begin + 2);
  size_t id_len = strnlen(app_id, size - 2);
  if (begin[2 + id_len] != '\0') {
    printf("  no zero-terminated id found\n");
    return NULL;
  }

  printf("  app id: '%s'\n", app_id);
  return app_id;
}

struct Options {
  bool scan;
};

static void jpeg_dump(struct Options* options, uint8_t* begin, uint8_t* end) {
  uint8_t* cur = begin;
  while (cur < end) {
    uint8_t b0 = *cur++;
    if (b0 != 0xff)
      continue;

    if (cur >= end)
      break;

    uint8_t b1 = *cur;
    if (b1 == 0xff)
      continue;
    cur++;

    if (b1 == 0)
      continue;

    printf("%02x%02x at offset %ld", b0, b1, cur - begin - 2);

    bool has_size =
        b1 != 0xd8 && b1 != 0xd9 && (b1 & 0xf8) != 0xd0 && end - cur >= 2;
    uint16_t size = 0;
    if (has_size) {
      size = be_uint16(cur);
      printf(", size %u", size);
    }

    // https://www.disktuna.com/list-of-jpeg-markers/
    switch (b1) {
      case 0xc0:
        printf(": Start Of Frame, baseline DCT (SOF0)\n");
        break;
      case 0xc2:
        printf(": Start Of Frame, progressive DCT (SOF2)\n");
        break;
      case 0xc4:
        printf(": Define Huffman Tables (DHT)\n");
        break;
      case 0xd0:
      case 0xd1:
      case 0xd2:
      case 0xd3:
      case 0xd4:
      case 0xd5:
      case 0xd6:
      case 0xd7:
        printf(": Restart (RST%d)\n", b1 - 0xd0);
        break;
      case 0xd8:
        printf(": Start Of Image (SOI)\n");
        break;
      case 0xd9:
        printf(": End Of Image (EOI)\n");
        break;
      case 0xda:
        printf(": Start Of Scan (SOS)\n");
        break;
      case 0xdb:
        printf(": Define Quantization Table(s) (DQT)\n");
        break;
      case 0xdd:
        printf(": Define Restart Interval (DRI)\n");
        if (!has_size)
          printf("  missing size\n");
        else if (size != 4)
          printf("  expected size 4, got %d\n", size);
        else
          printf("  %d macroblocks\n", be_uint16(cur + 2));
        break;
      case 0xe0:
        printf(": JPEG/JFIF Image segment (APP0)\n");
        jpeg_dump_app_id(cur, end, has_size, size);
        break;
      case 0xe1: {
        printf(": EXIF Image segment (APP1)\n");

        const char* app_id = jpeg_dump_app_id(cur, end, has_size, size);
        if (strcmp(app_id, "Exif") == 0)
          jpeg_dump_exif(cur, size);
        else if (strcmp(app_id, "http://ns.adobe.com/xap/1.0/") == 0)
          jpeg_dump_xmp(cur, size);
        break;
      }
      case 0xe2:
      case 0xe3:
      case 0xe4:
      case 0xe5:
      case 0xe6:
      case 0xe7:
      case 0xe8:
      case 0xe9:
      case 0xea:
      case 0xeb:
      case 0xec:
      case 0xed:
      case 0xee:
      case 0xef: {
        printf(": Application Segment (APP%d)\n", b1 - 0xe0);

        const char* app_id = jpeg_dump_app_id(cur, end, has_size, size);
        if (b1 == 0xe2 && strcmp(app_id, "ICC_PROFILE") == 0)
          jpeg_dump_icc(cur, size);
        break;
      }
      case 0xfe:
        printf(": Comment (COM)\n");
        break;
      default:
        printf("\n");
    }

    if (!options->scan && has_size) {
      cur += size;
      if (cur >= end)
        printf("marker length went %zu bytes past data end\n", cur - end + 1);
    }
  }
}

int main(int argc, char* argv[]) {
  // Parse options.
  struct Options options = {};
  struct option getopt_options[] = {
      {"scan", no_argument, NULL, 's'},
      {},
  };
  int opt;
  while ((opt = getopt_long(argc, argv, "s", getopt_options, NULL)) != -1) {
    switch (opt) {
      case 's':
        options.scan = true;
        break;
    }
  }
  argv += optind;
  argc -= optind;

  if (argc != 1)
    fatal("expected args == 1, got %d\n", argc);

  const char* in_name = argv[0];

  // Read input.
  int in_file = open(in_name, O_RDONLY);
  if (!in_file)
    fatal("Unable to read \'%s\'\n", in_name);

  struct stat in_stat;
  if (fstat(in_file, &in_stat))
    fatal("Failed to stat \'%s\'\n", in_name);

  void* contents = mmap(
      /*addr=*/0, in_stat.st_size, PROT_READ, MAP_SHARED, in_file,
      /*offset=*/0);
  if (contents == MAP_FAILED)
    fatal("Failed to mmap: %d (%s)\n", errno, strerror(errno));

  jpeg_dump(&options, contents, contents + in_stat.st_size);

  munmap(contents, in_stat.st_size);
  close(in_file);
}
