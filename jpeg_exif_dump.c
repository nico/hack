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

// https://www.loc.gov/preservation/digital/formats/content/tiff_tags.shtml
const char* tiff_tag_name(uint16_t tag) {
  switch (tag) {
    case 256:
      return "ImageWidth";
    case 257:
      return "ImageHeight";
    case 259:
      return "Compression";
    case 270:
      return "ImageDescription";
    case 271:
      return "Make";
    case 272:
      return "Model";
    case 274:
      return "Orientation";
    case 282:
      return "XResolution";
    case 283:
      return "YResolution";
    case 296:
      return "ResolutionUnit";
    case 305:
      return "Software";
    case 306:
      return "DateTime";
    case 513:
      return "JPEGInterchangeFormat";
    case 514:
      return "JPEGInterchangeFormatLength";
    case 531:
      return "YCbCrPositioning";

    // EXIF Private IFD tags
    // https://exiv2.org/tags.html
    case 33434:
      return "ExposureTime (seconds)";
    case 33437: return "FNumber";
    case 34850: return "ExposureProgram";
    case 34853:
      return "GPSInfo";
    case 34855: return "ISOSpeedRatings";
    case 36864: return "ExifVersion";
    case 36867: return "DateTimeOriginal";
    case 36868: return "DateTimeDigitized";
    case 36880: return "OffsetTime";
    case 36881: return "OffsetTimeOriginal";
    case 36882: return "OffsetTimeDigitized";
    case 37121: return "ComponentsConfiguration";
    case 37377: return "ShutterSpeedValue";
    case 37378: return "ApertureValue";
    case 37379: return "BrightnessValue";
    case 37380: return "ExposureBiasValue";
    case 37381: return "MaxApertureValue";
    case 37382: return "SubjectDistance (meters)";
    case 37383: return "MeteringMode";
    case 37385: return "Flash";
    case 37386: return "FocalLength";
    case 37520: return "SubsecTime";
    case 37521: return "SubsecTimeOriginal";
    case 37522: return "SubsecTimeDigitized";
    case 40960: return "FlashpixVersion";
    case 40961: return "ColorSpace";
    case 40962: return "PixelXDimension";
    case 40963: return "PixelYDimension";
    case 40965: return "InteroperabilityIFD";
    case 41495: return "SensingMethod";
    case 41729: return "SceneType";
    case 41985: return "CustomRendered";
    case 41986: return "ExposureMode";
    case 41987: return "WhiteBalance";
    case 41988: return "DigitalZoomRatio";
    case 41989: return "FocalLengthIn35mmFilm";
    case 41990: return "SceneCaptureType";
    case 41992: return "Contrast";
    case 41993: return "Saturation";
    case 41994: return "Sharpness";
    case 41996: return "SubjectDistanceRange";
    case 42035: return "LensMake";
    case 42036: return "LensModel";
    case 42080: return "CompositeImage";

    // Private tags
    case 34665:
      return "Exif IFD";
    default:
      return NULL;
  }
}

// Returns offset to next IFD, or 0 if none.
static uint32_t tiff_dump_one_ifd(uint8_t* begin,
                                  uint8_t* end,
                                  uint32_t ifd_offset,
                                  uint16_t (*uint16)(uint8_t*),
                                  uint32_t (*uint32)(uint8_t*)) {
  ssize_t size = end - begin;

  if (size - ifd_offset < 6) {
    fprintf(stderr, "IFD needs at least 6 bytes, has %zu\n", size - ifd_offset);
    return 0;
  }
  uint16_t num_ifd_entries = uint16(begin + ifd_offset);
  if (size - ifd_offset - 6 < num_ifd_entries * 12) {
    fprintf(stderr, "%d IFD entries need least %d bytes, have %zu\n",
            num_ifd_entries, num_ifd_entries * 12, size - ifd_offset - 2);
    return 0;
  }

  uint32_t exif_ifd_offset = 0;
  uint32_t gps_info_ifd_offset = 0;

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
    uint32_t data_offset = total_size <= 4
                               ? this_ifd_offset + 8
                               : uint32(begin + this_ifd_offset + 8);
    void* data = begin + data_offset;
    fprintf(stderr, "  tag %d", tag);
    const char* tag_name;
    if ((tag_name = tiff_tag_name(tag)))
      fprintf(stderr, " (%s)", tag_name);
    fprintf(stderr, " format %d (%s): data size %zu", format,
            TiffDataFormatNames[format], total_size);

    // FIXME: print other formats
    if (format == kUnsignedByte && count == 1)
      fprintf(stderr, ": %u", *(uint8_t*)data);
    else if (format == kAscii)
      fprintf(stderr, ": '%.*s'", count, (char*)data);
    else if (format == kUnsignedShort && count == 1)
      fprintf(stderr, ": %u", uint16(data));
    else if (format == kUnsignedLong && count == 1)
      fprintf(stderr, ": %u", uint32(data));
    else if (format == kUnsignedRational && count == 1)
      fprintf(stderr, ": %u/%u", uint32(data), uint32(data + 4));

    if (tag == 34665 && format == kUnsignedLong && count == 1)
      exif_ifd_offset = uint32(data);
    else if (tag == 34853 && format == kUnsignedLong && count == 1)
      gps_info_ifd_offset = uint32(data);

    fprintf(stderr, "\n");
  }

  if (exif_ifd_offset != 0) {
    fprintf(stderr, "  exif IFD:\n");
    tiff_dump_one_ifd(begin, end, exif_ifd_offset, uint16, uint32);
  }
  if (gps_info_ifd_offset != 0) {
    fprintf(stderr, "  GPSInfo IFD:\n");
    tiff_dump_one_ifd(begin, end, gps_info_ifd_offset, uint16, uint32);
  }

  uint32_t next_ifd_offset =
      uint32(begin + ifd_offset + 2 + num_ifd_entries * 12);
  if (next_ifd_offset != 0)
    fprintf(stderr, "  next IFD at %d\n", next_ifd_offset);

  return next_ifd_offset;
}

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
  if (check != 42) {
    fprintf(stderr, "expected 0x2a, got 0x%x\n", check);
    return;
  }

  // IFD is short for 'Image File Directory'.
  uint32_t ifd_offset = uint32(begin + 4);
  if (ifd_offset != 8) {
    fprintf(stderr, "IFD offset is surprisingly not 8 but %u\n", ifd_offset);
    fprintf(stderr, "continuing anyway\n");
  }

  do {
    uint32_t next_ifd_offset =
        tiff_dump_one_ifd(begin, end, ifd_offset, uint16, uint32);
    ifd_offset = next_ifd_offset;
  } while (ifd_offset != 0);
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
