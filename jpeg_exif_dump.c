#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdnoreturn.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

// Dumps metadata in jpeg files.
// Fairly complete:
// * Exif
// * XMP (missing custom dumpers for a few tags, such as gps pos)
// * ICC (RGB only for now)
// In progress:
// * MPF
// Still missing:
// * IPTC

static void print_usage(FILE* stream, const char* program_name) {
  fprintf(stream, "usage: %s [ options ] filename\n", program_name);
  fprintf(stream,
          "\n"
          "options:\n"
          "  -h  --help  print this message\n"
          "  -s  --scan  scan for jpeg markers in entire file\n"
          "              (by default, skips marker data)\n");
}

#if defined(__clang__) || defined(__GNUC__)
#define PRINTF(a, b) __attribute__((format(printf, a, b)))
#else
#define PRINTF(a, b)
#endif

#if __has_c_attribute(fallthrough) == 201910L
#define FALLTHROUGH [[fallthrough]]
#elif defined(__clang__)
#define FALLTHROUGH __attribute__((fallthrough))
#else
#define FALLTHROUGH
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

static uint16_t le_uint16(const uint8_t* p) {
  return (uint16_t)(p[1] << 8) | p[0];
}

static uint32_t le_uint32(const uint8_t* p) {
  return (uint32_t)(le_uint16(p + 2) << 16) | le_uint16(p);
}

struct Options {
  int current_indent;
  bool jpeg_scan;
};

static void increase_indent(struct Options* options) {
  options->current_indent += 2;
}

static void decrease_indent(struct Options* options) {
  options->current_indent -= 2;
}

static void print_indent(const struct Options* options) {
  for (int i = 0; i < options->current_indent; ++i)
    printf(" ");
}

PRINTF(2, 3)
static void iprintf(const struct Options* options, const char* msg, ...) {
  print_indent(options);
  va_list args;
  va_start(args, msg);
  vprintf(msg, args);
  va_end(args);
}

// TIFF dumping ///////////////////////////////////////////////////////////////

static void jpeg_dump(struct Options* options,
                      const uint8_t* begin,
                      const uint8_t* end);

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
  kDouble = 12,
  kLastEntry = kDouble,
};

static int TiffDataFormatSizes[] = {
    -1,
    1,  // kUnsignedByte
    1,  // kAscii
    2,  // kUnsignedShort
    4,  // kUnsignedLong
    8,  // kUnsignedRational
    1,  // kSignedByte
    1,  // kUndefined
    2,  // kSignedShort
    4,  // kSignedLong
    8,  // kSignedRational
    4,  // kFloat
    8,  // kDouble
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
static const char* tiff_tag_name(uint16_t tag) {
  // clang-format off
  switch (tag) {
    case 256: return "ImageWidth";
    case 257: return "ImageHeight";
    case 259: return "Compression";
    case 270: return "ImageDescription";
    case 271: return "Make";
    case 272: return "Model";
    case 274: return "Orientation";
    case 282: return "XResolution";
    case 283: return "YResolution";
    case 296: return "ResolutionUnit";
    case 305: return "Software";
    case 306: return "DateTime";
    case 315: return "Artist";
    case 513: return "JPEGInterchangeFormat";
    case 514: return "JPEGInterchangeFormatLength";
    case 531: return "YCbCrPositioning";
    case 33432: return "Copyright";

    // EXIF Private IFD tags
    // https://exiv2.org/tags.html
    case 33434: return "ExposureTime (seconds)";
    case 33437: return "FNumber";
    case 34850: return "ExposureProgram";
    case 34853: return "GPSInfo";
    case 34855: return "ISOSpeedRatings";
    case 34864: return "SensitivityType";
    case 34866: return "RecommendedExposureIndex";
    case 36864: return "ExifVersion";
    case 36867: return "DateTimeOriginal";
    case 36868: return "DateTimeDigitized";
    case 36880: return "OffsetTime";
    case 36881: return "OffsetTimeOriginal";
    case 36882: return "OffsetTimeDigitized";
    case 37121: return "ComponentsConfiguration";
    case 37122: return "CompressedBitsPerPixel";
    case 37377: return "ShutterSpeedValue";
    case 37378: return "ApertureValue";
    case 37379: return "BrightnessValue";
    case 37380: return "ExposureBiasValue";
    case 37381: return "MaxApertureValue";
    case 37382: return "SubjectDistance (meters)";
    case 37383: return "MeteringMode";
    case 37384: return "LightSource";
    case 37385: return "Flash";
    case 37386: return "FocalLength";
    case 37500: return "MakerNote";
    case 37520: return "SubsecTime";
    case 37521: return "SubsecTimeOriginal";
    case 37522: return "SubsecTimeDigitized";
    case 40960: return "FlashpixVersion";
    case 40961: return "ColorSpace";
    case 40962: return "PixelXDimension";
    case 40963: return "PixelYDimension";
    case 40965: return "InteroperabilityIFD";
    case 41486: return "FocalPlaneXResolution";
    case 41487: return "FocalPlaneYResolution";
    case 41488: return "FocalPlaneResolutionUnit";
    case 41493: return "ExposureIndex";
    case 41495: return "SensingMethod";
    case 41728: return "FileSource";
    case 41729: return "SceneType";
    case 41985: return "CustomRendered";
    case 41986: return "ExposureMode";
    case 41987: return "WhiteBalance";
    case 41988: return "DigitalZoomRatio";
    case 41989: return "FocalLengthIn35mmFilm";
    case 41990: return "SceneCaptureType";
    case 41991: return "GainControl";
    case 41992: return "Contrast";
    case 41993: return "Saturation";
    case 41994: return "Sharpness";
    case 41995: return "DeviceSettingDescription";
    case 41996: return "SubjectDistanceRange";
    case 42033: return "BodySerialNumber";
    case 42034: return "LensSpecification";
    case 42035: return "LensMake";
    case 42036: return "LensModel";
    case 42080: return "CompositeImage";

    // Multi-Picture Format (MPF) tags
    // https://web.archive.org/web/20190713230858/http://www.cipa.jp/std/documents/e/DC-007_E.pdf
    // Section 5.2.3, Table 3
    case 45056: return "MPFVersion";
    case 45057: return "NumberOfImages";
    case 45058: return "MPEntry";
    case 45059: return "ImageUIDList";
    case 45060: return "TotalFrames";

    // Private tags
    case 34665: return "Exif IFD";
    default: return NULL;
  }
  // clang-format on
}

// The GPS IFD has its own tag namespace.
static const char* tiff_gps_tag_name(uint16_t tag) {
  // clang-format off
  switch (tag) {
    case 1: return "GPSLatitudeRef";
    case 2: return "GPSLatitude";
    case 3: return "GPSLongitudeRef";
    case 4: return "GPSLongitude";
    case 5: return "GPSAltitudeRef";
    case 6: return "GPSAltitude";
    case 7: return "GPSTimeStamp";
    case 16: return "GPSImgDirectionRef";
    case 17: return "GPSImgDirection";
    case 27: return "GPSProcessingMethod";
    case 29: return "GPSDateStamp";
    default: return NULL;
  }
  // clang-format on
}

// The Interopability IFD has its own tag namespace.
static const char* tiff_interopability_tag_name(uint16_t tag) {
  // clang-format off
  switch (tag) {
    case 1: return "InteropabilityIndex";
    case 2: return "InteropabilityVersion";
    default: return NULL;
  }
  // clang-format on
}

struct TiffState {
  const uint8_t* begin;
  ssize_t size;
  uint16_t (*uint16)(const uint8_t*);
  uint32_t (*uint32)(const uint8_t*);
  const char* (*tag_name)(uint16_t);
  struct Options* options;
};

// Returns offset to next IFD, or 0 if none.
static uint32_t tiff_dump_one_ifd(const struct TiffState* tiff_state,
                                  uint32_t ifd_offset) {
  const uint8_t* begin = tiff_state->begin;
  ssize_t size = tiff_state->size;
  uint16_t (*uint16)(const uint8_t*) = tiff_state->uint16;
  uint32_t (*uint32)(const uint8_t*) = tiff_state->uint32;
  const char* (*name_for_tag)(uint16_t) = tiff_state->tag_name;
  struct Options* options = tiff_state->options;

  if (size - ifd_offset < 6) {
    printf("IFD needs at least 6 bytes, has %zu\n", size - ifd_offset);
    return 0;
  }
  uint16_t num_ifd_entries = uint16(begin + ifd_offset);
  if (size - ifd_offset - 6 < num_ifd_entries * 12) {
    printf("%d IFD entries need least %d bytes, have %zu\n", num_ifd_entries,
           num_ifd_entries * 12, size - ifd_offset - 2);
    return 0;
  }

  uint32_t jpeg_offset = 0;
  uint32_t jpeg_length = 0;
  uint32_t exif_ifd_offset = 0;
  uint32_t gps_info_ifd_offset = 0;
  uint32_t interopability_ifd_offset = 0;

  for (unsigned i = 0; i < num_ifd_entries; ++i) {
    size_t this_ifd_offset = ifd_offset + 2 + i * 12;
    uint16_t tag = uint16(begin + this_ifd_offset);
    uint16_t format = uint16(begin + this_ifd_offset + 2);
    uint32_t count = uint32(begin + this_ifd_offset + 4);

    if (format == 0 || format > kLastEntry) {
      iprintf(options, "ifd entry %u invalid format %i, ignoring\n", i, format);
      continue;
    }

    assert(TiffDataFormatSizes[format] >= 0);
    size_t total_size = count * (unsigned)TiffDataFormatSizes[format];
    size_t data_offset = total_size <= 4 ? this_ifd_offset + 8
                                         : uint32(begin + this_ifd_offset + 8);
    const void* data = begin + data_offset;
    iprintf(options, "tag %d", tag);
    const char* tag_name = name_for_tag(tag);
    if (tag_name)
      printf(" (%s)", tag_name);
    printf(" format %u (%s): count %u", format, TiffDataFormatNames[format],
           count);

    // FIXME: print other formats
    if (format == kUnsignedByte && count == 1)
      printf(": %u", *(const uint8_t*)data);
    else if (format == kAscii)
      printf(": '%.*s'", count, (const char*)data);
    else if (format == kUnsignedShort && count == 1)
      printf(": %u", uint16(data));
    else if (format == kUnsignedLong && count == 1)
      printf(": %u", uint32(data));
    else if (format == kUnsignedRational && count == 1) {
      uint32_t numerator = uint32(data);
      uint32_t denominator = uint32((const uint8_t*)data + 4);
      printf(": %u/%u", numerator, denominator);
      if (denominator != 0)
        printf(" (%.3f)", numerator / (double)denominator);
    } else if (format == kSignedRational && count == 1) {
      int32_t numerator = (int32_t)uint32(data);
      int32_t denominator = (int32_t)uint32((const uint8_t*)data + 4);
      printf(": %d/%d", numerator, denominator);
      if (denominator != 0)
        printf(" (%.3f)", numerator / (double)denominator);
    }

    if (tag == 513 && format == kUnsignedLong && count == 1)
      jpeg_offset = uint32(data);
    else if (tag == 514 && format == kUnsignedLong && count == 1)
      jpeg_length = uint32(data);
    else if (tag == 34665 && format == kUnsignedLong && count == 1)
      exif_ifd_offset = uint32(data);
    else if (tag == 34853 && format == kUnsignedLong && count == 1)
      gps_info_ifd_offset = uint32(data);
    else if (tag == 40965 && format == kUnsignedLong && count == 1)
      interopability_ifd_offset = uint32(data);

    printf("\n");
  }

  if (jpeg_offset != 0 && jpeg_length) {
    iprintf(options, "jpeg thumbnail\n");
    increase_indent(options);
    jpeg_dump(tiff_state->options, begin + jpeg_offset,
              begin + jpeg_offset + jpeg_length);
    decrease_indent(options);
  }
  if (exif_ifd_offset != 0) {
    iprintf(options, "exif IFD\n");
    increase_indent(options);
    uint32_t next = tiff_dump_one_ifd(tiff_state, exif_ifd_offset);
    if (next != 0)
      iprintf(options, "unexpected next IFD at %d, skipping\n", next);
    decrease_indent(options);
  }
  if (gps_info_ifd_offset != 0) {
    iprintf(options, "GPSInfo IFD:\n");
    increase_indent(options);
    struct TiffState gps_tiff_state = *tiff_state;
    gps_tiff_state.tag_name = tiff_gps_tag_name;
    uint32_t next = tiff_dump_one_ifd(&gps_tiff_state, gps_info_ifd_offset);
    if (next != 0)
      iprintf(options, "unexpected next IFD at %d, skipping\n", next);
    decrease_indent(options);
  }
  if (interopability_ifd_offset != 0) {
    iprintf(options, "Interopability IFD:\n");
    increase_indent(options);
    struct TiffState interopability_tiff_state = *tiff_state;
    interopability_tiff_state.tag_name = tiff_interopability_tag_name;
    uint32_t next = tiff_dump_one_ifd(&interopability_tiff_state,
                                      interopability_ifd_offset);
    if (next != 0)
      iprintf(options, "unexpected next IFD at %d, skipping\n", next);
    decrease_indent(options);
  }

  uint32_t next_ifd_offset =
      uint32(begin + ifd_offset + 2 + num_ifd_entries * 12);
  if (next_ifd_offset != 0)
    iprintf(options, "next IFD at %d\n", next_ifd_offset);

  return next_ifd_offset;
}

static void tiff_dump(struct Options* options,
                      const uint8_t* begin,
                      const uint8_t* end) {
  ssize_t size = end - begin;
  if (size < 8) {
    printf("tiff data should be at least 8 bytes, is %zu\n", size);
    return;
  }

  enum {
    kLittle,
    kBig,
  } endianness;
  if (strncmp((const char*)begin, "II", 2) == 0)
    endianness = kLittle;
  else if (strncmp((const char*)begin, "MM", 2) == 0)
    endianness = kBig;
  else {
    printf("unknown endianness id '%.2s'\n", begin);
    return;
  }

  uint16_t (*uint16)(const uint8_t*);
  uint32_t (*uint32)(const uint8_t*);
  if (endianness == kBig) {
    uint16 = be_uint16;
    uint32 = be_uint32;
  } else {
    uint16 = le_uint16;
    uint32 = le_uint32;
  }

  uint16_t check = uint16(begin + 2);
  if (check != 42) {
    printf("expected 0x2a, got 0x%x\n", check);
    return;
  }

  // IFD is short for 'Image File Directory'.
  uint32_t ifd_offset = uint32(begin + 4);
  if (ifd_offset != 8) {
    printf("IFD offset is surprisingly not 8 but %u\n", ifd_offset);
    printf("continuing anyway\n");
  }

  struct TiffState tiff_state = {
      .begin = begin,
      .size = size,
      .uint16 = uint16,
      .uint32 = uint32,
      .tag_name = tiff_tag_name,
      .options = options,
  };
  do {
    uint32_t next_ifd_offset = tiff_dump_one_ifd(&tiff_state, ifd_offset);
    ifd_offset = next_ifd_offset;
  } while (ifd_offset != 0);
}

// JPEG dumping ///////////////////////////////////////////////////////////////

// JPEG spec: https://www.w3.org/Graphics/JPEG/itu-t81.pdf
// JFIF spec: https://www.w3.org/Graphics/JPEG/jfif3.pdf
// EXIF spec: https://www.cipa.jp/std/documents/e/DC-008-2012_E.pdf
//            https://www.cipa.jp/std/documents/e/DC-X008-Translation-2019-E.pdf

static void jpeg_dump_sof0(struct Options* options,
                           const uint8_t* begin,
                           uint16_t size) {
  // https://www.w3.org/Graphics/JPEG/itu-t81.pdf section B.2.2 on page 25, or
  // https://mykb.cipindanci.com/archive/SuperKB/1294/JPEG%20File%20Layout%20and%20Format.htm
  if (size < 8) {
    printf("SOF0 should be at least 8 bytes, is %u\n", size);
    return;
  }

  begin += sizeof(uint16_t);

  iprintf(options, "bits per sample: %d\n", begin[0]);
  iprintf(options, "height: %d\n", be_uint16(begin + 1));
  iprintf(options, "width: %d\n", be_uint16(begin + 3));

  uint8_t num_components = begin[5];
  if (size - 8 < 3 * num_components) {
    printf("SOF0 with %d components should be at least %d bytes, is %u\n",
           num_components, 8 + 3 * num_components, size);
    return;
  }

  for (int i = 0; i < num_components; ++i) {
    iprintf(options, "components %d/%d\n", i + 1, num_components);
    iprintf(options, "  component id: %d\n", begin[6 + 3 * i]);

    uint8_t sampling_factors = begin[6 + 3 * i + 1];
    iprintf(options, "  sampling factors: %d horizontal, %d vertical\n",
            sampling_factors >> 4, sampling_factors & 0xf);
    iprintf(options, "  quantization table number: %d\n", begin[6 + 3 * i + 2]);
  }
}

static void jpeg_dump_jfif(struct Options* options,
                           const uint8_t* begin,
                           uint16_t size) {
  // https://en.wikipedia.org/wiki/JPEG_File_Interchange_Format#JFIF_APP0_marker_segment
  if (size < 16) {
    printf("jfif header should be at least 18 bytes, is %u\n", size);
    return;
  }

  begin += sizeof(uint16_t) + sizeof("JFIF");

  iprintf(options, "jfif version: %d.%d\n", begin[0], begin[1]);

  uint8_t units = begin[2];
  iprintf(options, "density unit: %d", units);
  switch (units) {
    case 0:
      printf(" (no units, aspect only)");
      break;
    case 1:
      printf(" (pixels/inch)");
      break;
    case 2:
      printf(" (pixels/cm)");
      break;
    default:
      printf(" (unknown value)");
      break;
  }
  printf("\n");

  iprintf(options, "Xdensity: %d\n", be_uint16(begin + 3));
  iprintf(options, "Ydensity: %d\n", be_uint16(begin + 5));
  iprintf(options, "Xthumbnail: %d\n", begin[7]);
  iprintf(options, "Ythumbnail: %d\n", begin[8]);
}

static void jpeg_dump_exif(struct Options* options,
                           const uint8_t* begin,
                           uint16_t size) {
  // https://www.cipa.jp/std/documents/e/DC-X008-Translation-2019-E.pdf
  tiff_dump(options, begin + sizeof(uint16_t) + sizeof("Exif") + 1,
            begin + size);
}

static const char* icc_profile_device_class_description(
    uint32_t profile_device_class) {
  switch (profile_device_class) {
    case 0x73636E72:  // 'scnr'
      return "Input device profile";
    case 0x6D6E7472:  // 'mntr'
      return "Display device profile";
    case 0x70727472:  // 'prtr'
      return "Output device profile";
    case 0x6C696E6B:  // 'link'
      return "DeviceLink profile";
    case 0x73706163:  // 'spac'
      return "ColorSpace profile";
    case 0x61627374:  // 'abst'
      return "Abstract profile";
    case 0x6E6D636C:  // 'nmcl'
      return "NamedColor profile";
    default:
      return NULL;
  }
}

static const char* icc_color_space_description(uint32_t data_color_space) {
  switch (data_color_space) {
    case 0x58595A20:  // 'XYZ '
      return "nCIEXYZ or PCSXYZ";
    case 0x4C616220:  // 'Lab '
      return "CIELAB or PCSLAB";
    case 0x4C757620:  // 'Luv '
      return "CIELUV";
    case 0x59436272:  // 'YCbr'
      return "YCbCr";
    case 0x59787920:  // 'Yxy '
      return "CIEYxy";
    case 0x52474220:  // 'RGB '
      return "RGB";
    case 0x47524159:  // 'GRAY'
      return "Gray";
    case 0x48535620:  // 'HSV '
      return "HSV";
    case 0x484C5320:  // 'HLS '
      return "HLS";
    case 0x434D594B:  // 'CMYK'
      return "CMYK";
    case 0x434D5920:  // 'CMY '
      return "CMY";
    case 0x32434C52:  // '2CLR'
      return "2 color";
    case 0x33434C52:  // '3CLR'
      return "3 color (other than XYZ, Lab, Luv, YCbCr, CIEYxy, RGB, HSV, HLS, "
             "CMY)";
    case 0x34434C52:  // '4CLR'
      return "4 color (other than CMYK)";
    case 0x35434C52:  // '5CLR'
      return "5 color";
    case 0x36434C52:  // '6CLR'
      return "6 color";
    case 0x37434C52:  // '7CLR'
      return "7 color";
    case 0x38434C52:  // '8CLR'
      return "8 color";
    case 0x39434C52:  // '9CLR'
      return "9 color";
    case 0x41434C52:  // 'ACLR'
      return "10 color";
    case 0x42434C52:  // 'BCLR'
      return "11 color";
    case 0x43434C52:  // 'CCLR'
      return "12 color";
    case 0x44434C52:  // 'DCLR'
      return "13 color";
    case 0x45434C52:  // 'ECLR'
      return "14 color";
    case 0x46434C52:  // 'FCLR'
      return "15 color";
    default:
      return NULL;
  }
}

static const char* icc_platform_description(uint32_t platform) {
  switch (platform) {
    case 0x4150504C:  // 'APPL'
      return "Apple Inc.";
    case 0x4D534654:  // 'MSFT'
      return "Microsoft Corporation";
    case 0x53474920:  // 'SGI '
      return "Silicon Graphics, Inc.";
    case 0x53554E57:  // 'SUNW'
      return "Sun Microsystems, Inc.";
    default:
      return NULL;
  }
}

static void icc_dump_textType(struct Options* options,
                              const uint8_t* begin,
                              uint32_t size) {
  // ICC.1:2022, 10.24 textType
  if (size < 9) {
    printf("textType must be at least 8 bytes, was %d\n", size);
    return;
  }

  uint32_t type_signature = be_uint32(begin);
  if (type_signature != 0x74657874) {  // 'text'
    printf("textType expected type 'text', got '%.4s'\n", begin);
    return;
  }

  uint32_t reserved = be_uint32(begin + 4);
  if (reserved != 0) {
    printf("textType expected reserved 0, got %d\n", reserved);
    return;
  }

  if (begin[size] != '\0') {
    printf("textType not 0-terminated\n");
    return;
  }

  iprintf(options, "%.*s\n", size - 9, begin + 8);
}

static void icc_dump_textDescriptionType(struct Options* options,
                                         const uint8_t* begin,
                                         uint32_t size) {
  // ICC.1:1998, 6.5.16 textDescriptionType

  // TODO: The 24 here includes the trailing 0 for the ascii text, but not
  // the trailling 0 for the unicode text (which isn't always there) and not
  // the 67 bytes for scriptcode (which _are_ always there).
  // This is 5 uint32_t, 2+1 bytes scriptcode trailer, and the 1 byte trailing
  // 0 in the ascii string.
  if (size < 24) {
    printf("multiLocalizedUnicodeType must be at least 24 bytes, was %d\n",
           size);
    return;
  }

  uint32_t type_signature = be_uint32(begin);
  if (type_signature != 0x64657363) {  // 'desc'
    printf("textDescriptionType expected type 'desc', got '%.4s'\n", begin);
    return;
  }

  uint32_t reserved = be_uint32(begin + 4);
  if (reserved != 0) {
    printf("textDescriptionType expected reserved 0, got %d\n", reserved);
    return;
  }

  uint32_t ascii_invariant_size = be_uint32(begin + 8);
  if (ascii_invariant_size == 0) {
    printf("textDescriptionType expected 0 byte ascci data\n");
    return;
  }
  if (size - 24 < ascii_invariant_size) {
    printf(
        "textDescriptionType with %u bytes of ascii must be at least %u bytes, "
        "was %d\n",
        ascii_invariant_size, 23 + ascii_invariant_size, size);
    return;
  }

  iprintf(options, "ascii: \"%.*s\"\n", ascii_invariant_size, begin + 12);

  // Note: Not aligned!
  uint32_t unicode_language_code = be_uint32(begin + 12 + ascii_invariant_size);
  uint32_t unicode_count = be_uint32(begin + 16 + ascii_invariant_size);
  if (unicode_language_code == 0 && unicode_count == 0)
    iprintf(options, "(no unicode data)\n");
  else {
    // TODO: UCS-2, \0-terminated, unicode_length inclusive of \0
    iprintf(options, "Unicode: TODO '%.4s' %u\n",
            begin + 12 + ascii_invariant_size, unicode_count);
  }

  uint32_t unicode_size = unicode_count * 2;  // UCS-2

  uint16_t scriptcode_code =
      be_uint16(begin + 20 + ascii_invariant_size + unicode_size);
  uint8_t scriptcode_count = begin[22 + ascii_invariant_size + unicode_size];
  if (scriptcode_code == 0 && scriptcode_count == 0) {
    iprintf(options, "(no scriptcode data)\n");
    // TODO: Could verify that the following 67 bytes are all zero.
  } else {
    iprintf(options, "Scriptcode: TODO %u %u\n", scriptcode_code,
            scriptcode_count);
  }

  if (size != 23 + ascii_invariant_size + unicode_size + 67)
    iprintf(options, "surprising size\n");
}

static void icc_dump_multiLocalizedUnicodeType(struct Options* options,
                                               const uint8_t* begin,
                                               uint32_t size) {
  // ICC.1:2022, 10.15 multiLocalizedUnicodeType
  if (size < 16) {
    printf("multiLocalizedUnicodeType must be at least 16 bytes, was %d\n",
           size);
    return;
  }

  uint32_t type_signature = be_uint32(begin);
  if (type_signature != 0x6D6C7563) {  // 'mluc'
    printf("multiLocalizedUnicodeType expected type 'mluc', got '%.4s'\n",
           begin);
    return;
  }

  uint32_t reserved = be_uint32(begin + 4);
  if (reserved != 0) {
    printf("multiLocalizedUnicodeType expected reserved 0, got %d\n", reserved);
    return;
  }

  uint32_t num_records = be_uint32(begin + 8);
  if (size - 16 < num_records * 12) {
    printf(
        "multiLocalizedUnicodeType with %u records must be at least %u bytes, "
        "was %d\n",
        num_records, 16 + num_records * 12, size);
    return;
  }

  uint32_t record_size = be_uint32(begin + 12);
  if (record_size != 12) {
    printf("multiLocalizedUnicodeType expected record_size 12, got %d\n",
           record_size);
    return;
  }

  for (unsigned i = 0; i < num_records; ++i) {
    uint32_t this_offset = 16 + i * 12;
    uint16_t iso_639_1_language_code = be_uint16(begin + this_offset);
    uint16_t iso_3166_1_country_code = be_uint16(begin + this_offset + 2);
    uint32_t string_length = be_uint32(begin + this_offset + 4);
    uint32_t string_offset = be_uint32(begin + this_offset + 8);

    iprintf(options, "%c%c/%c%c: \"", iso_639_1_language_code >> 8,
            iso_639_1_language_code & 0xff, iso_3166_1_country_code >> 8,
            iso_3166_1_country_code & 0xff);

    if (string_length % 2 != 0) {
      printf("data length not multiple of 2, skipping\n");
      continue;
    }

    // UTF-16BE text :/
    // And no uchar.h / c16rtomb() on macOS either (as of macOS 12.5) :/
    // FIXME: Do actual UTF16-to-UTF8 conversion.
    const uint8_t* utf16_be = begin + string_offset;
    for (unsigned j = 0; j < string_length / 2; ++j, utf16_be += 2) {
      uint16_t cur = be_uint16(utf16_be);
      printf("%c", cur & 0x7f);
    }
    printf("\"\n");
  }
}

static void icc_dump_XYZType(struct Options* options,
                             const uint8_t* begin,
                             uint32_t size) {
  // ICC.1:2022, 10.31 XYZType
  if (size < 8) {
    printf("XYZType must be at least 8 bytes, was %d\n", size);
    return;
  }

  uint32_t type_signature = be_uint32(begin);
  if (type_signature != 0x58595A20) {  // 'XYZ '
    printf("XYZType expected type 'XYZ ', got '%.4s'\n", begin);
    return;
  }

  uint32_t reserved = be_uint32(begin + 4);
  if (reserved != 0) {
    printf("XYZType expected reserved 0, got %d\n", reserved);
    return;
  }

  uint32_t xyz_size = size - 8;
  if (xyz_size % 12 != 0) {
    printf("XYZType expected %d to be multiple of 12\n", xyz_size);
    return;
  }

  uint32_t xyz_count = xyz_size / 12;
  for (unsigned i = 0; i < xyz_count; ++i) {
    uint32_t this_offset = 8 + i * 12;

    int32_t xyz_x = (int32_t)be_uint32(begin + this_offset);
    int32_t xyz_y = (int32_t)be_uint32(begin + this_offset + 4);
    int32_t xyz_z = (int32_t)be_uint32(begin + this_offset + 8);
    iprintf(options, "X = %.4f, Y = %.4f, Z = %.4f\n", xyz_x / (double)0x10000,
            xyz_y / (double)0x10000, xyz_z / (double)0x10000);
  }
}

static void icc_dump_curveType(struct Options* options,
                               const uint8_t* begin,
                               uint32_t size) {
  // ICC.1:2022, 10.6 curveType
  if (size < 12) {
    printf("curveType must be at least 12 bytes, was %d\n", size);
    return;
  }

  uint32_t type_signature = be_uint32(begin);
  if (type_signature != 0x63757276) {  // 'curv'
    printf("curveType expected type 'curv', got '%.4s'\n", begin);
    return;
  }

  uint32_t reserved = be_uint32(begin + 4);
  if (reserved != 0) {
    printf("curveType expected reserved 0, got %d\n", reserved);
    return;
  }

  uint32_t num_entries = be_uint32(begin + 8);
  if (size - 12 != num_entries * 2) {
    printf("curveType with %d entries must be %u bytes, was %d\n", num_entries,
           12 + num_entries * 2, size);
    return;
  }

  iprintf(options, "%d entries:", num_entries);
  if (num_entries == 0) {
    printf(" identity response");
  } else if (num_entries == 1) {
    uint16_t value = be_uint16(begin + 12);
    printf(" gamma %f", value / (double)0x100);
  } else {
    for (unsigned i = 0; i < num_entries; ++i) {
      uint32_t this_offset = 12 + i * 2;
      uint16_t value = be_uint16(begin + this_offset);
      printf(" %u", value);
    }
  }
  printf("\n");
}

static void icc_dump_parametricCurveType(struct Options* options,
                                         const uint8_t* begin,
                                         uint32_t size) {
  // ICC.1:2022, 10.18 parametricCurveType
  if (size < 12) {
    printf("parametricCurveType must be at least 12 bytes, was %d\n", size);
    return;
  }

  uint32_t type_signature = be_uint32(begin);
  if (type_signature != 0x70617261) {  // 'para'
    printf("parametricCurveType expected type 'para', got '%.4s'\n", begin);
    return;
  }

  uint32_t reserved = be_uint32(begin + 4);
  if (reserved != 0) {
    printf("parametricCurveType expected reserved 0, got %d\n", reserved);
    return;
  }

  uint16_t function_type = be_uint16(begin + 8);
  uint16_t reserved2 = be_uint16(begin + 10);
  if (reserved != 0) {
    printf("parametricCurveType expected reserved2 0, got %d\n", reserved2);
    return;
  }

  unsigned n;
  switch (function_type) {
    case 0:
      iprintf(options, "Y = X**g\n");
      n = 1;
      break;
    case 1:
      iprintf(options, "Y = (a*X + b)**g     if X >= -b/a\n");
      iprintf(options, "  = 0                if X <  -b/a\n");
      n = 3;
      break;
    case 2:
      iprintf(options, "Y = (a*X + b)**g + c if X >= -b/a\n");
      iprintf(options, "  = c                if X <  -b/a\n");
      n = 4;
      break;
    case 3:
      iprintf(options, "Y = (a*X + b)**g     if X >= d\n");
      iprintf(options, "  =  c*X             if X <  d\n");
      n = 5;
      break;
    case 4:
      iprintf(options, "Y = (a*X + b)**g + e if X >= d\n");
      iprintf(options, "  =  c*X + f         if X <  d\n");
      n = 7;
      break;
    default:
      iprintf(options, "unknown function type %d\n", function_type);
      return;
  }

  if (size - 12 != n * 4) {
    printf("parametricCurveType with %d params must be %u bytes, was %d\n", n,
           12 + n * 2, size);
  }

  const char names[] = {'g', 'a', 'b', 'c', 'd', 'e', 'f'};
  iprintf(options, "with ");
  for (unsigned i = 0; i < n; ++i) {
    int32_t v = (int32_t)be_uint32(begin + 12 + i*4);
    printf("%c = %f", names[i], v / (double)0x10000);
    if (i != n - 1)
      printf(", ");
  }
  printf("\n");
}

static void jpeg_dump_icc(struct Options* options,
                          const uint8_t* begin,
                          uint16_t size) {
  // https://www.color.org/technotes/ICC-Technote-ProfileEmbedding.pdf
  //   (In particular, an ICC profile can be split across several APP2 marker
  //   chunks: "...a mechanism is required to break the profile into chunks...")
  // https://www.color.org/specification/ICC.1-2022-05.pdf
  const size_t prefix_size = sizeof(uint16_t) + sizeof("ICC_PROFILE");
  const size_t header_size = prefix_size + 1 + 1;
  if (size < header_size) {
    printf("ICC should be at least %zu bytes, is %u\n", header_size, size);
    return;
  }

  uint8_t chunk_sequence_number = begin[prefix_size];  // 1-based (!)
  uint8_t num_chunks = begin[prefix_size + 1];
  iprintf(options, "chunk %u/%u\n", chunk_sequence_number, num_chunks);

  if (chunk_sequence_number != 1 || num_chunks != 1) {
    printf("cannot handle ICC profiles spread over several chunks yet\n");
    return;
  }

  // 7.2 Profile header
  const uint8_t* icc_header = begin + header_size;
  uint32_t profile_size = be_uint32(icc_header);
  iprintf(options, "Profile size: %u\n", profile_size);

  uint32_t preferred_cmm_type = be_uint32(icc_header + 4);
  if (preferred_cmm_type == 0)
    iprintf(options, "Preferred CMM type: (not set)\n");
  else {
    iprintf(options, "Preferred CMM type: '%.4s'\n", icc_header + 4);
  }

  uint8_t profile_version_major = icc_header[8];
  uint8_t profile_version_minor = icc_header[9] >> 4;
  uint8_t profile_version_bugfix = icc_header[9] & 0xf;
  uint16_t profile_version_zero = be_uint16(icc_header + 8 + 2);
  iprintf(options, "Profile version: %d.%d.%d.%d\n", profile_version_major,
          profile_version_minor, profile_version_bugfix, profile_version_zero);

  uint32_t profile_device_class = be_uint32(icc_header + 12);
  iprintf(options, "Profile/Device class: '%.4s'", icc_header + 12);
  const char* profile_device_class_description =
      icc_profile_device_class_description(profile_device_class);
  if (profile_device_class_description)
    printf(" (%s)", profile_device_class_description);
  printf("\n");

  uint32_t data_color_space = be_uint32(icc_header + 16);
  iprintf(options, "Data color space: '%.4s'", icc_header + 16);
  const char* color_space_description =
      icc_color_space_description(data_color_space);
  if (color_space_description)
    printf(" (%s)", color_space_description);
  printf("\n");

  uint32_t pcs = be_uint32(icc_header + 20);
  iprintf(options, "Profile connection space (PCS): '%.4s'", icc_header + 20);
  const char* pcc_description = icc_color_space_description(pcs);
  if (pcc_description)
    printf(" (%s)", pcc_description);
  printf("\n");

  uint16_t year = be_uint16(icc_header + 24);
  uint16_t month = be_uint16(icc_header + 26);
  uint16_t day = be_uint16(icc_header + 28);
  uint16_t hour = be_uint16(icc_header + 30);
  uint16_t minutes = be_uint16(icc_header + 32);
  uint16_t seconds = be_uint16(icc_header + 34);
  iprintf(options, "Profile created: %d-%02d-%02dT%02d:%02d:%02dZ\n", year,
          month, day, hour, minutes, seconds);

  uint32_t profile_file_signature = be_uint32(icc_header + 36);
  iprintf(options, "Profile file signature: '%.4s'", icc_header + 36);
  if (profile_file_signature != 0x61637370)
    printf(" (expected 'acsp', but got something else?");
  printf("\n");

  uint32_t primary_platform = be_uint32(icc_header + 40);
  iprintf(options, "Primary platform: ");
  if (primary_platform == 0)
    printf("none");
  else {
    printf("'%.4s'", icc_header + 40);
    const char* primary_platform_description =
        icc_platform_description(primary_platform);
    if (primary_platform_description)
      printf(" (%s)", primary_platform_description);
  }
  printf("\n");

  uint32_t profile_flags = be_uint32(icc_header + 44);
  iprintf(options, "Profile flags: 0x%08x\n", profile_flags);

  uint32_t device_manufacturer = be_uint32(icc_header + 48);
  iprintf(options, "Device manufacturer: ");
  if (device_manufacturer == 0)
    printf("none");
  else
    printf("'%.4s'", icc_header + 48);
  printf("\n");

  uint32_t device_model = be_uint32(icc_header + 52);
  iprintf(options, "Device model: ");
  if (device_model == 0)
    printf("none");
  else
    printf("'%.4s'", icc_header + 52);
  printf("\n");

  uint64_t device_attributes = be_uint64(icc_header + 56);
  iprintf(options, "Device attributes: 0x%016" PRIx64 "\n", device_attributes);

  uint32_t rendering_intent = be_uint32(icc_header + 64);
  iprintf(options, "Rendering intent: %d", rendering_intent);
  switch (rendering_intent & 0xffff) {
    case 0:
      printf(" (Perceptual)");
      break;
    case 1:
      printf(" (Media-relative colorimetric)");
      break;
    case 2:
      printf(" (Saturation)");
      break;
    case 3:
      printf(" (ICC-absolute colorimetric)");
      break;
  }
  if (rendering_intent >> 16 != 0)
    printf(" (top 16-bit unexpectedly not zero)");
  printf("\n");

  int32_t xyz_x = (int32_t)be_uint32(icc_header + 68);
  int32_t xyz_y = (int32_t)be_uint32(icc_header + 72);
  int32_t xyz_z = (int32_t)be_uint32(icc_header + 76);
  char xyz_buf[1024];
  snprintf(xyz_buf, sizeof(xyz_buf), "X = %.4f, Y = %.4f, Z = %.4f",
           xyz_x / (double)0x10000, xyz_y / (double)0x10000,
           xyz_z / (double)0x10000);
  iprintf(options, "PCS illuminant: %s", xyz_buf);
  const char expected_xyz[] = "X = 0.9642, Y = 1.0000, Z = 0.8249";
  if (strcmp(xyz_buf, expected_xyz) != 0)
    printf("(unexpected; expected %s)", expected_xyz);
  printf("\n");

  uint32_t profile_creator = be_uint32(icc_header + 80);
  iprintf(options, "Profile creator: ");
  if (profile_creator == 0)
    printf("none");
  else
    printf("'%.4s'", icc_header + 80);
  printf("\n");

  // This is the MD5 of the entire profile, with profile flags, rendering
  // intent, and profile ID temporarily set to 0.
  uint64_t profile_id_part_1 = be_uint64(icc_header + 84);
  uint64_t profile_id_part_2 = be_uint64(icc_header + 92);
  iprintf(options, "Profile ID: ");
  if (profile_id_part_1 == 0 && profile_id_part_2 == 0)
    printf("not computed");
  else
    printf("0x%016" PRIx64 "_%016" PRIx64, profile_id_part_1,
           profile_id_part_2);
  printf("\n");

  bool reserved_fields_are_zero = true;
  for (int i = 100; i < 128; ++i)
    if (icc_header[i] != 0)
      reserved_fields_are_zero = false;
  if (reserved_fields_are_zero)
    iprintf(options, "reserved header bytes are zero\n");
  else
    iprintf(options, "reserved header bytes are unexpectedly not zero\n");

  // 7.3 Tag table
  const uint8_t* tag_table = icc_header + 128;

  uint32_t tag_count = be_uint32(tag_table);
  iprintf(options, "%d tags\n", tag_count);

  increase_indent(options);
  for (unsigned i = 0; i < tag_count; ++i) {
    uint32_t this_offset = 4 + 12 * i;
    uint32_t tag_signature = be_uint32(tag_table + this_offset);
    uint32_t offset_to_data = be_uint32(tag_table + this_offset + 4);
    uint32_t size_of_data = be_uint32(tag_table + this_offset + 8);
    iprintf(options, "signature 0x%08x ('%.4s') offset %d size %d\n",
            tag_signature, tag_table + this_offset, offset_to_data,
            size_of_data);

    increase_indent(options);

    uint32_t type_signature = 0;
    if (size_of_data >= 4) {
      type_signature = be_uint32(icc_header + offset_to_data);
      iprintf(options, "type '%.4s'\n", icc_header + offset_to_data);
    }

    switch (tag_signature) {
      case 0x63707274:  // 'cprt', copyrightTag
        // Per ICC.1:2022 9.2.22, the type of copyrightTag must be
        // multiLocalizedUnicodeType. But Sony RAW files exported by Lightroom
        // give it type textType. (This was valid older ICC versions, e.g. in
        // ICC.1:1998.)
        if (type_signature == 0x74657874) {  // 'text'
          icc_dump_textType(options, icc_header + offset_to_data, size_of_data);
          break;
        }
        FALLTHROUGH;
      case 0x64657363:  // 'desc', profileDescriptionTag
      case 0x646D6E64:  // 'dmnd', deviceMfgDescTag
      case 0x646D6464:  // 'dmdd', deviceModelDescTag
      case 0x76756564:  // 'vued', viewingCondDescTag
        // For non-'cprt', older ICC versions used type textDescriptionType
        // ('desc') here.
        if (tag_signature != 0x63707274 && type_signature == 0x64657363) {
          icc_dump_textDescriptionType(options, icc_header + offset_to_data,
                                       size_of_data);
          break;
        }
        icc_dump_multiLocalizedUnicodeType(options, icc_header + offset_to_data,
                                           size_of_data);
        break;
      case 0x6258595A:  // 'bXYZ', blueMatrixColumnTag
      case 0x626B7074:  // 'btpt', mediaBlackPointTag (v2 only, not in v4)
      case 0x6758595A:  // 'gXYZ', greenMatrixColumnTag
      case 0x6c756d69:  // 'lumi', luminanceTag
      case 0x7258595A:  // 'rXYZ', redMatrixColumnTag
      case 0x77747074:  // 'wtpt', mediaWhitePointTag
        icc_dump_XYZType(options, icc_header + offset_to_data, size_of_data);
        break;
      case 0x62545243:  // 'bTRC', blueTRCTag (TRC: tone reproduction curve)
      case 0x67545243:  // 'gTRC', greenTRCTag
      case 0x72545243:  // 'rTRC', redTRCTag
        if (type_signature == 0x63757276) {  // 'curv'
          icc_dump_curveType(options, icc_header + offset_to_data,
                             size_of_data);
        } else if (type_signature == 0x70617261) {  // 'para'
          icc_dump_parametricCurveType(options, icc_header + offset_to_data,
                                       size_of_data);
        } else {
          iprintf(options, "unexpected type, expected 'curv' or 'para'\n");
        }
        break;
    }
    decrease_indent(options);
  }
  decrease_indent(options);
}

static void jpeg_dump_mpf(struct Options* options,
                          const uint8_t* begin,
                          uint16_t size) {
  // https://web.archive.org/web/20190713230858/http://www.cipa.jp/std/documents/e/DC-007_E.pdf
  tiff_dump(options, begin + sizeof(uint16_t) + sizeof("MPF"), begin + size);
}

static void indent_and_elide_each_line(struct Options* options,
                                       const uint8_t* s,
                                       int n) {
  const int max_column = 80;
  print_indent(options);
  int column = options->current_indent;
  for (int i = 0; i < n; ++i) {
    putchar(s[i]);
    ++column;

    if (s[i] == '\n') {
      if (i + 1 < n)
        print_indent(options);
      column = options->current_indent;
    }

    if (column > max_column) {
      printf("...");
      const uint8_t* next_newline =
          (const uint8_t*)strchr((const char*)(s + i), '\n');
      if (next_newline == NULL)
        return;
      i += next_newline - (s + i) - 1;
    }
  }
}

static void print_elided(struct Options* options,
                         int max_width,
                         const uint8_t* s,
                         int n) {
  if (n < max_width) {
    indent_and_elide_each_line(options, s, n);
  } else {
    indent_and_elide_each_line(options, s, max_width / 2 - 1);
    if (s[max_width / 2 - 2] != '\n')
      printf("\n");
    print_indent(options);
    printf("...\n");
    indent_and_elide_each_line(options, s + n - (max_width / 2 - 2),
                               max_width / 2 - 2);
  }
  if (s[n - 1] != '\n')
    printf("\n");
}

static void jpeg_dump_xmp(struct Options* options,
                          const uint8_t* begin,
                          uint16_t size) {
  // http://www.npes.org/pdf/xmpspecification-Jun05.pdf
  const size_t header_size =
      sizeof(uint16_t) + sizeof("http://ns.adobe.com/xap/1.0/");
  if (size < header_size) {
    printf("xmp header should be at least %zu bytes, is %u\n", header_size,
           size);
    return;
  }
  print_elided(options, 1024, begin + header_size, size - header_size);
}

static void jpeg_dump_xmp_extension(struct Options* options,
                                    const uint8_t* begin,
                                    uint16_t size) {
  // See section 1.1.3.1 in:
  // https://github.com/adobe/xmp-docs/blob/master/XMPSpecifications/XMPSpecificationPart3.pdf
  const size_t guid_size = 32;
  const size_t prefix_size =
      sizeof(uint16_t) + sizeof("http://ns.adobe.com/xmp/extension/");
  const size_t header_size = prefix_size + guid_size + 2 * sizeof(uint32_t);
  if (size < header_size) {
    printf("xmp header should be at least %zu bytes, is %u\n", header_size,
           size);
    return;
  }
  iprintf(options, "guid %.32s\n", begin + prefix_size);
  uint32_t total_data_size = be_uint32(begin + prefix_size + guid_size);
  uint32_t data_offset =
      be_uint32(begin + prefix_size + guid_size + sizeof(uint32_t));
  uint16_t data_size = size - header_size;
  iprintf(options, "offset %u\n", data_offset);
  iprintf(options, "total size %u\n", total_data_size);
  print_elided(options, 1024, begin + header_size, data_size);
}

static const char* jpeg_dump_app_id(struct Options* options,
                                    const uint8_t* begin,
                                    const uint8_t* end,
                                    bool has_size,
                                    uint16_t size) {
  if (!has_size) {
    iprintf(options, "  no size?!\n");
    return NULL;
  }

  if (size < 2) {
    iprintf(options, "invalid size, must be at least 2, but was %u\n", size);
    return NULL;
  }

  if (end - begin < size) {
    iprintf(options, "size is %u, but only %zu bytes left\n", size,
            end - begin);
    return NULL;
  }

  const char* app_id = (const char*)(begin + 2);
  size_t id_len = strnlen(app_id, size - 2);
  if (begin[2 + id_len] != '\0') {
    iprintf(options, "no zero-terminated id found\n");
    return NULL;
  }

  iprintf(options, "app id: '%s'\n", app_id);
  return app_id;
}

static void jpeg_dump(struct Options* options,
                      const uint8_t* begin,
                      const uint8_t* end) {
  const uint8_t* cur = begin;
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

    iprintf(options, "%02x%02x at offset %ld", b0, b1, cur - begin - 2);

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
        assert(has_size);
        printf(": Start Of Frame, baseline DCT (SOF0)\n");
        increase_indent(options);
        jpeg_dump_sof0(options, cur, size);
        decrease_indent(options);
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
        // If there's an App2/"MPF" ("Multi-Picture Format") marker,
        // it'll point to additional images past the first image's EOI.
        // Some versions of the Pixel Pro camera app write unknown-to-me
        // non-jpeg trailing data after the EOI marker.
        // FIXME: In non-scan mode, this should terminate the reading loop.
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
          iprintf(options, "  missing size\n");
        else if (size != 4)
          iprintf(options, "  expected size 4, got %d\n", size);
        else
          iprintf(options, "  %d macroblocks\n", be_uint16(cur + 2));
        break;
      case 0xe0: {
        printf(": JPEG/JFIF Image segment (APP0)\n");
        increase_indent(options);
        const char* app_id =
            jpeg_dump_app_id(options, cur, end, has_size, size);
        if (strcmp(app_id, "JFIF") == 0)
          jpeg_dump_jfif(options, cur, size);
        decrease_indent(options);
        break;
      }
      case 0xe1: {
        printf(": EXIF Image segment (APP1)\n");

        increase_indent(options);
        const char* app_id =
            jpeg_dump_app_id(options, cur, end, has_size, size);
        if (strcmp(app_id, "Exif") == 0)
          jpeg_dump_exif(options, cur, size);
        else if (strcmp(app_id, "http://ns.adobe.com/xap/1.0/") == 0)
          jpeg_dump_xmp(options, cur, size);
        else if (strcmp(app_id, "http://ns.adobe.com/xmp/extension/") == 0)
          jpeg_dump_xmp_extension(options, cur, size);
        decrease_indent(options);
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

        increase_indent(options);
        const char* app_id =
            jpeg_dump_app_id(options, cur, end, has_size, size);
        if (b1 == 0xe2 && strcmp(app_id, "ICC_PROFILE") == 0)
          jpeg_dump_icc(options, cur, size);
        else if (b1 == 0xe2 && strcmp(app_id, "MPF") == 0)
          jpeg_dump_mpf(options, cur, size);
        decrease_indent(options);
        break;
      }
      case 0xfe:
        printf(": Comment (COM)\n");
        assert(has_size);
        if (size > 2) {
          increase_indent(options);
          print_elided(options, 1024, cur + 2, size - 2);
          decrease_indent(options);
        }
        break;
      default:
        printf("\n");
    }

    if (!options->jpeg_scan && has_size) {
      cur += size;
      if (cur >= end)
        printf("marker length went %zu bytes past data end\n", cur - end + 1);
    }
  }
}

int main(int argc, char* argv[]) {
  const char* program_name = argv[0];

  // Parse options.
  struct Options options = {
      .current_indent = 0,
      .jpeg_scan = false,
  };
  struct option getopt_options[] = {
      {"help", no_argument, NULL, 'h'},
      {"scan", no_argument, NULL, 's'},
      {0, 0, 0, 0},
  };
  int opt;
  while ((opt = getopt_long(argc, argv, "hs", getopt_options, NULL)) != -1) {
    switch (opt) {
      case 'h':
        print_usage(stdout, program_name);
        return 0;
      case 's':
        options.jpeg_scan = true;
        break;
      case '?':
        print_usage(stderr, program_name);
        return 1;
    }
  }
  argv += optind;
  argc -= optind;

  if (argc != 1) {
    fprintf(stderr, "expected 1 arg, got %d\n", argc);
    print_usage(stderr, program_name);
    return 1;
  }

  const char* in_name = argv[0];

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

  jpeg_dump(&options, contents, (uint8_t*)contents + in_stat.st_size);

  munmap(contents, (size_t)in_stat.st_size);
  close(in_file);
}
