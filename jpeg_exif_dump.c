#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <math.h>
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

/*
clang jpeg_exif_dump.c -isysroot $(xcrun -show-sdk-path) -fuse-ld=lld \
   -Weverything -Wno-declaration-after-statement -Wno-padded \
   -Wno-unsafe-buffer-usage
*/

// Dumps metadata in jpeg files.
// Fairly complete:
// * Exif
// * XMP (could parse XML and combine xmp extension chunks into a single
//       doc, also could interpret some of the base64 data in here for Pixel
//       jpegs)
// * ICC (RGB only for now)
// * IPTC
// Dumps some bits:
// * Photoshop resource block (APP13/"Photoshop 3.0")
// Still missing:
// * MPF
// * Makernote

static void print_usage(FILE* stream, const char* program_name) {
  fprintf(stream, "usage: %s [ options ] filename\n", program_name);
  fprintf(stream,
          "\n"
          "options:\n"
          "  -d  --dump   write each embedded jpeg to jpeg-$n.jpg\n"
          "  --dump-luts  dump LUTs even on non-truecolor terminals\n"
          "  -h  --help   print this message\n"
          "  -s  --scan   scan for jpeg markers in entire file\n"
          "               (by default, skips marker data)\n");
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

struct JpegIccChunks;

struct Options {
  int current_indent;
  bool jpeg_scan;

  bool dump_luts;
  bool dump_jpegs;
  int num_jpegs;

  struct JpegIccChunks* jpeg_icc_chunks;
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

// TIFF / Exif dumping ////////////////////////////////////////////////////////

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
    case 258: return "BitsPerSample";
    case 259: return "Compression";
    case 262: return "PhotometricInterpretation";
    case 270: return "ImageDescription";
    case 271: return "Make";
    case 272: return "Model";
    case 274: return "Orientation";
    case 277: return "SamplesPerPixel";
    case 282: return "XResolution";
    case 283: return "YResolution";
    case 296: return "ResolutionUnit";
    case 305: return "Software";
    case 306: return "DateTime";
    case 315: return "Artist";
    case 316: return "HostComputer";
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
    case 37396: return "SubjectArea";
    case 37500: return "MakerNote";
    case 37510: return "UserComment";
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
    case 12: return "GPSSpeedRef";
    case 13: return "GPSSpeed";
    case 16: return "GPSImgDirectionRef";
    case 17: return "GPSImgDirection";
    case 23: return "GPSDestBearingRef";
    case 24: return "GPSDestBearing";
    case 27: return "GPSProcessingMethod";
    case 29: return "GPSDateStamp";
    case 31: return "GPSHPositioningError";  // Horizontal error in meters.
    default: return NULL;
  }
  // clang-format on
}

// The Interoperability IFD has its own tag namespace.
static const char* tiff_interoperability_tag_name(uint16_t tag) {
  // clang-format off
  switch (tag) {
    case 1: return "InteroperabilityIndex";
    case 2: return "InteroperabilityVersion";
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
  void (*dump_extra_tag_info)(const struct TiffState*,
                              uint16_t,
                              uint16_t,
                              uint32_t,
                              const void*);
  struct Options* options;
};

static bool tiff_has_format_and_count(uint16_t format,
                                      uint16_t expected_format,
                                      uint32_t count,
                                      uint32_t expected_count) {
  if (format != expected_format) {
    printf(" (invalid format %d, wanted %d)", format, expected_format);
    return false;
  }
  if (count != expected_count) {
    printf(" (invalid count %d, wanted %d)", count, expected_count);
    return false;
  }
  return true;
}

static void tiff_dump_image_orientation(const struct TiffState* tiff_state,
                                        uint16_t format,
                                        uint32_t count,
                                        const void* data) {
  if (!tiff_has_format_and_count(format, kUnsignedShort, count, 1))
    return;

  uint16_t orientation = tiff_state->uint16(data);
  // clang-format off
  switch (orientation) {
    case 1: printf(" (top left)"); break;
    case 2: printf(" (top right)"); break;
    case 3: printf(" (bottom right)"); break;
    case 4: printf(" (bottom left)"); break;
    case 5: printf(" (left top)"); break;
    case 6: printf(" (right top)"); break;
    case 7: printf(" (right bottom)"); break;
    case 8: printf(" (left bottom)"); break;
    default: printf(" (unknown value)");
  }
  // clang-format on
}

static void tiff_dump_resolution_unit(const struct TiffState* tiff_state,
                                      uint16_t format,
                                      uint32_t count,
                                      const void* data) {
  if (!tiff_has_format_and_count(format, kUnsignedShort, count, 1))
    return;

  uint16_t resolution_unit = tiff_state->uint16(data);
  // clang-format off
  switch (resolution_unit) {
    case 1: printf(" (none)"); break;
    case 2: printf(" (inch)"); break;
    case 3: printf(" (centimeter)"); break;
    default: printf(" (unknown value)");
  }
  // clang-format on
}

static void tiff_dump_ycbcr_positioning(const struct TiffState* tiff_state,
                                        uint16_t format,
                                        uint32_t count,
                                        const void* data) {
  if (!tiff_has_format_and_count(format, kUnsignedShort, count, 1))
    return;

  uint16_t resolution_unit = tiff_state->uint16(data);
  // clang-format off
  switch (resolution_unit) {
    case 1: printf(" (centered)"); break;
    case 2: printf(" (cosited)"); break;
    default: printf(" (unknown value)");
  }
  // clang-format on
}

static void tiff_dump_exposure_program(const struct TiffState* tiff_state,
                                       uint16_t format,
                                       uint32_t count,
                                       const void* data) {
  if (!tiff_has_format_and_count(format, kUnsignedShort, count, 1))
    return;

  uint16_t exposure_program = tiff_state->uint16(data);
  switch (exposure_program) {
    case 0:
      printf(" (Not defined)");
      break;
    case 1:
      printf(" (Manual)");
      break;
    case 2:
      printf(" (Normal program)");
      break;
    case 3:
      printf(" (Aperture priority)");
      break;
    case 4:
      printf(" (Shutter priority)");
      break;
    case 5:
      printf(" (Creative program (biased toward depth of field))");
      break;
    case 6:
      printf(" (Action program (biased toward fast shutter speed))");
      break;
    case 7:
      printf(
          " (Portrait mode (for closeup photos with the background out of "
          "focus))");
      break;
    case 8:
      printf(
          " (Landscape mode (for landscape photos with the background in "
          "focus))");
      break;
    default:
      printf(" (unknown value)");
  }
}

static void tiff_dump_sensitivity_type(const struct TiffState* tiff_state,
                                       uint16_t format,
                                       uint32_t count,
                                       const void* data) {
  if (!tiff_has_format_and_count(format, kUnsignedShort, count, 1))
    return;

  uint16_t sensitivity_type = tiff_state->uint16(data);
  switch (sensitivity_type) {
    case 0:
      printf(" (Unknown)");
      break;
    case 1:
      printf(" (Standard output sensitivity (SOS))");
      break;
    case 2:
      printf(" (Recommended exposure index (REI))");
      break;
    case 3:
      printf(" (ISO speed)");
      break;
    case 4:
      printf(
          " (Standard output sensitivity (SOS) and recommended exposure index "
          "(REI))");
      break;
    case 5:
      printf(" (Standard output sensitivity (SOS) and ISO speed)");
      break;
    case 6:
      printf(" (Recommended exposure index (REI) and ISO speed)");
      break;
    case 7:
      printf(
          " (Standard output sensitivity (SOS) and recommended exposure index "
          "(REI) and ISO speed)");
      break;
    default:
      printf(" (unknown value)");
  }
}

static void tiff_dump_exif_version(uint16_t format,
                                   uint32_t count,
                                   const void* data) {
  if (!tiff_has_format_and_count(format, kUndefined, count, 4))
    return;

  const char* version = (const char*)data;
  printf(" (%c%c.%c%c)", version[0], version[1], version[2], version[3]);
}

static void tiff_dump_components_configuration(uint16_t format,
                                               uint32_t count,
                                               const void* data) {
  if (!tiff_has_format_and_count(format, kUndefined, count, 4))
    return;

  printf(" (");
  const uint8_t* components = (const uint8_t*)data;
  for (int i = 0; i < 4; ++i) {
    // clang-format off
    switch(components[i]) {
      case 0: printf("."); break;
      case 1: printf("Y"); break;
      case 2: printf("Cb"); break;
      case 3: printf("Cr"); break;
      case 4: printf("R"); break;
      case 5: printf("G"); break;
      case 6: printf("B"); break;
      default: printf("?");
    }
    // clang-format on
  }
  printf(")");
}

static void tiff_dump_metering_mode(const struct TiffState* tiff_state,
                                    uint16_t format,
                                    uint32_t count,
                                    const void* data) {
  if (!tiff_has_format_and_count(format, kUnsignedShort, count, 1))
    return;

  uint16_t metering_mode = tiff_state->uint16(data);
  // clang-format off
  switch (metering_mode) {
    case 0: printf(" (unknown)"); break;
    case 1: printf(" (Average)"); break;
    case 2: printf(" (CenterWeightedAverage)"); break;
    case 3: printf(" (Spot)"); break;
    case 4: printf(" (Multispot)"); break;
    case 5: printf(" (Pattern)"); break;
    case 6: printf(" (Partial)"); break;
    case 255: printf(" (other)"); break;
    default: printf(" (unknown value)");
  }
  // clang-format on
}

static void tiff_dump_light_source(const struct TiffState* tiff_state,
                                   uint16_t format,
                                   uint32_t count,
                                   const void* data) {
  if (!tiff_has_format_and_count(format, kUnsignedShort, count, 1))
    return;

  uint16_t light_source = tiff_state->uint16(data);
  // clang-format off
  switch (light_source) {
    case 0: printf(" (unknown)"); break;
    case 1: printf(" (Daylight)"); break;
    case 2: printf(" (Fluorescent)"); break;
    case 3: printf(" (Tungsten (incandescent light))"); break;
    case 4: printf(" (Flash)"); break;
    case 9: printf(" (Fine weather)"); break;
    case 10: printf(" (Cloudy weather)"); break;
    case 11: printf(" (Shade)"); break;
    case 12: printf(" (Daylight fluorescent (D 5700 - 7100K))"); break;
    case 13: printf(" (Day white fluorescent (N 4600 - 5500K))"); break;
    case 14: printf(" (Cool white fluorescent (W 3800 - 4500K))"); break;
    case 15: printf(" (White fluorescent (WW 3250 - 3800K))"); break;
    case 16: printf(" (Warm white fluorescent (L 2600 - 3250K))"); break;
    case 17: printf(" (Standard light A)"); break;
    case 18: printf(" (Standard light B)"); break;
    case 19: printf(" (Standard light C)"); break;
    case 20: printf(" (D55)"); break;
    case 21: printf(" (D65)"); break;
    case 22: printf(" (D75)"); break;
    case 23: printf(" (D50)"); break;
    case 24: printf(" (ISO studio tungsten)"); break;
    case 255: printf(" (other light source)"); break;
    default: printf(" (unknown value)");
  }
  // clang-format on
}

static void tiff_dump_flash(const struct TiffState* tiff_state,
                            uint16_t format,
                            uint32_t count,
                            const void* data) {
  if (!tiff_has_format_and_count(format, kUnsignedShort, count, 1))
    return;

  uint16_t flash = tiff_state->uint16(data);

  bool flash_fired = flash & 1;

  enum {
    no_strobe_return_function,
    reserved,
    strobe_return_light_not_detected,
    strobe_return_light_detected,
  } flash_return = (flash >> 1) & 3;

  enum {
    unknown,
    compulsory_flash_firing,
    compulsory_flash_suppression,
    auto_mode,
  } flash_mode = (flash >> 3) & 3;

  bool flash_function_present = !(flash & 0x20);

  bool red_eye_reduction_supported = flash & 0x40;

  printf(" (");

  if (flash_fired)
    printf("fired");
  else
    printf("did not fire");

  switch (flash_return) {
    case no_strobe_return_function:
      printf(", no strobe return function");
      break;
    case reserved:
      printf(", reserved");
      break;
    case strobe_return_light_not_detected:
      printf(", strobe return light not detected");
      break;
    case strobe_return_light_detected:
      printf(", strobe return light detected");
      break;
  }

  printf(", mode ");
  switch (flash_mode) {
    case unknown:
      printf("unknown");
      break;
    case compulsory_flash_firing:
      printf("on");
      break;
    case compulsory_flash_suppression:
      printf("off");
      break;
    case auto_mode:
      printf("auto");
      break;
  }

  if (flash_function_present)
    printf(", flash present");
  else
    printf(", no flash present");

  if (red_eye_reduction_supported)
    printf(", red-eye reduction supported");
  else
    printf(", no red-eye reduction mode or unknown");

  printf(")");
}

static void tiff_dump_subject_area(const struct TiffState* tiff_state,
                                   uint16_t format,
                                   uint32_t count,
                                   const void* data) {
  if (format != kUnsignedShort) {
    printf(" (invalid format %d, wanted %d)", format, kUnsignedShort);
    return;
  }
  if (count != 2 && count != 3 && count != 4) {
    printf(" (invalid count %d, wanted 2, 3, or 4)", count);
    return;
  }

  const uint8_t* p = (const uint8_t*)data;
  uint16_t x = tiff_state->uint16(p);
  uint16_t y = tiff_state->uint16(p + 2);
  if (count == 2) {
    printf(" (point: %d, %d)", x, y);
    return;
  }

  if (count == 3) {
    uint16_t d = tiff_state->uint16(p + 4);
    printf(" (circle at %d, %d with diameter %d)", x, y, d);
    return;
  }

  assert(count == 4);
  uint16_t x2 = tiff_state->uint16(p + 4);
  uint16_t y2 = tiff_state->uint16(p + 6);
  printf(" (rect: (%d, %d), (%d, %d))", x, y, x2, y2);
}

static void tiff_dump_user_comment(uint16_t format,
                                   uint32_t count,
                                   const void* data) {
  if (format != kUndefined) {
    printf(" (invalid format %d, wanted %d)", format, kUndefined);
    return;
  }
  if (count < 8) {
    printf(" (invalid count %d, wanted at least 8)", count);
    return;
  }

  printf(" (");
  if (memcmp(data, "ASCII\0\0", 8) == 0) {
    printf("ASCII: '%.*s'", count - 8, (const char*)data);
  } else if (memcmp(data, "JIS\0\0\0\0", 8) == 0) {
    printf("JIS");  // TODO: convert, print?
  } else if (memcmp(data, "UNICODE", 8) == 0) {
    // TODO: convert, print? DC-008 doesn't say what "Unicode" means here --
    // I suppose either UCS-2 or UTF-16.
    printf("UNICODE: ");
  } else if (memcmp(data, "\0\0\0\0\0\0\0", 8) == 0) {
    printf("Undefined encoding");
  } else {
    printf("Unknown encoding '%.8s'", (const char*)data);
  }
  printf(")");
}

static void tiff_dump_color_space(const struct TiffState* tiff_state,
                                  uint16_t format,
                                  uint32_t count,
                                  const void* data) {
  if (!tiff_has_format_and_count(format, kUnsignedShort, count, 1))
    return;

  uint16_t color_space = tiff_state->uint16(data);
  // clang-format off
  switch (color_space) {
    case 1: printf(" (sRGB)"); break;
    case 65535: printf(" (Uncalibrated)"); break;
    default: printf(" (unknown value)");
  }
  // clang-format on
}

static void tiff_dump_sensing_method(const struct TiffState* tiff_state,
                                     uint16_t format,
                                     uint32_t count,
                                     const void* data) {
  if (!tiff_has_format_and_count(format, kUnsignedShort, count, 1))
    return;

  uint16_t sensing_method = tiff_state->uint16(data);
  // clang-format off
  switch (sensing_method) {
    case 1: printf(" (Not defined)"); break;
    case 2: printf(" (One-chip color area sensor)"); break;
    case 3: printf(" (Two-chip color area sensor)"); break;
    case 4: printf(" (Three-chip color area sensor)"); break;
    case 5: printf(" (Color sequential area sensor)"); break;
    // no 6
    case 7: printf(" (Trilinear sensor)"); break;
    case 8: printf(" (Color sequential linear sensor)"); break;
    default: printf(" (unknown value)");
  }
  // clang-format on
}

static void tiff_dump_file_source(uint16_t format,
                                  uint32_t count,
                                  const void* data) {
  if (!tiff_has_format_and_count(format, kUndefined, count, 1))
    return;

  uint8_t file_source = *(const uint8_t*)data;
  printf(" (%d, ", file_source);
  // clang-format off
  switch (file_source) {
    case 0: printf("others"); break;
    case 1: printf("scanner of transparent type"); break;
    case 2: printf("scanner of reflex type"); break;
    case 3: printf("digital still camera (DSC)"); break;
    default: printf("unknown value");
  }
  // clang-format on
  printf(")");
}

static void tiff_dump_scene_type(uint16_t format,
                                 uint32_t count,
                                 const void* data) {
  if (!tiff_has_format_and_count(format, kUndefined, count, 1))
    return;

  uint8_t scene_type = *(const uint8_t*)data;
  printf(" (%d, ", scene_type);
  // clang-format off
  switch (scene_type) {
    case 1: printf("directly photographed image"); break;
    default: printf("unknown value");
  }
  // clang-format on
  printf(")");
}

static void tiff_dump_custom_rendered(const struct TiffState* tiff_state,
                                      uint16_t format,
                                      uint32_t count,
                                      const void* data) {
  if (!tiff_has_format_and_count(format, kUnsignedShort, count, 1))
    return;

  uint16_t custom_rendered = tiff_state->uint16(data);
  // clang-format off
  switch (custom_rendered) {
    case 0: printf(" (Normal process)"); break;
    case 1: printf(" (Custom process)"); break;
    default: printf(" (unknown value)");
  }
  // clang-format on
}

static void tiff_dump_exposure_mode(const struct TiffState* tiff_state,
                                    uint16_t format,
                                    uint32_t count,
                                    const void* data) {
  if (!tiff_has_format_and_count(format, kUnsignedShort, count, 1))
    return;

  uint16_t exposure_mode = tiff_state->uint16(data);
  // clang-format off
  switch (exposure_mode) {
    case 0: printf(" (Auto exposure)"); break;
    case 1: printf(" (Manual exposure)"); break;
    case 2: printf(" (Auto bracket)"); break;
    default: printf(" (unknown value)");
  }
  // clang-format on
}

static void tiff_dump_white_balance(const struct TiffState* tiff_state,
                                    uint16_t format,
                                    uint32_t count,
                                    const void* data) {
  if (!tiff_has_format_and_count(format, kUnsignedShort, count, 1))
    return;

  uint16_t white_balance = tiff_state->uint16(data);
  // clang-format off
  switch (white_balance) {
    case 0: printf(" (Auto white balance)"); break;
    case 1: printf(" (Manual white balance)"); break;
    default: printf(" (unknown value)");
  }
  // clang-format on
}

static void tiff_dump_scene_capture_type(const struct TiffState* tiff_state,
                                         uint16_t format,
                                         uint32_t count,
                                         const void* data) {
  if (!tiff_has_format_and_count(format, kUnsignedShort, count, 1))
    return;

  uint16_t scene_capture_type = tiff_state->uint16(data);
  // clang-format off
  switch (scene_capture_type) {
    case 0: printf(" (Standard)"); break;
    case 1: printf(" (Landscape)"); break;
    case 2: printf(" (Portrait)"); break;
    case 3: printf(" (Night scene)"); break;
    default: printf(" (unknown value)");
  }
  // clang-format on
}

static void tiff_dump_gain_control(const struct TiffState* tiff_state,
                                   uint16_t format,
                                   uint32_t count,
                                   const void* data) {
  if (!tiff_has_format_and_count(format, kUnsignedShort, count, 1))
    return;

  uint16_t gain_control = tiff_state->uint16(data);
  // clang-format off
  switch (gain_control) {
    case 0: printf(" (None)"); break;
    case 1: printf(" (Low gain up)"); break;
    case 2: printf(" (High gain up)"); break;
    case 3: printf(" (Low gain down)"); break;
    case 4: printf(" (High gain down)"); break;
    default: printf(" (unknown value)");
  }
  // clang-format on
}

static void tiff_dump_contrast(const struct TiffState* tiff_state,
                               uint16_t format,
                               uint32_t count,
                               const void* data) {
  if (!tiff_has_format_and_count(format, kUnsignedShort, count, 1))
    return;

  uint16_t contrast = tiff_state->uint16(data);
  // clang-format off
  switch (contrast) {
    case 0: printf(" (Normal)"); break;
    case 1: printf(" (Soft)"); break;
    case 2: printf(" (Hard)"); break;
    default: printf(" (unknown value)");
  }
  // clang-format on
}

static void tiff_dump_saturation(const struct TiffState* tiff_state,
                                 uint16_t format,
                                 uint32_t count,
                                 const void* data) {
  if (!tiff_has_format_and_count(format, kUnsignedShort, count, 1))
    return;

  uint16_t saturation = tiff_state->uint16(data);
  // clang-format off
  switch (saturation) {
    case 0: printf(" (Normal)"); break;
    case 1: printf(" (Low saturation)"); break;
    case 2: printf(" (High saturation)"); break;
    default: printf(" (unknown value)");
  }
  // clang-format on
}

static void tiff_dump_sharpness(const struct TiffState* tiff_state,
                                uint16_t format,
                                uint32_t count,
                                const void* data) {
  if (!tiff_has_format_and_count(format, kUnsignedShort, count, 1))
    return;

  uint16_t sharpness = tiff_state->uint16(data);
  // clang-format off
  switch (sharpness) {
    case 0: printf(" (Normal)"); break;
    case 1: printf(" (Soft)"); break;
    case 2: printf(" (Hard)"); break;
    default: printf(" (unknown value)");
  }
  // clang-format on
}

static void tiff_dump_subject_distance_range(const struct TiffState* tiff_state,
                                             uint16_t format,
                                             uint32_t count,
                                             const void* data) {
  if (!tiff_has_format_and_count(format, kUnsignedShort, count, 1))
    return;

  uint16_t subject_distance_range = tiff_state->uint16(data);
  // clang-format off
  switch (subject_distance_range) {
    case 0: printf(" (unknown)"); break;
    case 1: printf(" (Macro)"); break;
    case 2: printf(" (Close view)"); break;
    case 3: printf(" (Distant view)"); break;
    default: printf(" (unknown value)");
  }
  // clang-format on
}

static void tiff_dump_fraction(uint32_t numerator, uint32_t denominator) {
  if (denominator == 1)
    printf("%u", numerator);
  else if (denominator != 0)
    printf("%f", numerator / (double)denominator);
  else
    printf("%u/%u", numerator, denominator);
}

static void tiff_dump_lens_specification(const struct TiffState* tiff_state,
                                         uint16_t format,
                                         uint32_t count,
                                         const void* data) {
  if (!tiff_has_format_and_count(format, kUnsignedRational, count, 4))
    return;

  const uint8_t* p = (const uint8_t*)data;
  uint32_t min_focal_length_mm_numerator = tiff_state->uint32(p);
  uint32_t min_focal_length_mm_denominator = tiff_state->uint32(p + 4);

  uint32_t max_focal_length_mm_numerator = tiff_state->uint32(p + 8);
  uint32_t max_focal_length_mm_denominator = tiff_state->uint32(p + 12);

  uint32_t min_f_num_at_min_focal_len_numerator = tiff_state->uint32(p + 16);
  uint32_t min_f_num_at_min_focal_len_denominator = tiff_state->uint32(p + 20);

  uint32_t min_f_num_at_max_focal_len_numerator = tiff_state->uint32(p + 24);
  uint32_t min_f_num_at_max_focal_len_denominator = tiff_state->uint32(p + 28);

  printf(" (focal length ");

  tiff_dump_fraction(min_focal_length_mm_numerator,
                     min_focal_length_mm_denominator);

  if (max_focal_length_mm_numerator != min_focal_length_mm_numerator ||
      max_focal_length_mm_denominator != min_focal_length_mm_denominator) {
    printf(" - ");
    tiff_dump_fraction(max_focal_length_mm_numerator,
                       max_focal_length_mm_denominator);
  }

  printf(" mm");

  if (min_f_num_at_max_focal_len_numerator !=
          min_f_num_at_min_focal_len_numerator ||
      min_f_num_at_max_focal_len_denominator !=
          min_f_num_at_min_focal_len_denominator) {
    printf(", min f numbers ");

    tiff_dump_fraction(min_f_num_at_min_focal_len_numerator,
                       min_f_num_at_min_focal_len_denominator);
    printf(" at min, ");

    tiff_dump_fraction(min_f_num_at_max_focal_len_numerator,
                       min_f_num_at_max_focal_len_denominator);
    printf(" at max)");
  } else {
    printf(", min f number ");
    tiff_dump_fraction(min_f_num_at_min_focal_len_numerator,
                       min_f_num_at_min_focal_len_denominator);
    printf(")");
  }
}

static void tiff_dump_composite_image(const struct TiffState* tiff_state,
                                      uint16_t format,
                                      uint32_t count,
                                      const void* data) {
  if (!tiff_has_format_and_count(format, kUnsignedShort, count, 1))
    return;

  uint16_t composite_image = tiff_state->uint16(data);
  // clang-format off
  switch (composite_image) {
    case 1: printf(" (non-composite image)"); break;
    case 2: printf(" (General composite image)"); break;
    case 3: printf(" (Composite image captured while shooting)"); break;
    default: printf(" (unknown value)");
  }
  // clang-format on
}

static void tiff_dump_extra_exif_tag_info(const struct TiffState* tiff_state,
                                          uint16_t tag,
                                          uint16_t format,
                                          uint32_t count,
                                          const void* data) {
  if (tag == 274)
    tiff_dump_image_orientation(tiff_state, format, count, data);
  else if (tag == 296 || tag == 41488)
    tiff_dump_resolution_unit(tiff_state, format, count, data);
  else if (tag == 531)
    tiff_dump_ycbcr_positioning(tiff_state, format, count, data);
  else if (tag == 34850)
    tiff_dump_exposure_program(tiff_state, format, count, data);
  else if (tag == 34864)
    tiff_dump_sensitivity_type(tiff_state, format, count, data);
  else if (tag == 36864 || tag == 40960)
    tiff_dump_exif_version(format, count, data);
  else if (tag == 37121)
    tiff_dump_components_configuration(format, count, data);
  else if (tag == 37383)
    tiff_dump_metering_mode(tiff_state, format, count, data);
  else if (tag == 37384)
    tiff_dump_light_source(tiff_state, format, count, data);
  else if (tag == 37385)
    tiff_dump_flash(tiff_state, format, count, data);
  else if (tag == 37396)
    tiff_dump_subject_area(tiff_state, format, count, data);
  else if (tag == 37510)
    tiff_dump_user_comment(format, count, data);
  else if (tag == 40961)
    tiff_dump_color_space(tiff_state, format, count, data);
  else if (tag == 41495)
    tiff_dump_sensing_method(tiff_state, format, count, data);
  else if (tag == 41728)
    tiff_dump_file_source(format, count, data);
  else if (tag == 41729)
    tiff_dump_scene_type(format, count, data);
  else if (tag == 41985)
    tiff_dump_custom_rendered(tiff_state, format, count, data);
  else if (tag == 41986)
    tiff_dump_exposure_mode(tiff_state, format, count, data);
  else if (tag == 41987)
    tiff_dump_white_balance(tiff_state, format, count, data);
  else if (tag == 41990)
    tiff_dump_scene_capture_type(tiff_state, format, count, data);
  else if (tag == 41991)
    tiff_dump_gain_control(tiff_state, format, count, data);
  else if (tag == 41992)
    tiff_dump_contrast(tiff_state, format, count, data);
  else if (tag == 41993)
    tiff_dump_saturation(tiff_state, format, count, data);
  else if (tag == 41994)
    tiff_dump_sharpness(tiff_state, format, count, data);
  else if (tag == 41996)
    tiff_dump_subject_distance_range(tiff_state, format, count, data);
  else if (tag == 42034)
    tiff_dump_lens_specification(tiff_state, format, count, data);
  else if (tag == 42080)
    tiff_dump_composite_image(tiff_state, format, count, data);
}

static void tiff_dump_exif_gps_position(const struct TiffState* tiff_state,
                                        uint16_t format,
                                        uint32_t count,
                                        const void* data) {
  if (!tiff_has_format_and_count(format, kUnsignedRational, count, 3))
    return;

  const uint8_t* p = (const uint8_t*)data;
  uint32_t degrees_numerator = tiff_state->uint32(p);
  uint32_t degrees_denominator = tiff_state->uint32(p + 4);

  uint32_t minutes_numerator = tiff_state->uint32(p + 8);
  uint32_t minutes_denominator = tiff_state->uint32(p + 12);

  uint32_t seconds_numerator = tiff_state->uint32(p + 16);
  uint32_t seconds_denominator = tiff_state->uint32(p + 20);

  printf(" (");

  tiff_dump_fraction(degrees_numerator, degrees_denominator);
  printf("°");

  tiff_dump_fraction(minutes_numerator, minutes_denominator);
  printf("′");

  tiff_dump_fraction(seconds_numerator, seconds_denominator);
  printf("″");

  printf(")");
}

static void tiff_dump_exif_gps_altitude_ref(uint16_t format,
                                            uint32_t count,
                                            const void* data) {
  if (!tiff_has_format_and_count(format, kUnsignedByte, count, 1))
    return;

  uint8_t gps_altitude_ref = *(const uint8_t*)data;
  // clang-format off
  switch (gps_altitude_ref) {
    case 0: printf(" (meters above sea level)"); break;
    case 1: printf(" (meters below sea level)"); break;
    default: printf(" (unknown value)");
  }
  // clang-format on
}

static void tiff_dump_extra_gps_tag_info(const struct TiffState* tiff_state,
                                         uint16_t tag,
                                         uint16_t format,
                                         uint32_t count,
                                         const void* data) {
  if (tag == 2 || tag == 4)
    tiff_dump_exif_gps_position(tiff_state, format, count, data);
  else if (tag == 5)
    tiff_dump_exif_gps_altitude_ref(format, count, data);
}

static void tiff_dump_extra_interoperability_tag_info(
    const struct TiffState* tiff_state,
    uint16_t tag,
    uint16_t format,
    uint32_t count,
    const void* data) {
  (void)tiff_state;
  if (tag == 2)
    tiff_dump_exif_version(format, count, data);
}

static void tiff_dump_tag_info(const struct TiffState* tiff_state,
                               uint16_t tag,
                               uint16_t format,
                               uint32_t count,
                               const void* data) {
  iprintf(tiff_state->options, "tag %d", tag);
  const char* tag_name = tiff_state->tag_name(tag);
  if (tag_name)
    printf(" (%s)", tag_name);
  printf(" format %u (%s): count %u", format, TiffDataFormatNames[format],
         count);

  // TODO: print other formats
  uint16_t (*uint16)(const uint8_t*) = tiff_state->uint16;
  uint32_t (*uint32)(const uint8_t*) = tiff_state->uint32;
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

  tiff_state->dump_extra_tag_info(tiff_state, tag, format, count, data);

  printf("\n");
}

// Returns offset to next IFD, or 0 if none.
static uint32_t tiff_dump_one_ifd(const struct TiffState* tiff_state,
                                  uint32_t ifd_offset) {
  const uint8_t* begin = tiff_state->begin;
  ssize_t size = tiff_state->size;
  uint16_t (*uint16)(const uint8_t*) = tiff_state->uint16;
  uint32_t (*uint32)(const uint8_t*) = tiff_state->uint32;
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
  uint32_t interoperability_ifd_offset = 0;

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
    // TODO: Add bounds checking for data, data + total_size

    tiff_dump_tag_info(tiff_state, tag, format, count, data);

    if (tag == 513 && format == kUnsignedLong && count == 1)
      jpeg_offset = uint32(data);
    else if (tag == 514 && format == kUnsignedLong && count == 1)
      jpeg_length = uint32(data);
    else if (tag == 34665 && format == kUnsignedLong && count == 1)
      exif_ifd_offset = uint32(data);
    else if (tag == 34853 && format == kUnsignedLong && count == 1)
      gps_info_ifd_offset = uint32(data);
    else if (tag == 40965 && format == kUnsignedLong && count == 1)
      interoperability_ifd_offset = uint32(data);
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
    gps_tiff_state.dump_extra_tag_info = tiff_dump_extra_gps_tag_info;
    uint32_t next = tiff_dump_one_ifd(&gps_tiff_state, gps_info_ifd_offset);
    if (next != 0)
      iprintf(options, "unexpected next IFD at %d, skipping\n", next);
    decrease_indent(options);
  }
  if (interoperability_ifd_offset != 0) {
    iprintf(options, "interoperability IFD:\n");
    increase_indent(options);
    struct TiffState interoperability_tiff_state = *tiff_state;
    interoperability_tiff_state.tag_name = tiff_interoperability_tag_name;
    interoperability_tiff_state.dump_extra_tag_info =
        tiff_dump_extra_interoperability_tag_info;
    uint32_t next = tiff_dump_one_ifd(&interoperability_tiff_state,
                                      interoperability_ifd_offset);
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
      .dump_extra_tag_info = tiff_dump_extra_exif_tag_info,
      .options = options,
  };
  do {
    uint32_t next_ifd_offset = tiff_dump_one_ifd(&tiff_state, ifd_offset);
    ifd_offset = next_ifd_offset;
  } while (ifd_offset != 0);
}

// ICC dumping ////////////////////////////////////////////////////////////////
// ICC spec: https://www.color.org/specification/ICC.1-2022-05.pdf

struct ICCHeader {
  uint32_t profile_size;
  uint32_t preferred_cmm_type;

  uint8_t profile_version_major;
  uint8_t profile_version_minor_bugfix;
  uint16_t profile_version_zero;

  uint32_t profile_device_class;
  uint32_t data_color_space;
  uint32_t pcs;  // "Profile Connection Space"

  uint16_t year;
  uint16_t month;
  uint16_t day;
  uint16_t hour;
  uint16_t minutes;
  uint16_t seconds;

  uint32_t profile_file_signature;
  uint32_t primary_platform;

  uint32_t profile_flags;
  uint32_t device_manufacturer;
  uint32_t device_model;
  uint64_t device_attributes;
  uint32_t rendering_intent;

  int32_t pcs_illuminant_x;
  int32_t pcs_illuminant_y;
  int32_t pcs_illuminant_z;

  uint32_t profile_creator;

  uint8_t profile_md5[16];
  uint8_t reserved[28];
};
_Static_assert(sizeof(struct ICCHeader) == 128, "");

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

static double icc_s15fixed16(int32_t i) {
  return i / (double)0x10000;
}

// clang-format off
// Use with '%.4s' format string.
#define FOUR_CC_STR(f)          \
  (const char[]) {              \
    (char) ((f) >> 24u)         , \
    (char)(((f) >> 16u) & 0xffu), \
    (char)(((f) >>  8u) & 0xffu), \
    (char)( (f)         & 0xffu), \
  }
// clang-format on

static bool icc_check_type(const uint8_t* begin,
                           const char* name,
                           uint32_t expected_signature) {
  uint32_t type_signature = be_uint32(begin);
  if (type_signature != expected_signature) {
    printf("%s expected type '%.4s', got '%.4s'\n", name,
           FOUR_CC_STR(expected_signature), begin);
    return false;
  }
  uint32_t reserved = be_uint32(begin + 4);
  if (reserved != 0) {
    printf("%s expected reserved 0, got %d\n", name, reserved);
    return false;
  }
  return true;
}

static void icc_dump_textType(struct Options* options,
                              const uint8_t* begin,
                              uint32_t size) {
  // ICC.1:2022, 10.24 textType
  if (size < 9) {
    printf("textType must be at least 8 bytes, was %d\n", size);
    return;
  }

  if (!icc_check_type(begin, "textType", 0x74657874))  // 'text'
    return;

  iprintf(options, "%.*s\n", size - 9, begin + 8);

  // This is invalid per spec.
  if (begin[size - 1] != '\0')
    iprintf(options, "(textType not 0-terminated!)\n");
}

// rfc2781
static const int kLowSurrogateStart = 0xdc00;
static const int kLowSurrogateEnd = 0xdfff;
static const int kHighSurrogateStart = 0xd800;
static const int kHighSurrogateEnd = 0xdbff;

static void dump_as_utf8(uint32_t codepoint) {
  if (codepoint <= 0x7f) {
    putchar((int)codepoint);
  } else if (codepoint <= 0x7ff) {
    putchar(0xc0 | ((codepoint >> 6) & 0x1f));
    putchar(0x80 | (codepoint & 0x3f));
  } else if (codepoint <= 0xffff) {
    putchar(0xe0 | ((codepoint >> 12) & 0xf));
    putchar(0x80 | ((codepoint >> 6) & 0x3f));
    putchar(0x80 | (codepoint & 0x3f));
  } else if (codepoint <= 0x10ffff) {
    putchar(0xf0 | ((codepoint >> 18) & 0x7));
    putchar(0x80 | ((codepoint >> 12) & 0x3f));
    putchar(0x80 | ((codepoint >> 6) & 0x3f));
    putchar(0x80 | (codepoint & 0x3f));
  } else {
    printf("(outside of utf-8 range; 0x%x)", codepoint);
  }
}

static void dump_ucs2be(const uint8_t* ucs2_be, size_t num_codepoints) {
  // UCS2-BE text :/
  // And no uchar.h / c16rtomb() on macOS either (as of macOS 12.5) :/
  for (unsigned i = 0; i < num_codepoints; ++i, ucs2_be += 2) {
    uint16_t cur = be_uint16(ucs2_be);
    if (cur < kHighSurrogateStart || cur > kLowSurrogateEnd)
      dump_as_utf8(cur);
    else
      printf("(surrogate 0x%x invalid in UCS-2)", cur);
  }
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

  if (!icc_check_type(begin, "textDescriptionType", 0x64657363))  // 'desc'
    return;

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
    iprintf(options, "Unicode: %08x \"", unicode_language_code);
    dump_ucs2be(begin + 20 + ascii_invariant_size, unicode_count);
    printf("\"\n");
  }

  uint32_t unicode_size = unicode_count * 2;  // UCS-2

  uint16_t scriptcode_code =
      be_uint16(begin + 20 + ascii_invariant_size + unicode_size);
  uint8_t scriptcode_count = begin[22 + ascii_invariant_size + unicode_size];
  if (scriptcode_code == 0 && scriptcode_count == 0) {
    iprintf(options, "(no scriptcode data)\n");
    // TODO: Could verify that the following 67 bytes are all zero.
  } else if (scriptcode_code == 0 && scriptcode_count <= 67) {
    // scriptcode_code is apparently just ASCII (?)
    iprintf(options, "Scriptcode: \"%.*s\"\n", scriptcode_count,
            begin + 23 + ascii_invariant_size + unicode_size);
  } else {
    iprintf(options, "Scriptcode: TODO %u %u\n", scriptcode_code,
            scriptcode_count);
  }

  if (size != 23 + ascii_invariant_size + unicode_size + 67)
    iprintf(options, "surprising size %d, expected %d\n", size,
            23 + ascii_invariant_size + unicode_size + 67);
}

static void dump_utf16be(const uint8_t* utf16_be, size_t num_codepoints) {
  // UTF-16BE text :/
  // And no uchar.h / c16rtomb() on macOS either (as of macOS 12.5) :/
  for (unsigned i = 0; i < num_codepoints; ++i, utf16_be += 2) {
    uint16_t cur = be_uint16(utf16_be);
    if (cur < kHighSurrogateStart || cur > kLowSurrogateEnd)
      dump_as_utf8(cur);
    else if (cur >= kHighSurrogateStart && cur <= kHighSurrogateEnd) {
      if (i + 1 < num_codepoints) {
        ++i;
        utf16_be += 2;
        uint16_t next = be_uint16(utf16_be);
        if (next >= kLowSurrogateStart && next <= kLowSurrogateEnd) {
          dump_as_utf8(0x10000 + (((uint32_t)(cur - kHighSurrogateStart) << 10) | (uint32_t)(next - kLowSurrogateStart)));
        } else {
          printf("(high surrogate 0x%x followed by 0x%x instead of by low surrogate in UTF-16)", cur, next);
        }
      } else {
        printf("(high surrogate 0x%x followed by end-of-data instead of by low surrogate in UTF-16)", cur);
      }
    } else
      printf("(low surrogate 0x%x not following high surrogate in UTF-16)", cur);
  }
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

  if (!icc_check_type(begin, "multiLocalizedUnicodeType",
                      0x6D6C7563))  // 'mluc'
    return;

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

    dump_utf16be(begin + string_offset, string_length / 2);
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

  if (!icc_check_type(begin, "XYZType", 0x58595A20))  // 'XYZ '
    return;

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
    iprintf(options, "X = %.4f, Y = %.4f, Z = %.4f\n", icc_s15fixed16(xyz_x),
            icc_s15fixed16(xyz_y), icc_s15fixed16(xyz_z));
  }
}

static uint32_t icc_dump_curveType(struct Options* options,
                                   const uint8_t* begin,
                                   uint32_t size,
                                   bool want_exact_size_match) {
  // ICC.1:2022, 10.6 curveType
  if (size < 12) {
    printf("curveType must be at least 12 bytes, was %d\n", size);
    return 0;
  }

  if (!icc_check_type(begin, "curveType", 0x63757276))  // 'curv'
    return 0;

  uint32_t num_entries = be_uint32(begin + 8);
  if (want_exact_size_match) {
    if (size - 12 != num_entries * 2) {
      printf("curveType with %d entries must be %u bytes, was %d\n",
             num_entries, 12 + num_entries * 2, size);
      return 0;
    }
  } else {
    if (size - 12 < num_entries * 2) {
      printf("curveType with %d entries needs at least %u bytes, was %d\n",
             num_entries, 12 + num_entries * 2, size);
      return 0;
    }
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
  return 12 + num_entries * 2;
}

static uint32_t icc_dump_parametricCurveType(struct Options* options,
                                             const uint8_t* begin,
                                             uint32_t size,
                                             bool want_exact_size_match) {
  // ICC.1:2022, 10.18 parametricCurveType
  if (size < 12) {
    printf("parametricCurveType must be at least 12 bytes, was %d\n", size);
    return 0;
  }

  if (!icc_check_type(begin, "parametricCurveType", 0x70617261))  // 'para'
    return 0;

  uint16_t function_type = be_uint16(begin + 8);
  uint16_t reserved2 = be_uint16(begin + 10);
  if (reserved2 != 0) {
    printf("parametricCurveType expected reserved2 0, got %d\n", reserved2);
    return 0;
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
      return 0;
  }

  if (want_exact_size_match) {
    if (size - 12 != n * 4) {
      printf("parametricCurveType with %d params must be %u bytes, was %d\n", n,
             12 + n * 4, size);
      return 0;
    }
  } else {
    if (size - 12 < n * 4) {
      printf(
          "parametricCurveType with %d parms needs at least %u bytes, was %d\n",
          n, 12 + n * 4, size);
      return 0;
    }
  }

  const char names[] = {'g', 'a', 'b', 'c', 'd', 'e', 'f'};
  iprintf(options, "with ");
  for (unsigned i = 0; i < n; ++i) {
    int32_t v = (int32_t)be_uint32(begin + 12 + i * 4);
    printf("%c = %f", names[i], icc_s15fixed16(v));
    if (i != n - 1)
      printf(", ");
  }
  printf("\n");
  return 12 + n * 4;
}

static void icc_dumpS15Fixed16ArrayType(struct Options* options,
                                        const uint8_t* begin,
                                        uint32_t size,
                                        int newline_after) {
  // 10.22 s15Fixed16ArrayType
  if (size < 8) {
    printf("s15Fixed16ArrayType must be at least 8 bytes, was %d\n", size);
    return;
  }
  if (size % 4 != 0) {
    printf("s15Fixed16ArrayType must be multiple of 4, was %d\n", size);
    return;
  }

  if (!icc_check_type(begin, "s15Fixed16ArrayType", 0x73663332))  // 'sf32'
    return;

  int count = (size - 8) / 4;
  iprintf(options, "[");
  for (int i = 0; i < count; ++i) {
    if (i > 0)
      printf(",");
    if (newline_after > 0 && i > 0 && i % newline_after == 0) {
      printf("\n");
      iprintf(options, "  ");
    } else {
      printf(" ");
    }

    int32_t n = (int32_t)be_uint32(begin + 8 + i * 4);
    printf("%.4f", icc_s15fixed16(n));
  }
  printf(" ]\n");
}

static void icc_dumpCicpType(struct Options* options,
                             const uint8_t* begin,
                             uint32_t size) {
  // 9.2.17 cicpTag
  // "This tag defines Coding-Independent Code Points (CICP) for video signal
  // type identification."
  if (size != 12) {
    printf("cicpType must be 12 bytes, was %d\n", size);
    return;
  }

  if (!icc_check_type(begin, "cicpType", 0x63696370))  // 'cicp'
    return;

  uint8_t color_primaries = begin[8];
  uint8_t transfer_characteristics = begin[9];
  uint8_t matrix_coefficients = begin[10];
  uint8_t video_full_range_flag = begin[11];

  // "The fields ColourPrimaries, TransferCharacteristics, MatrixCoefficients,
  // and VideoFullRangeFlag shall be encoded as specified in Recommendation
  // ITU-T H.273."

  iprintf(options, "color primaries: %d\n", color_primaries);
  iprintf(options, "transfer characteristics: %d\n", transfer_characteristics);
  iprintf(options, "matrix coefficients: %d\n", matrix_coefficients);
  iprintf(options, "video full range flag: %d\n", video_full_range_flag);
}

static const char* icc_technology_description(uint32_t tech) {
  // Table 29 — Technology signatures
  switch (tech) {
    case 0x6673636E:  // 'fscn'
      return "Film scanner";
    case 0x6463616D:  // 'dcam'
      return "Digital camera";
    case 0x7273636E:  // 'rscn'
      return "Reflective scanner";
    case 0x696A6574:  // 'ijet'
      return "Ink jet printer";
    case 0x74776178:  // 'twax'
      return "Thermal wax printer";
    case 0x6570686F:  // 'epho'
      return "Electrophotographic printer";
    case 0x65737461:  // 'esta'
      return "Electrostatic printer";
    case 0x64737562:  // 'dsub'
      return "Dye sublimation printer";
    case 0x7270686F:  // 'rpho'
      return "Photographic paper printer";
    case 0x6670726E:  // 'fprn'
      return "Film writer";
    case 0x7669646D:  // 'vidm'
      return "Video monitor";
    case 0x76696463:  // 'vidc'
      return "Video camera";
    case 0x706A7476:  // 'pjtv'
      return "Projection television";
    case 0x43525420:  // 'CRT '
      return "Cathode ray tube display";
    case 0x504D4420:  // 'PMD '
      return "Passive matrix display";
    case 0x414D4420:  // 'AMD '
      return "Active matrix display";
    case 0x4C434420:  // 'LCD '
      return "Liquid crystal display";
    case 0x4F4C4544:  // 'OLED'
      return "Organic LED display";
    case 0x4B504344:  // 'KPCD'
      return "Photo CD";
    case 0x696D6773:  // 'imgs'
      return "Photographic image setter";
    case 0x67726176:  // 'grav'
      return "Gravure";
    case 0x6F666673:  // 'offs'
      return "Offset lithography";
    case 0x73696C6B:  // 'silk'
      return "Silkscreen";
    case 0x666C6578:  // 'flex'
      return "Flexography";
    case 0x6D706673:  // 'mpfs'
      return "Motion picture film scanner";
    case 0x6D706672:  // 'mpfr'
      return "Motion picture film recorder";
    case 0x646D7063:  // 'dmpc'
      return "Digital motion picture camera";
    case 0x64636A70:  // 'dcpj'
      return "Digital cinema projector";
    default:
      return NULL;
  }
}

static void icc_dumpMeasurementType(struct Options* options,
                                    const uint8_t* begin,
                                    uint32_t size) {
  // 10.14 measurementType
  if (size != 36) {
    printf("measurementType must be 36 bytes, was %d\n", size);
    return;
  }

  if (!icc_check_type(begin, "measurementType", 0x6D656173))  // 'meas'
    return;

  uint32_t standard_observer = be_uint32(begin + 8);
  int32_t xyz_x = (int32_t)be_uint32(begin + 12);
  int32_t xyz_y = (int32_t)be_uint32(begin + 16 + 4);
  int32_t xyz_z = (int32_t)be_uint32(begin + 20);
  uint32_t measurement_geometry = be_uint32(begin + 24);
  uint32_t measurement_flare = be_uint32(begin + 28);
  uint32_t standard_illuminant = be_uint32(begin + 32);

  // Table 50 — Standard observer encodings
  iprintf(options, "standard observer: %u", standard_observer);
  switch (standard_observer) {
    case 0:
      printf(" (Unknown)");
      break;
    case 1:
      printf(" (CIE 1931 standard colorimetric observer)");
      break;
    case 2:
      printf(" (CIE 1964 standard colorimetric observer )");
      break;
    default:
      printf(" (Invalid value)");
      break;
  }
  printf("\n");

  iprintf(options,
          "tristimulus values for measurement backing: "
          "X = %.4f, Y = %.4f, Z = %.4f\n",
          icc_s15fixed16(xyz_x), icc_s15fixed16(xyz_y), icc_s15fixed16(xyz_z));

  // Table 51 — Measurement geometry encodings
  iprintf(options, "measurement geometry: %u", measurement_geometry);
  switch (measurement_geometry) {
    case 0:
      printf(" (Unknown)");
      break;
    case 1:
      printf(" (0°:45° or 45°:0°)");
      break;
    case 2:
      printf(" (0°:d or d:0° )");
      break;
    default:
      printf(" (Invalid value)");
      break;
  }
  printf("\n");

  iprintf(options, "measurement flare: %f%%\n",
          100 * (measurement_flare / (double)0x10000));

  // Table 53 — Standard illuminant encodings
  iprintf(options, "standard illuminant: %u", standard_illuminant);
  switch (standard_illuminant) {
    case 0:
      printf(" (Unknown)");
      break;
    case 1:
      printf(" (D50)");
      break;
    case 2:
      printf(" (D65)");
      break;
    case 3:
      printf(" (D93)");
      break;
    case 4:
      printf(" (F2)");
      break;
    case 5:
      printf(" (D55)");
      break;
    case 6:
      printf(" (A)");
      break;
    case 7:
      printf(" (E)");
      break;
    case 8:
      printf(" (F8)");
      break;
    default:
      printf(" (Invalid value)");
      break;
  }
  printf("\n");
}

static void icc_dumpSignatureType(struct Options* options,
                                  const uint8_t* begin,
                                  uint32_t size,
                                  const char* (*details)(uint32_t)) {
  // 10.23 signatureType
  if (size != 12) {
    printf("signatureType must be 12 bytes, was %d\n", size);
    return;
  }

  if (!icc_check_type(begin, "signatureType", 0x73696720))  // 'sig '
    return;

  // 9.2.49 technologyTag
  uint32_t tag = be_uint32(begin + 8);
  iprintf(options, "signature: '%.4s'", FOUR_CC_STR(tag));

  const char* description = details(tag);
  if (description)
    printf(" (%s)", description);
  printf("\n");
}

static uint8_t icc_saturate_u8(double d) {
  double d8 = round(d * 255);
  if (d8 < 0)
    return 0;
  if (d8 > 255)
    return 255;
  return (uint8_t)d8;
}

static void icc_xyz_to_rgb(double x,
                           double y,
                           double z,
                           uint8_t* r8,
                           uint8_t* g8,
                           uint8_t* b8) {
  // This uses the sRGB D50 reference white value.
  // Assuming an rgb output color space here is a bit silly, but hey.
  // See http://www.brucelindbloom.com/index.html?Eqn_RGB_XYZ_Matrix.html
  // FIXME: Do chromatic adaptation.
  double r = 3.1338561 * x + -1.6168667 * y + -0.4906146 * z;
  double g = -0.9787684 * x + 1.9161415 * y + 0.0334540 * z;
  double b = 0.0719453 * x + -0.2289914 * y + 1.4052427 * z;

  *r8 = icc_saturate_u8(r);
  *g8 = icc_saturate_u8(g);
  *b8 = icc_saturate_u8(b);
}

static void icc_xyz16_to_rgb(uint16_t x16,
                             uint16_t y16,
                             uint16_t z16,
                             uint8_t* r8,
                             uint8_t* g8,
                             uint8_t* b8) {
  // 6.3.4.2 General PCS encoding:
  // "For the 16-bit integer based PCSXYZ encoding, each component is
  // encoded as a u1Fixed15Number".
  double x = x16 / (double)0x8000;
  double y = y16 / (double)0x8000;
  double z = z16 / (double)0x8000;
  icc_xyz_to_rgb(x, y, z, r8, g8, b8);
}

static bool icc_is_truecolor_terminal(void) {
  const char* e = getenv("COLORTERM");
  return e != NULL && strcmp(e, "truecolor") == 0;
}

// Assumes 3 output colors.
static void icc_colored_lut_row_xyz_to_rgb_16(const uint8_t* begin,
                                              uint8_t n,
                                              char end) {
  for (unsigned b = 0; b < n; ++b) {
    uint16_t x = be_uint16(begin);
    uint16_t y = be_uint16(begin + 2);
    uint16_t z = be_uint16(begin + 4);
    begin += 6;

    uint8_t r8, g8, b8;
    icc_xyz16_to_rgb(x, y, z, &r8, &g8, &b8);

    printf("\033[48;2;%u;%u;%um.", r8, g8, b8);
  }
  printf("\033[0m%c", end);
}

static void icc_lab16_to_xyz(uint16_t l16,
                             uint16_t a16,
                             uint16_t b16,
                             double* x,
                             double* y,
                             double* z) {
  // 6.3.4.2 General PCS encoding describes how Lab is encoded in 16 bit.
  double L = 100 * (l16 / 65535.0);
  double a = 255 * (a16 / 65535.0) - 128;
  double b = 255 * (b16 / 65535.0) - 128;

  // See http://www.brucelindbloom.com/Eqn_Lab_to_XYZ.html
  double fy = (L + 16) / 116;
  double fx = a / 500 + fy;
  double fz = fy - b / 200;

  const double EPS = 0.008856;
  const double KAPPA = 903.3;

  double xr;
  double fx3 = fx * fx * fx;
  if (fx3 > EPS)
    xr = fx3;
  else
    xr = (116 * fx - 16) / KAPPA;

  double yr;
  if (L > KAPPA * EPS) {
    yr = ((L + 16) / 116);
    yr = yr * yr * yr;
  } else {
    yr = L / KAPPA;
  }

  double zr;
  double fz3 = fz * fz * fz;
  if (fz3 > EPS)
    zr = fz3;
  else
    zr = (116 * fz - 16) / KAPPA;

  // D50
  // FIXME: Get this from profile instead of hardcoding (?)
  const double ref_white_x = 0.9642;
  const double ref_white_y = 1.0000;
  const double ref_white_z = 0.8249;

  *x = xr * ref_white_x;
  *y = yr * ref_white_y;
  *z = zr * ref_white_z;
}

// Assumes 3 output colors.
static void icc_colored_lut_row_lab_to_rgb_16(const uint8_t* begin,
                                              uint8_t n,
                                              char end) {
  for (unsigned blue = 0; blue < n; ++blue) {
    uint16_t L = be_uint16(begin);
    uint16_t a = be_uint16(begin + 2);
    uint16_t b = be_uint16(begin + 4);
    begin += 6;

    double x, y, z;
    icc_lab16_to_xyz(L, a, b, &x, &y, &z);
    uint8_t r8, g8, b8;
    icc_xyz_to_rgb(x, y, z, &r8, &g8, &b8);

    printf("\033[48;2;%u;%u;%um.", r8, g8, b8);
  }
  printf("\033[0m%c", end);
}

static void icc_dump_clut_3_3_truecolor(struct Options* options,
                                        uint8_t clut_size_r,
                                        uint8_t clut_size_g,
                                        uint8_t clut_size_b,
                                        const uint8_t* clut_data,
                                        void (*dump_row)(const uint8_t*,
                                                         uint8_t,
                                                         char)) {
  // This assumes RGB, and 24-bit color terminal support
  // (i.e. iTerm2 is in, Terminal.app is out).
  unsigned grids_per_line =
      (unsigned)(160 - options->current_indent) / (clut_size_g + 1);
  if (grids_per_line < 1)
    grids_per_line = 1;

  for (unsigned r = 0; r < clut_size_r; r += grids_per_line) {
    unsigned grids_this_line = grids_per_line;
    if (r + grids_per_line >= clut_size_r) {
      // Overflow line (maybe empty).
      grids_this_line = clut_size_r % grids_per_line;
      if (grids_this_line == 0)
        break;
    }

    for (unsigned g = 0; g < clut_size_g; ++g) {
      iprintf(options, "");
      for (unsigned i = 0; i < grids_this_line; ++i) {
        const uint8_t* line = clut_data;
        line += ((r + i) * clut_size_g + g) * clut_size_b * 6;
        dump_row(line, clut_size_b, i == grids_this_line - 1 ? '\n' : ' ');
      }
    }
    printf("\n");
  }
}

static void icc_dump_clut_values(struct Options* options,
                                 uint8_t clut_size_r,
                                 uint8_t clut_size_g,
                                 uint8_t clut_size_b,
                                 uint8_t num_output_channels,
                                 const uint8_t* clut_data) {
  for (unsigned r = 0; r < clut_size_r; ++r) {
    for (unsigned g = 0; g < clut_size_g; ++g) {
      iprintf(options, "");
      for (unsigned b = 0; b < clut_size_b; ++b) {
        printf(" ");
        for (unsigned i = 0; i < num_output_channels; ++i) {
          uint16_t value = be_uint16(clut_data);
          clut_data += 2;
          printf("%u", value);
          if (i != num_output_channels - 1)
            printf(",");
        }
      }
      printf("\n");
    }
    printf("\n");
  }
}

static void icc_dump_clut(struct Options* options,
                          const struct ICCHeader* icc,
                          uint8_t num_input_channels,
                          uint8_t num_output_channels,
                          uint8_t bytes_per_entry,
                          const uint8_t* clut_sizes,
                          const uint8_t* clut_data) {
  // num_input_channels-dimensional space with clut_sizes[i] along axis i.
  // Each point contains num_output_channels points.
  if (num_input_channels == 3 && num_output_channels == 3 &&
      bytes_per_entry == 2 && icc_is_truecolor_terminal()) {
    void (*dump)(const uint8_t* begin, uint8_t n, char end) = NULL;
    if (icc->pcs == 0x58595A20 /* 'XYZ ' */)
      dump = icc_colored_lut_row_xyz_to_rgb_16;
    else if (icc->pcs == 0x4C616220 /* 'Lab ' */)
      dump = icc_colored_lut_row_lab_to_rgb_16;

    if (!dump) {
      printf("(not dumping CLUT due to invalid PCS\n");
    } else {
      icc_dump_clut_3_3_truecolor(options, clut_sizes[0], clut_sizes[1],
                                  clut_sizes[2], clut_data, dump);
    }
  } else if (num_input_channels == 3 && bytes_per_entry == 2 &&
             options->dump_luts) {
    icc_dump_clut_values(options, clut_sizes[0], clut_sizes[1], clut_sizes[2],
                         num_output_channels, clut_data);
  } else {
    iprintf(options, "not dumping lut.");
    if (num_input_channels == 3) {
      if (num_output_channels == 3)
        printf(" run in a truecolor terminal, or");
      printf(" pass '--dump-luts'.");
    }
    printf("\n");
  }
}

static void icc_dump_lut16Type(struct Options* options,
                               const struct ICCHeader* icc,
                               const uint8_t* begin,
                               uint32_t size) {
  // https://www.color.org/specification/ICC.1-2022-05.pdf
  // 10.10 lut16Type
  if (size < 52) {
    printf("lut16Type must be at least 52 bytes, was %d\n", size);
    return;
  }

  if (!icc_check_type(begin, "lut16Type", 0x6D667432))  // 'mft2'
    return;

  uint8_t num_input_channels = begin[8];
  uint8_t num_output_channels = begin[9];
  uint8_t num_clut_grid_points = begin[10];
  uint8_t reserved2 = begin[11];
  if (reserved2 != 0) {
    printf("lut16Type expected reserved2 0, got %d\n", reserved2);
    return;
  }

  // This limitation doesn't exist in the spec. But it does exist in
  // icc_dump_lutAToBType in the spec, it'll probably always be true in
  // practice, and it allows us to not allocate on the heap below.
  if (num_input_channels > 16) {
    printf("this program assumes most 16 input channels, got %u\n",
           num_input_channels);
    return;
  }

  double e[9];
  for (int i = 0; i < 9; ++i)
    e[i] = icc_s15fixed16((int32_t)be_uint32(begin + 12 + i * 4));

  uint16_t num_input_table_entries = be_uint16(begin + 48);
  uint16_t num_output_table_entries = be_uint16(begin + 50);

  iprintf(options, "num input channels: %u\n", num_input_channels);
  iprintf(options, "num output channels: %u\n", num_output_channels);
  iprintf(options, "num CLUT grid points: %u\n", num_clut_grid_points);

  iprintf(options, "e matrix %.4f %.4f %.4f\n", e[0], e[1], e[2]);
  iprintf(options, "         %.4f %.4f %.4f\n", e[3], e[4], e[5]);
  iprintf(options, "         %.4f %.4f %.4f\n", e[6], e[7], e[8]);

  iprintf(options, "num input table entries: %u\n", num_input_table_entries);
  if (num_input_table_entries < 2 || num_input_table_entries > 4096) {
    printf("lut16Type expected num input tables to be in [2, 4096], got %u\n",
           num_input_table_entries);
    return;
  }

  iprintf(options, "num output table entries: %u\n", num_output_table_entries);
  if (num_output_table_entries < 2 || num_output_table_entries > 4096) {
    printf("lut16Type expected num output tables to be in [2, 4096], got %u\n",
           num_output_table_entries);
    return;
  }

  // FIXME: bounds checking
  size_t offset = 52;

  iprintf(options, "input tables:\n");
  for (unsigned c = 0; c < num_input_channels; ++c) {
    iprintf(options, "  channel %u:", c);
    for (unsigned i = 0; i < num_input_table_entries; ++i) {
      printf(" %u", be_uint16(begin + offset));
      offset += 2;
    }
    printf("\n");
  }

  // num_input_channels-dimensional space with num_clut_grid_points along
  // each axis, for a total of num_clut_grid_points ** num_input_channels
  // points. Each point contains num_output_channels points.
  uint8_t clut_sizes[16];
  for (unsigned i = 0; i < num_input_channels; ++i)
    clut_sizes[i] = num_clut_grid_points;
  icc_dump_clut(options, icc, num_input_channels, num_output_channels,
                /*bytes_per_entry=*/2, clut_sizes, begin + offset);
  size_t clut_values_size = 2 * num_output_channels;
  for (unsigned i = 0; i < num_input_channels; ++i)
    clut_values_size *= num_clut_grid_points;
  offset += clut_values_size;

  iprintf(options, "output tables:\n");
  for (unsigned c = 0; c < num_output_channels; ++c) {
    iprintf(options, "  channel %u:", c);
    for (unsigned i = 0; i < num_output_table_entries; ++i) {
      printf(" %u", be_uint16(begin + offset));
      offset += 2;
    }
    printf("\n");
  }
}

static void icc_dump_lut8Type(struct Options* options,
                              const struct ICCHeader* icc,
                              const uint8_t* begin,
                              uint32_t size) {
  // https://www.color.org/specification/ICC.1-2022-05.pdf
  // 10.11 lut8Type
  if (size < 52) {
    printf("lut8Type must be at least 52 bytes, was %d\n", size);
    return;
  }

  if (!icc_check_type(begin, "lut8Type", 0x6D667431))  // 'mft1'
    return;

  uint8_t num_input_channels = begin[8];
  uint8_t num_output_channels = begin[9];
  uint8_t num_clut_grid_points = begin[10];
  uint8_t reserved2 = begin[11];
  if (reserved2 != 0) {
    printf("lut8Type expected reserved2 0, got %d\n", reserved2);
    return;
  }

  // This limitation doesn't exist in the spec. But it does exist in
  // icc_dump_lutAToBType in the spec, it'll probably always be true in
  // practice, and it allows us to not allocate on the heap below.
  if (num_input_channels > 16) {
    printf("this program assumes most 16 input channels, got %u\n",
           num_input_channels);
    return;
  }

  double e[9];
  for (int i = 0; i < 9; ++i)
    e[i] = icc_s15fixed16((int32_t)be_uint32(begin + 12 + i * 4));

  iprintf(options, "num input channels: %u\n", num_input_channels);
  iprintf(options, "num output channels: %u\n", num_output_channels);
  iprintf(options, "num CLUT grid points: %u\n", num_clut_grid_points);

  iprintf(options, "e matrix %.4f %.4f %.4f\n", e[0], e[1], e[2]);
  iprintf(options, "         %.4f %.4f %.4f\n", e[3], e[4], e[5]);
  iprintf(options, "         %.4f %.4f %.4f\n", e[6], e[7], e[8]);

  // FIXME: bounds checking
  size_t offset = 48;

  iprintf(options, "input tables:\n");
  for (unsigned c = 0; c < num_input_channels; ++c) {
    iprintf(options, "  channel %u:", c);
    for (unsigned i = 0; i < 255; ++i)
      printf(" %u", begin[offset++]);
    printf("\n");
  }

  // num_input_channels-dimensional space with num_clut_grid_points along
  // each axis, for a total of num_clut_grid_points ** num_input_channels
  // points. Each point contains num_output_channels points.
  uint8_t clut_sizes[16];
  for (unsigned i = 0; i < num_input_channels; ++i)
    clut_sizes[i] = num_clut_grid_points;
  icc_dump_clut(options, icc, num_input_channels, num_output_channels,
                /*bytes_per_entry=*/1, clut_sizes, begin + offset);
  size_t clut_values_size = num_output_channels;
  for (unsigned i = 0; i < num_input_channels; ++i)
    clut_values_size *= num_clut_grid_points;
  offset += clut_values_size;

  iprintf(options, "output tables:\n");
  for (unsigned c = 0; c < num_output_channels; ++c) {
    iprintf(options, "  channel %u:", c);
    for (unsigned i = 0; i < 255; ++i)
      printf(" %u", begin[offset++]);
    printf("\n");
  }
}

static uint32_t pad_to_4(uint32_t v) {
  return v + (4 - v % 4) % 4;
}

static void icc_dump_curves(struct Options* options,
                            uint8_t num_curves,
                            const uint8_t* begin,
                            uint32_t size,
                            const uint8_t* curves_begin,
                            const char* format_string) {
  for (unsigned i = 0; i < num_curves; ++i) {
    iprintf(options, format_string, i);
    increase_indent(options);
    // FIXME: bounds checking
    uint32_t size_left = size - (uint32_t)(curves_begin - begin);
    uint32_t curve_type = be_uint32(curves_begin);
    uint32_t bytes_read = 0;
    if (curve_type == 0x63757276) {  // 'curv'
      bytes_read = icc_dump_curveType(options, curves_begin, size_left,
                                      /*want_exact_size_match=*/false);
    } else if (curve_type == 0x70617261) {  // 'para'
      bytes_read =
          icc_dump_parametricCurveType(options, curves_begin, size_left,
                                       /*want_exact_size_match=*/false);
    } else {
      iprintf(options, "unexpected type, expected 'curv' or 'para'\n");
    }
    if (bytes_read == 0)
      return;
    curves_begin += pad_to_4(bytes_read);
    decrease_indent(options);
  }
}

static void icc_dump_lutAToBType(struct Options* options,
                                 const struct ICCHeader* icc,
                                 const uint8_t* begin,
                                 uint32_t size) {
  // https://www.color.org/specification/ICC.1-2022-05.pdf
  // 10.12 lutAToBType
  if (size < 32) {
    printf("lutAToBType must be at least 32 bytes, was %d\n", size);
    return;
  }

  if (!icc_check_type(begin, "lutAToBType", 0x6D414220))  // 'mAB '
    return;

  uint8_t num_input_channels = begin[8];
  uint8_t num_output_channels = begin[9];
  uint16_t reserved2 = be_uint16(begin + 10);
  if (reserved2 != 0) {
    printf("lutAToBType expected reserved2 0, got %d\n", reserved2);
    return;
  }

  uint32_t offset_to_b_curves = be_uint32(begin + 12);
  uint32_t offset_to_matrix = be_uint32(begin + 16);
  uint32_t offset_to_m_curves = be_uint32(begin + 20);
  uint32_t offset_to_clut = be_uint32(begin + 24);
  uint32_t offset_to_a_curves = be_uint32(begin + 28);

  iprintf(options, "num input channels: %u\n", num_input_channels);
  iprintf(options, "num output channels: %u\n", num_output_channels);

  // 10.12.2 “A” curves
  if (offset_to_a_curves) {
    icc_dump_curves(options, num_input_channels, begin, size,
                    begin + offset_to_a_curves,
                    "'A' curve for input channel %u:\n");
  }

  // 10.12.3 CLUT
  if (offset_to_clut) {
    const uint8_t* clut_begin = begin + offset_to_clut;

    if (num_input_channels > 16) {
      printf("can have at most 16 input channels, got %u\n",
             num_input_channels);
      return;
    }

    iprintf(options, "clut sizes:");
    for (unsigned i = 0; i < num_input_channels; ++i)
      printf(" %u", clut_begin[i]);
    printf("\n");
    for (unsigned i = num_input_channels; i < 16; ++i)
      if (clut_begin[i] != 0)
        printf("clut size[%u] expected to be 0 but was %u\n", i, clut_begin[i]);

    uint8_t bytes_per_entry = clut_begin[16];
    iprintf(options, "clut bytes per entry: %u\n", bytes_per_entry);
    for (unsigned i = 17; i < 20; ++i)
      if (clut_begin[i] != 0)
        printf("clut padding[%u] expected to be 0 but was %u\n", i - 17,
               clut_begin[i]);

    if (bytes_per_entry != 1 && bytes_per_entry != 2) {
      printf("expected bytes_per_entry to be 1 or 2, was %u\n",
             bytes_per_entry);
      return;
    }

    icc_dump_clut(options, icc, num_input_channels, num_output_channels,
                  bytes_per_entry, clut_begin, clut_begin + 20);
  }

  // 10.12.4 “M” curves
  if (offset_to_m_curves) {
    icc_dump_curves(options, num_output_channels, begin, size,
                    begin + offset_to_m_curves,
                    "'M' curve for output channel %u:\n");
  }

  // 10.12.5 Matrix
  double e[12];
  if (offset_to_matrix) {
    const uint8_t* matrix_begin = begin + offset_to_matrix;
    for (int i = 0; i < 12; ++i)
      e[i] = icc_s15fixed16((int32_t)be_uint32(matrix_begin + i * 4));
    iprintf(options, "e matrix %.4f %.4f %.4f %.4f\n", e[0], e[1], e[2], e[9]);
    iprintf(options, "         %.4f %.4f %.4f %.4f\n", e[3], e[4], e[5], e[10]);
    iprintf(options, "         %.4f %.4f %.4f %.4f\n", e[6], e[7], e[8], e[11]);
  }

  // 10.12.6 “B” curves
  if (offset_to_b_curves) {
    icc_dump_curves(options, num_output_channels, begin, size,
                    begin + offset_to_b_curves,
                    "'B' curve for output channel %u:\n");
  }
}

static void icc_dump_lutBToAType(struct Options* options,
                                 const struct ICCHeader* icc,
                                 const uint8_t* begin,
                                 uint32_t size) {
  // https://www.color.org/specification/ICC.1-2022-05.pdf
  // 10.13 lutBToAType
  if (size < 32) {
    printf("lutBToAType must be at least 32 bytes, was %d\n", size);
    return;
  }

  if (!icc_check_type(begin, "lutBToAType", 0x6D424120))  // 'mBA '
    return;

  uint8_t num_input_channels = begin[8];
  uint8_t num_output_channels = begin[9];
  uint16_t reserved2 = be_uint16(begin + 10);
  if (reserved2 != 0) {
    printf("lutBToAType expected reserved2 0, got %d\n", reserved2);
    return;
  }

  uint32_t offset_to_b_curves = be_uint32(begin + 12);
  uint32_t offset_to_matrix = be_uint32(begin + 16);
  uint32_t offset_to_m_curves = be_uint32(begin + 20);
  uint32_t offset_to_clut = be_uint32(begin + 24);
  uint32_t offset_to_a_curves = be_uint32(begin + 28);

  iprintf(options, "num input channels: %u\n", num_input_channels);
  iprintf(options, "num output channels: %u\n", num_output_channels);

  // 10.13.2 “B” curves
  if (offset_to_b_curves) {
    icc_dump_curves(options, num_input_channels, begin, size,
                    begin + offset_to_b_curves,
                    "'B' curve for input channel %u:\n");
  }

  // 10.13.3 Matrix
  double e[12];
  if (offset_to_matrix) {
    const uint8_t* matrix_begin = begin + offset_to_matrix;
    for (int i = 0; i < 12; ++i)
      e[i] = icc_s15fixed16((int32_t)be_uint32(matrix_begin + i * 4));
    iprintf(options, "e matrix %.4f %.4f %.4f %.4f\n", e[0], e[1], e[2], e[9]);
    iprintf(options, "         %.4f %.4f %.4f %.4f\n", e[3], e[4], e[5], e[10]);
    iprintf(options, "         %.4f %.4f %.4f %.4f\n", e[6], e[7], e[8], e[11]);
  }

  // 10.13.4 “M” curves
  if (offset_to_m_curves) {
    icc_dump_curves(options, num_input_channels, begin, size,
                    begin + offset_to_m_curves,
                    "'M' curve for output channel %u:\n");
  }

  // 10.13.5 CLUT
  if (offset_to_clut) {
    const uint8_t* clut_begin = begin + offset_to_clut;

    if (num_input_channels > 16) {
      printf("can have at most 16 input channels, got %u\n",
             num_input_channels);
      return;
    }

    iprintf(options, "clut sizes:");
    for (unsigned i = 0; i < num_input_channels; ++i)
      printf(" %u", clut_begin[i]);
    printf("\n");
    for (unsigned i = num_input_channels; i < 16; ++i)
      if (clut_begin[i] != 0)
        printf("clut size[%u] expected to be 0 but was %u\n", i, clut_begin[i]);

    uint8_t bytes_per_entry = clut_begin[16];
    iprintf(options, "clut bytes per entry: %u\n", bytes_per_entry);
    for (unsigned i = 17; i < 20; ++i)
      if (clut_begin[i] != 0)
        printf("clut padding[%u] expected to be 0 but was %u\n", i - 17,
               clut_begin[i]);

    if (bytes_per_entry != 1 && bytes_per_entry != 2) {
      printf("expected bytes_per_entry to be 1 or 2, was %u\n",
             bytes_per_entry);
      return;
    }

    icc_dump_clut(options, icc, num_input_channels, num_output_channels,
                  bytes_per_entry, clut_begin, clut_begin + 20);
  }

  // 10.13.6 “A” curves
  if (offset_to_a_curves) {
    icc_dump_curves(options, num_output_channels, begin, size,
                    begin + offset_to_a_curves,
                    "'A' curve for output channel %u:\n");
  }
}

static void icc_read_header(const uint8_t* icc_header,
                            size_t size,
                            struct ICCHeader* icc) {
  // https://www.color.org/specification/ICC.1-2022-05.pdf
  // 7.2 Profile header
  assert(size >= 128);

  icc->profile_size = be_uint32(icc_header);
  icc->preferred_cmm_type = be_uint32(icc_header + 4);
  icc->profile_version_major = icc_header[8];
  icc->profile_version_minor_bugfix = icc_header[9];
  icc->profile_version_zero = be_uint16(icc_header + 8 + 2);
  icc->profile_device_class = be_uint32(icc_header + 12);
  icc->data_color_space = be_uint32(icc_header + 16);
  icc->pcs = be_uint32(icc_header + 20);
  icc->year = be_uint16(icc_header + 24);
  icc->month = be_uint16(icc_header + 26);
  icc->day = be_uint16(icc_header + 28);
  icc->hour = be_uint16(icc_header + 30);
  icc->minutes = be_uint16(icc_header + 32);
  icc->seconds = be_uint16(icc_header + 34);
  icc->profile_file_signature = be_uint32(icc_header + 36);
  icc->primary_platform = be_uint32(icc_header + 40);
  icc->profile_flags = be_uint32(icc_header + 44);
  icc->device_manufacturer = be_uint32(icc_header + 48);
  icc->device_model = be_uint32(icc_header + 52);
  icc->device_attributes = be_uint64(icc_header + 56);
  icc->rendering_intent = be_uint32(icc_header + 64);
  icc->pcs_illuminant_x = (int32_t)be_uint32(icc_header + 68);
  icc->pcs_illuminant_y = (int32_t)be_uint32(icc_header + 72);
  icc->pcs_illuminant_z = (int32_t)be_uint32(icc_header + 76);
  icc->profile_creator = be_uint32(icc_header + 80);

  // This is the MD5 of the entire profile, with profile flags, rendering
  // intent, and profile ID temporarily set to 0.
  memcpy(icc->profile_md5, icc_header + 84, 16);

  memcpy(icc->reserved, icc_header + 100, 28);
}

static void icc_dump_header(struct Options* options,
                            const struct ICCHeader* icc) {
  // https://www.color.org/specification/ICC.1-2022-05.pdf
  // 7.2 Profile header
  iprintf(options, "Profile size: %u\n", icc->profile_size);

  if (icc->preferred_cmm_type == 0)
    iprintf(options, "Preferred CMM type: (not set)\n");
  else {
    iprintf(options, "Preferred CMM type: '%.4s'\n",
            FOUR_CC_STR(icc->preferred_cmm_type));
  }

  uint8_t profile_version_minor = icc->profile_version_minor_bugfix >> 4;
  uint8_t profile_version_bugfix = icc->profile_version_minor_bugfix & 0xf;
  iprintf(options, "Profile version: %d.%d.%d.%d\n", icc->profile_version_major,
          profile_version_minor, profile_version_bugfix,
          icc->profile_version_zero);

  iprintf(options, "Profile/Device class: '%.4s'",
          FOUR_CC_STR(icc->profile_device_class));
  const char* profile_device_class_description =
      icc_profile_device_class_description(icc->profile_device_class);
  if (profile_device_class_description)
    printf(" (%s)", profile_device_class_description);
  printf("\n");

  iprintf(options, "Data color space: '%.4s'",
          FOUR_CC_STR(icc->data_color_space));
  const char* color_space_description =
      icc_color_space_description(icc->data_color_space);
  if (color_space_description)
    printf(" (%s)", color_space_description);
  printf("\n");

  iprintf(options, "Profile connection space (PCS): '%.4s'",
          FOUR_CC_STR(icc->pcs));
  const char* pcc_description = icc_color_space_description(icc->pcs);
  if (pcc_description)
    printf(" (%s)", pcc_description);
  printf("\n");
  if (icc->profile_device_class != 0x6C696E6B) {  // 'link'
    // "For all profile classes (see Table 18), other than a DeviceLink
    // profile, the PCS encoding shall be either PCSXYZ or PCSLAB"
    if (icc->pcs != 0x58595A20 /* 'XYZ '*/ &&
        icc->pcs != 0x4C616220 /* 'Lab '*/) {
      iprintf(options,
              "Invalid: Only PCSXYZ or PCSLAB valid for '%.4s' profile\n",
              FOUR_CC_STR(icc->profile_device_class));
    }
  }

  iprintf(options, "Profile created: %d-%02d-%02dT%02d:%02d:%02dZ\n", icc->year,
          icc->month, icc->day, icc->hour, icc->minutes, icc->seconds);

  iprintf(options, "Profile file signature: '%.4s'",
          FOUR_CC_STR(icc->profile_file_signature));
  if (icc->profile_file_signature != 0x61637370)
    printf(" (expected 'acsp', but got something else?");
  printf("\n");

  iprintf(options, "Primary platform: ");
  if (icc->primary_platform == 0)
    printf("none");
  else {
    printf("'%.4s'", FOUR_CC_STR(icc->primary_platform));
    const char* primary_platform_description =
        icc_platform_description(icc->primary_platform);
    if (primary_platform_description)
      printf(" (%s)", primary_platform_description);
  }
  printf("\n");

  iprintf(options, "Profile flags: 0x%08x\n", icc->profile_flags);

  iprintf(options, "Device manufacturer: ");
  if (icc->device_manufacturer == 0)
    printf("none");
  else
    printf("'%.4s'", FOUR_CC_STR(icc->device_manufacturer));
  printf("\n");

  iprintf(options, "Device model: ");
  if (icc->device_model == 0)
    printf("none");
  else
    printf("'%.4s'", FOUR_CC_STR(icc->device_model));
  printf("\n");

  iprintf(options, "Device attributes: 0x%016" PRIx64 "\n",
          icc->device_attributes);

  iprintf(options, "Rendering intent: %d", icc->rendering_intent);
  switch (icc->rendering_intent & 0xffff) {
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
  if (icc->rendering_intent >> 16 != 0)
    printf(" (top 16-bit unexpectedly not zero)");
  printf("\n");

  int32_t xyz_x = icc->pcs_illuminant_x;
  int32_t xyz_y = icc->pcs_illuminant_y;
  int32_t xyz_z = icc->pcs_illuminant_z;
  char xyz_buf[1024];
  snprintf(xyz_buf, sizeof(xyz_buf), "X = %.4f, Y = %.4f, Z = %.4f",
           icc_s15fixed16(xyz_x), icc_s15fixed16(xyz_y), icc_s15fixed16(xyz_z));
  iprintf(options, "PCS illuminant: %s", xyz_buf);
  const char expected_xyz[] = "X = 0.9642, Y = 1.0000, Z = 0.8249";
  if (strcmp(xyz_buf, expected_xyz) != 0)
    printf("(unexpected; expected %s)", expected_xyz);
  printf("\n");

  iprintf(options, "Profile creator: ");
  if (icc->profile_creator == 0)
    printf("none");
  else
    printf("'%.4s'", FOUR_CC_STR(icc->profile_creator));
  printf("\n");

  // This is the MD5 of the entire profile, with profile flags, rendering
  // intent, and profile ID temporarily set to 0.
  uint64_t profile_id_part_1 = be_uint64(icc->profile_md5);
  uint64_t profile_id_part_2 = be_uint64(icc->profile_md5 + 8);
  iprintf(options, "Profile ID: ");
  if (profile_id_part_1 == 0 && profile_id_part_2 == 0)
    printf("not computed");
  else
    printf("0x%016" PRIx64 "_%016" PRIx64, profile_id_part_1,
           profile_id_part_2);
  printf("\n");

  bool reserved_fields_are_zero = true;
  for (int i = 0; i < 28; ++i)
    if (icc->reserved[i] != 0)
      reserved_fields_are_zero = false;
  if (!reserved_fields_are_zero)
    iprintf(options, "reserved header bytes are unexpectedly not zero\n");
}

static void icc_dump_tag_table(struct Options* options,
                               const struct ICCHeader* icc,
                               const uint8_t* icc_header,
                               uint32_t size) {
  // https://www.color.org/specification/ICC.1-2022-05.pdf
  // 7.3 Tag table
  const uint8_t* tag_table = icc_header + 128;

  uint32_t tag_count = be_uint32(tag_table);
  iprintf(options, "%d tags\n", tag_count);

  // TODO: range checking for tag_count

  increase_indent(options);
  for (unsigned i = 0; i < tag_count; ++i) {
    uint32_t this_offset = 4 + 12 * i;
    uint32_t tag_signature = be_uint32(tag_table + this_offset);
    uint32_t offset_to_data = be_uint32(tag_table + this_offset + 4);
    uint32_t size_of_data = be_uint32(tag_table + this_offset + 8);
    iprintf(options, "signature 0x%08x ('%.4s') offset %d size %d\n",
            tag_signature, tag_table + this_offset, offset_to_data,
            size_of_data);

    // TODO: range checking for offset_to_data, size_of_data

    increase_indent(options);

    uint32_t type_signature = 0;
    if (size_of_data >= 4) {
      type_signature = be_uint32(icc_header + offset_to_data);
      iprintf(options, "type '%.4s'\n", icc_header + offset_to_data);
    }

    switch (tag_signature) {
      case 0x41324230:                       // 'A2B0', AToB0Tag
      case 0x41324231:                       // 'A2B1', AToB1Tag
      case 0x41324232:                       // 'A2B2', AToB2Tag
        if (type_signature == 0x6D667431) {  // 'mft1'
          icc_dump_lut8Type(options, icc, icc_header + offset_to_data,
                            size_of_data);
        } else if (type_signature == 0x6D667432) {  // 'mft2'
          icc_dump_lut16Type(options, icc, icc_header + offset_to_data,
                             size_of_data);
        } else if (type_signature == 0x6D414220) {  // 'mAB '
          icc_dump_lutAToBType(options, icc, icc_header + offset_to_data,
                               size_of_data);
        } else {
          iprintf(options,
                  "unexpected type, expected 'mtf1', 'mtf2', or 'mAB '\n");
        }
        break;
      case 0x42324130:                       // 'B2A0', BToA0Tag
      case 0x42324131:                       // 'B2A1', BToA1Tag
      case 0x42324132:                       // 'B2A2', BToA2Tag
        if (type_signature == 0x6D667431) {  // 'mft1'
          icc_dump_lut8Type(options, icc, icc_header + offset_to_data,
                            size_of_data);
        } else if (type_signature == 0x6D667432) {  // 'mft2'
          icc_dump_lut16Type(options, icc, icc_header + offset_to_data,
                             size_of_data);
        } else if (type_signature == 0x6D424120) {  // 'mBA '
          icc_dump_lutBToAType(options, icc, icc_header + offset_to_data,
                               size_of_data);
        } else {
          iprintf(options,
                  "unexpected type, expected 'mtf1', 'mtf2', or 'mBA '\n");
        }
        break;
      case 0x63707274:  // 'cprt', copyrightTag
        // Per ICC.1:2022 9.2.22, the type of copyrightTag must be
        // multiLocalizedUnicodeType. But Sony RAW files exported by Lightroom
        // give it type textType. (This was valid in older ICC versions, e.g. in
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
      case 0x6473636d:  // 'dscm', Apple extension profileDescMultiLocalized
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
          icc_dump_curveType(options, icc_header + offset_to_data, size_of_data,
                             /*want_exact_size_match=*/true);
        } else if (type_signature == 0x70617261) {  // 'para'
          icc_dump_parametricCurveType(options, icc_header + offset_to_data,
                                       size_of_data,
                                       /*want_exact_size_match=*/true);
        } else {
          iprintf(options, "unexpected type, expected 'curv' or 'para'\n");
        }
        break;
      case 0x63686164:  // 'chad', chromaticAdaptationTag
        icc_dumpS15Fixed16ArrayType(options, icc_header + offset_to_data,
                                    size_of_data, 3);
        break;
      case 0x63696370:  // 'cicp', cicpTag
        icc_dumpCicpType(options, icc_header + offset_to_data, size_of_data);
        break;
      case 0x6d656173:  // 'meas', measurementTag
        icc_dumpMeasurementType(options, icc_header + offset_to_data,
                                size_of_data);
        break;
      case 0x74656368:  // 'tech', technologyTag
        icc_dumpSignatureType(options, icc_header + offset_to_data,
                              size_of_data, icc_technology_description);
        break;
    }
    decrease_indent(options);
  }
  decrease_indent(options);
}

static void icc_dump(struct Options* options,
                     const uint8_t* icc_header,
                     size_t size) {
  if (options->dump_jpegs) {
    FILE* f = fopen("jpeg.icc", "wb");
    fwrite(icc_header, size, 1, f);
    fclose(f);
  }

  if (size < 128) {
    printf("ICC header must be at least 128 bytes, was %zu\n", size);
    return;
  }

  struct ICCHeader icc;
  icc_read_header(icc_header, size, &icc);
  icc_dump_header(options, &icc);

  if (size < icc.profile_size) {
    printf("ICC data needs %d bytes, but only %zu present\n", icc.profile_size,
           size);
    return;
  }

  icc_dump_tag_table(options, &icc, icc_header, icc.profile_size);
}

// IPTC dumping ///////////////////////////////////////////////////////////////
// IPTC IIM spec: https://www.iptc.org/std/IIM/4.1/specification/IIMV4.1.pdf

static const char* iptc_envelope_record_dataset_name(uint8_t dataset_number) {
  // https://www.iptc.org/std/IIM/4.1/specification/IIMV4.1.pdf
  // Chapter 5. ENVELOPE RECORD
  // clang-format off
  switch (dataset_number) {
    case  90: return "Coded Character Set";
    default: return NULL;
  }
  // clang-format on
}

static const char* iptc_application_record_dataset_name(
    uint8_t dataset_number) {
  // https://www.iptc.org/std/IIM/4.1/specification/IIMV4.1.pdf
  // Chapter 6. APPLICATION RECORD
  // clang-format off
  switch (dataset_number) {
    case   0: return "Record Version";
    case   4: return "Object Attribute Reference";
    case   5: return "Object Name";
    case  12: return "Subject Reference";
    case  25: return "Keywords";
    case  40: return "Special Instructions";
    case  55: return "Date Created";
    case  60: return "Time Created";
    case  62: return "Digital Creation Date";
    case  63: return "Digital Creation Time";
    case  80: return "By-line";
    case  85: return "By-line Title";
    case  90: return "City";
    case  92: return "Sublocation";
    case  95: return "Province/State";
    case 100: return "Country/Primary Location Code";
    case 101: return "Country/Primary Location Name";
    case 103: return "Original Transmission Reference";
    case 105: return "Headline";
    case 110: return "Credit";
    case 115: return "Source";
    case 116: return "Copyright Notice";
    case 120: return "Caption/Abstract";
    case 122: return "Writer/Editor";
    default: return NULL;
  }
  // clang-format on
}

static const char* iptc_dataset_name(uint8_t record_number,
                                     uint8_t dataset_number) {
  // TODO: record_numbers 3, 4, 5, 6, 7, 8, 9 (apparently not used in jpeg
  //       files at least, though?)
  if (record_number == 1)
    return iptc_envelope_record_dataset_name(dataset_number);
  if (record_number == 2)
    return iptc_application_record_dataset_name(dataset_number);
  return NULL;
}

static void iptc_dump_coded_character_set(struct Options* options,
                                          const uint8_t* begin,
                                          uint32_t size) {
  // https://www.iptc.org/std/IIM/4.1/specification/IIMV4.1.pdf
  // Chapter 5, 1:90 Coded Character Set
  // https://en.wikipedia.org/wiki/ISO/IEC_2022#Character_set_designations
  // https://en.wikipedia.org/wiki/ISO/IEC_2022#Interaction_with_other_coding_systems

  // 'ESC % G' is UTF-8 per the last link.
  if (size == 3 && begin[0] == 0x1b && begin[1] == 0x25 && begin[2] == 0x47)
    iprintf(options, "UTF-8\n");
  else
    iprintf(options, "TODO support for this encoding is not yet implemented\n");
}

static void iptc_dump_record_version(struct Options* options,
                                     const uint8_t* begin,
                                     uint32_t size) {
  // https://www.iptc.org/std/IIM/4.1/specification/IIMV4.1.pdf
  // Chapter 6, 2:00 Record Version
  if (size != 2) {
    printf("IPTC record version should be 2 bytes, was %d\n", size);
    return;
  }

  uint16_t version = be_uint16(begin);
  iprintf(options, "%d\n", version);
}

static void iptc_dump_text(struct Options* options,
                           const uint8_t* begin,
                           uint32_t size) {
  // https://www.iptc.org/std/IIM/4.1/specification/IIMV4.1.pdf
  // Chapter %, 1:90 Coded Character Set:
  // "If 1:90 is omitted, the default for records 2-6 and 8 is ISO 646 IRV (7
  // bits) or ISO 4873 DV (8 bits)"
  // 1:90 can specify complex encodings (see links in
  // iptc_dump_coded_character_set), but in practice 1:90 is either missing
  // or set to UTF-8. ISO 646 is basically ASCII.
  // So let's just dump this as ASCII for now.
  // TODO: Do this correctly at some point.
  iprintf(options, "'%.*s'\n", size, begin);
}

static void iptc_dump_date(struct Options* options,
                           const uint8_t* begin,
                           uint32_t size) {
  // https://www.iptc.org/std/IIM/4.1/specification/IIMV4.1.pdf
  // Chapter 6, 2:55 Date Created and 2:62 Digital Creation Date
  if (size != 8) {
    printf("IPTC date should be 8 bytes, was %d\n", size);
    return;
  }

  // "Represented in the form CCYYMMDD [...] follows ISO 8601 standard."
  // CCMM of 0000 means unknown year, MM of 00 means unknown month,
  // DD of 00 means unknown day.
  // TODO: Incorporate in output?
  iprintf(options, "%.4s-%.2s-%.2s\n", begin, begin + 4, begin + 6);
}

static void iptc_dump_time(struct Options* options,
                           const uint8_t* begin,
                           uint32_t size) {
  // https://www.iptc.org/std/IIM/4.1/specification/IIMV4.1.pdf
  // Chapter 6, 2:60 Time Created and 2:63 Digital Creation Time
  if (size != 11) {
    printf("IPTC time should be 11 bytes, was %d\n", size);
    return;
  }

  // "Represented in the form HHMMSS±HHMM [...] Follows ISO 8601 standard."
  // TODO: Maybe transcode hyphen-minus to U+2212 minus per ISO 8601
  iprintf(options, "%.2s:%.2s:%.2s%.1s%.2s:%.2s\n", begin, begin + 2, begin + 4,
          begin + 6, begin + 7, begin + 9);
}

static uint32_t iptc_dump_tag(struct Options* options,
                              const uint8_t* begin,
                              uint32_t size) {
  // https://www.iptc.org/std/IIM/4.1/specification/IIMV4.1.pdf
  if (size < 5) {
    printf("iptc tag should be at least 5 bytes, is %u\n", size);
    return size;
  }

  // Chapter 3, section 1.5 (b) The Standard DataSet Tag
  uint8_t tag_marker = begin[0];
  uint8_t record_number = begin[1];
  uint8_t dataset_number = begin[2];
  uint32_t data_field_size = be_uint16(begin + 3);

  // Chapter 3, section 1.5 (b) (ii):
  //     "Octet 1 is the tag marker that initiates the start of a DataSet and
  //     is always position 1/12"
  // Chapter 1, section 1.37 octet:
  //     "Character Definition by Chart Position:
  //     The bit combinations are identified by notations of the form xx/yy,
  //     where xx and yy are numbers in the range 00-15"
  // That is, tag_marker must be 0x1c for DataSets.
  if (tag_marker != 0x1c) {
    printf("iptc tag marker should be 0x1c, was 0x%x\n", tag_marker);
    return size;
  }

  unsigned header_size = 5;
  if (data_field_size > 32767) {
    // 1.5 (c) The Extended DataSet Tag
    uint16_t data_field_size_size = data_field_size & 0x7fff;

    if (data_field_size_size > 4) {
      printf("iptc tag with size field %d bytes, can handle at most 4\n",
             data_field_size_size);
      return size;
    }

    // TODO: untested
    data_field_size = 0;
    for (int i = 0; i < data_field_size_size; ++i)
      data_field_size = (data_field_size << 8) | begin[5 + i];

    header_size += data_field_size_size;
  }

  // TODO: size checking for data_field_size

  iprintf(options, "IPTC tag %d:%02d", record_number, dataset_number);
  const char* name = iptc_dataset_name(record_number, dataset_number);
  if (name)
    printf(" (%s)", name);
  printf(", %d bytes\n", data_field_size);

  const uint8_t* data_field = begin + header_size;
  increase_indent(options);
  if (record_number == 1) {
    if (dataset_number == 90)
      iptc_dump_coded_character_set(options, data_field, data_field_size);
  } else if (record_number == 2) {
    switch (dataset_number) {
      case 0:
        iptc_dump_record_version(options, data_field, data_field_size);
        break;
      case 5:
      case 12:  // TODO: custom dumper for 2:12
      case 25:
      case 40:
      case 80:
      case 85:
      case 90:
      case 92:
      case 95:
      case 100:  // TODO: custom dumper for 2:100
      case 101:
      case 103:
      case 105:
      case 110:
      case 115:
      case 116:
      case 120:
      case 122:
        iptc_dump_text(options, data_field, data_field_size);
        break;
      case 55:
      case 62:
        iptc_dump_date(options, data_field, data_field_size);
        break;
      case 60:
      case 63:
        iptc_dump_time(options, data_field, data_field_size);
        break;
    }
  }
  decrease_indent(options);

  return header_size + data_field_size;
}

static void iptc_dump(struct Options* options,
                      const uint8_t* begin,
                      uint32_t size) {
  uint32_t offset = 0;
  while (offset < size) {
    uint32_t tag_size = iptc_dump_tag(options, begin + offset, size - offset);

    if (tag_size > size - offset) {
      printf("tag size %d larger than remaining room %d\n", tag_size,
             size - offset);
      break;
    }

    offset += tag_size;
  }
}

// Photoshop Image Resource Section dumping //////////////////////////////////

static const char* photoshop_tag_name(uint16_t tag) {
  // https://www.adobe.com/devnet-apps/photoshop/fileformatashtml/#50577409_38034
  // clang-format off
  switch (tag) {
    case 0x03ed: return "ResolutionInfo";
    case 0x03f3: return "Print flags";
    case 0x03f5: return "Color halftoning information";
    case 0x03f8: return "Color transfer functions";
    case 0x0404: return "IPTC-NAA record";
    case 0x0406: return "JPEG quality";
    case 0x0408: return "Grid and guides information";
    case 0x040c: return "Thumbnail";
    case 0x040d: return "Global Angle";
    case 0x0414: return "Document-specific IDs seed number";
    case 0x0419: return "Global Altitude";
    case 0x041a: return "Slices";
    case 0x041e: return "URL List";
    case 0x0421: return "Version Info";
    case 0x0425: return "MD5 checksum";
    case 0x0426: return "Print scale";
    case 0x0428: return "Pixel Aspect Ratio";
    case 0x043a: return "Print Information";
    case 0x043b: return "Print Style";
    case 0x2710: return "Print flags information";
    default: return NULL;
  }
  // clang-format on
}

static void photoshop_dump_resolution_info(struct Options* options,
                                           const uint8_t* begin,
                                           uint32_t size) {
  const size_t header_size = 2 * sizeof(uint32_t) + 4 * sizeof(uint16_t);
  if (size != header_size) {
    printf("photoshop resolution info should be %zu bytes, is %u\n",
           header_size, size);
    return;
  }

  const char* direction[] = {"horizontal", "vertical"};
  const char* dimension[] = {"width", "height"};
  for (int i = 0; i < 2; ++i) {
    uint32_t res = be_uint32(begin + i * 8);
    uint16_t res_unit = be_uint16(begin + i * 8 + 4);
    uint16_t unit = be_uint16(begin + i * 8 + 6);

    iprintf(options, "%s resolution: %f ppi\n", direction[i],
            res / (double)0x10000);
    iprintf(options, "display unit %d", res_unit);
    if (res_unit == 1)
      printf(" (pixels per inch)");
    else if (res_unit == 2)
      printf(" (pixels per cm)");

    printf(", display %s unit %d", dimension[i], unit);
    if (res_unit == 1)
      printf(" (inches)");
    else if (res_unit == 2)
      printf(" (cm)");
    else if (res_unit == 3)
      printf(" (points)");
    else if (res_unit == 4)
      printf(" (picas)");
    else if (res_unit == 5)
      printf(" (columns)");
    printf("\n");
  }
}

static void photoshop_dump_thumbnail(struct Options* options,
                                     const uint8_t* begin,
                                     uint32_t size) {
  const size_t header_size = 6 * sizeof(uint32_t) + 2 * sizeof(uint16_t);
  if (size < header_size) {
    printf("photoshop thumbnail block should be at least %zu bytes, is %u\n",
           header_size, size);
    return;
  }

  uint32_t format = be_uint32(begin);
  uint32_t width = be_uint32(begin + 4);
  uint32_t height = be_uint32(begin + 8);
  uint32_t stride = be_uint32(begin + 12);
  uint32_t uncompressed_size = be_uint32(begin + 16);
  uint32_t compressed_size = be_uint32(begin + 20);
  uint16_t bits_per_pixel = be_uint16(begin + 24);
  uint16_t number_of_planes = be_uint16(begin + 26);

  iprintf(options, "Format %d", format);
  if (format == 0)
    printf(" (raw RGB)");
  else if (format == 1)
    printf(" (JPEG RGB)");
  printf("\n");
  iprintf(options, "%dx%d, %d bytes stride\n", width, height, stride);
  iprintf(options, "%d bytes uncompressed, %d bytes compressed\n",
          uncompressed_size, compressed_size);
  iprintf(options, "%d bpp, %d planes\n", bits_per_pixel, number_of_planes);

  if (format == 1) {
    if (compressed_size != size - header_size) {
      printf("expected %d bytes compressed size, got %zu\n", compressed_size,
             size - header_size);
      return;
    }

    increase_indent(options);
    jpeg_dump(options, begin + header_size, begin + size);
    decrease_indent(options);
  }
}

static void photoshop_dump_md5(struct Options* options,
                               const uint8_t* begin,
                               uint32_t size) {
  if (size != 16) {
    printf("photoshop md5 block should be 16 bytes, is %u\n", size);
    return;
  }

  iprintf(options, "checksum: ");
  for (int i = 0; i < 16; ++i) {
    if (i != 0 && i % 4 == 0)
      printf("-");
    printf("%02x", begin[i]);
  }
  printf("\n");
}

static uint32_t photoshop_dump_resource_block(struct Options* options,
                                              const uint8_t* begin,
                                              uint16_t size) {
  const size_t header_size = 12;
  if (size < header_size) {
    printf(
        "photoshop image resource block should be at least %zu bytes, is %u\n",
        header_size, size);
    return size;
  }

  if (strncmp((const char*)begin, "8BIM", 4) != 0) {
    printf(
        "photoshop image resource block doesn't start with '8BIM' but '%.4s' "
        "(0x%08x)\n",
        begin, be_uint32(begin));
    return size;
  }

  uint16_t image_resource_id = be_uint16(begin + 4);

  uint8_t name_len = begin[6];

  uint16_t size_offset = 7 + name_len;
  if (size_offset % 2 != 0)  // Pad to even size.
    ++size_offset;

  uint32_t resource_data_size = be_uint32(begin + size_offset);
  const uint8_t* resource_data = begin + size_offset + 4;

  uint32_t resource_block_size = size_offset + 4 + resource_data_size;
  if (resource_block_size % 2 != 0)  // Pad to even size.
    ++resource_block_size;

  iprintf(options, "Resource block 0x%04x", image_resource_id);
  const char* tag_name = photoshop_tag_name(image_resource_id);
  if (tag_name)
    printf(" (%s)", tag_name);
  printf(" '%.*s' size %d\n", name_len, begin + 7, resource_data_size);

  increase_indent(options);
  switch (image_resource_id) {
    case 0x03ed:
      photoshop_dump_resolution_info(options, resource_data,
                                     resource_data_size);
      break;
    case 0x0404:
      iptc_dump(options, resource_data, resource_data_size);
      break;
    case 0x040c:
      photoshop_dump_thumbnail(options, resource_data, resource_data_size);
      break;
    case 0x0425:
      photoshop_dump_md5(options, resource_data, resource_data_size);
      break;
  }
  decrease_indent(options);

  return resource_block_size;
}

static void photoshop_dump_resource_blocks(struct Options* options,
                                           const uint8_t* begin,
                                           uint16_t size) {
  // https://oldschoolprg.x10.mx/downloads/ps6ffspecsv2.pdf
  // https://www.adobe.com/devnet-apps/photoshop/fileformatashtml/#50577409_pgfId-1037685
  // https://metacpan.org/pod/Image::MetaData::JPEG::Structures#Structure-of-a-Photoshop-style-APP13-segment
  uint16_t offset = 0;
  while (offset < size) {
    uint32_t block_size =
        photoshop_dump_resource_block(options, begin + offset, size - offset);

    if (block_size > size - offset) {
      printf("block size %d larger than remaining room %d\n", block_size,
             size - offset);
      break;
    }

    offset += block_size;
  }
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

struct JpegIccChunks {
  uint8_t num_chunks;
  uint8_t num_completed_chunks;
  const uint8_t** datas;
  uint16_t* sizes;
};

static bool jpeg_icc_ensure_multi_chunk_state(struct Options* options,
                                              uint8_t num_chunks) {
  if (options->jpeg_icc_chunks) {
    if (options->jpeg_icc_chunks->num_chunks == num_chunks)
      return true;
    // FIXME: dealloc?
    printf("num_chunks %u doesn't match previous num_chunks %u\n", num_chunks,
           options->jpeg_icc_chunks->num_chunks);
    return false;
  }

  // FIXME: Need to free jpeg_icc_chunks and its arrays somewhere.
  // But they're tiny and this is a one-shot program anyways, so fine for now.
  struct JpegIccChunks* jpeg_icc_chunks = malloc(sizeof *jpeg_icc_chunks);
  jpeg_icc_chunks->num_chunks = num_chunks;
  jpeg_icc_chunks->num_completed_chunks = 0;
  jpeg_icc_chunks->datas = calloc(num_chunks, sizeof *jpeg_icc_chunks->datas);
  jpeg_icc_chunks->sizes = calloc(num_chunks, sizeof *jpeg_icc_chunks->sizes);
  options->jpeg_icc_chunks = jpeg_icc_chunks;
  return true;
}

static void jpeg_icc_add_chunk(struct Options* options,
                               uint8_t sequence_number,
                               const uint8_t* data,
                               uint16_t size) {
  if (sequence_number == 0) {
    printf("sequence-numbers are 1-based, but got 0\n");
    return;
  }
  uint8_t index = sequence_number - 1;

  assert(options->jpeg_icc_chunks);
  struct JpegIccChunks* jpeg_icc_chunks = options->jpeg_icc_chunks;

  if (index >= jpeg_icc_chunks->num_chunks) {
    printf("got sequence number %u but only have %u chunks\n", sequence_number,
           jpeg_icc_chunks->num_chunks);
    return;
  }

  if (jpeg_icc_chunks->datas[index]) {
    printf("duplicate entry for sequence number %u\n", sequence_number);
    return;
  }

  assert(data);
  jpeg_icc_chunks->datas[index] = data;
  jpeg_icc_chunks->sizes[index] = size;
  ++jpeg_icc_chunks->num_completed_chunks;
}

static bool jpeg_icc_have_all_chunks(struct Options* options) {
  assert(options->jpeg_icc_chunks);
  struct JpegIccChunks* jpeg_icc_chunks = options->jpeg_icc_chunks;
  return jpeg_icc_chunks->num_chunks == jpeg_icc_chunks->num_completed_chunks;
}

static void jpeg_icc_dump(struct Options* options) {
  assert(options->jpeg_icc_chunks);
  struct JpegIccChunks* jpeg_icc_chunks = options->jpeg_icc_chunks;

  uint32_t total_size = 0;
  for (uint8_t i = 0; i < jpeg_icc_chunks->num_chunks; ++i)
    total_size += jpeg_icc_chunks->sizes[i];

  uint8_t* icc_data = malloc(total_size);

  uint32_t offset = 0;
  for (uint8_t i = 0; i < jpeg_icc_chunks->num_chunks; ++i) {
    memcpy(icc_data + offset, jpeg_icc_chunks->datas[i],
           jpeg_icc_chunks->sizes[i]);
    offset += jpeg_icc_chunks->sizes[i];
  }

  icc_dump(options, icc_data, total_size);

  free(icc_data);
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

  if (chunk_sequence_number == 1 && num_chunks == 1) {
    icc_dump(options, begin + header_size, size - header_size);
    return;
  }

  if (!jpeg_icc_ensure_multi_chunk_state(options, num_chunks))
    return;

  jpeg_icc_add_chunk(options, chunk_sequence_number, begin + header_size,
                     size - header_size);

  if (jpeg_icc_have_all_chunks(options))
    jpeg_icc_dump(options);
}

static void jpeg_dump_mpf(struct Options* options,
                          const uint8_t* begin,
                          uint16_t size) {
  // https://web.archive.org/web/20190713230858/http://www.cipa.jp/std/documents/e/DC-007_E.pdf
  tiff_dump(options, begin + sizeof(uint16_t) + sizeof("MPF"), begin + size);
}

static int is_line_with_just_spaces(int i, const uint8_t* s, int n) {
  int num_spaces = 0;
  while (i < n) {
    if (s[i] == '\n')
      return num_spaces;
    if (s[i] != ' ')
      return 0;
    ++num_spaces;
    ++i;
  }
  return 0;
}

static int num_lines_with_just_spaces(int* i, const uint8_t* s, int n) {
  // XMPSpecificationPart3.pdf
  // "It is recommended that applications place 2 KB to 4 KB of padding within
  // the packet. This allows the XMP to be edited in place, and expanded if
  // necessary, without overwriting existing application data. The padding must
  // be XML-compatible whitespace; the recommended practice is to use the ASCII
  // space character (U+0020) in the appropriate encoding, with a newline about
  // every 100 characters."
  // JPEGs written by Lightroom Classic and by Sony cameras do this.
  // Filter out the padding, to not print hundreds of lines with nothing but
  // spaces.
  int num_lines = 0;
  int num_spaces_on_current_line = 0;
  while ((num_spaces_on_current_line = is_line_with_just_spaces(*i, s, n))) {
    ++num_lines;
    *i += num_spaces_on_current_line + 1;
  }
  return num_lines;
}

static void indent_and_elide_each_line(struct Options* options,
                                       const uint8_t* s,
                                       int n) {
  const int max_column = 80;
  print_indent(options);
  int column = options->current_indent;
  bool is_at_start_of_line = true;
  for (int i = 0; i < n; ++i) {
    if (is_at_start_of_line) {
      int num_skipped_lines = num_lines_with_just_spaces(&i, s, n);
      if (num_skipped_lines) {
        printf("...skipped %d space-only lines...", num_skipped_lines);
        // Reset to last newline, so that newline printing on next iteration
        // resets state and prints indent.
        --i;
      }
    }

    putchar(s[i]);
    ++column;

    if (s[i] == '\n') {
      if (i + 1 < n)
        print_indent(options);
      column = options->current_indent;
      is_at_start_of_line = true;
    } else
      is_at_start_of_line = false;

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

static void print_elided(struct Options* options, const uint8_t* s, int n) {
  indent_and_elide_each_line(options, s, n);
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
  print_elided(options, begin + header_size, size - header_size);
}

static void jpeg_dump_xmp_extension(struct Options* options,
                                    const uint8_t* begin,
                                    uint16_t size) {
  // See section 1.1.3.1 in:
  // https://github.com/adobe/xmp-docs/blob/master/XMPSpecifications/XMPSpecificationPart3.pdf
  const int guid_size = 32;
  const size_t prefix_size =
      sizeof(uint16_t) + sizeof("http://ns.adobe.com/xmp/extension/");
  const size_t header_size = prefix_size + guid_size + 2 * sizeof(uint32_t);
  if (size < header_size) {
    printf("xmp header should be at least %zu bytes, is %u\n", header_size,
           size);
    return;
  }
  iprintf(options, "guid %.*s\n", guid_size, begin + prefix_size);
  uint32_t total_data_size = be_uint32(begin + prefix_size + guid_size);
  uint32_t data_offset =
      be_uint32(begin + prefix_size + guid_size + sizeof(uint32_t));
  uint16_t data_size = size - header_size;
  iprintf(options, "offset %u\n", data_offset);
  iprintf(options, "total size %u\n", total_data_size);
  print_elided(options, begin + header_size, data_size);
}

static void jpeg_dump_photoshop_3(struct Options* options,
                                  const uint8_t* begin,
                                  uint16_t size) {
  const size_t prefix_size = sizeof(uint16_t) + sizeof("Photoshop 3.0");
  if (size < prefix_size) {
    printf("Photoshop 3.0 header should be at least %zu bytes, is %u\n",
           prefix_size, size);
    return;
  }
  photoshop_dump_resource_blocks(options, begin + prefix_size,
                                 size - prefix_size);
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

  const uint8_t* cur_start = NULL;

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
        cur_start = cur - 2;
        break;
      case 0xd9:
        // If there's an App2/"MPF" ("Multi-Picture Format") marker,
        // it'll point to additional images past the first image's EOI.
        // Some versions of the Pixel Pro camera app write unknown-to-me
        // non-jpeg trailing data after the EOI marker.
        // TODO: In non-scan mode, this should terminate the reading loop.
        printf(": End Of Image (EOI)\n");

        if (options->dump_jpegs) {
          char buf[80];
          sprintf(buf, "jpeg-%d.jpg", options->num_jpegs);
          FILE* f = fopen(buf, "wb");
          fwrite(cur_start, (size_t)(cur - cur_start), 1, f);
          fclose(f);
        }
        ++options->num_jpegs;

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
      case 0xe0:
      case 0xe1:
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
        if (b1 == 0xe0) {  // APP0
          if (strcmp(app_id, "JFIF") == 0)
            jpeg_dump_jfif(options, cur, size);
        } else if (b1 == 0xe1) {  // APP1
          if (strcmp(app_id, "Exif") == 0)
            jpeg_dump_exif(options, cur, size);
          else if (strcmp(app_id, "http://ns.adobe.com/xap/1.0/") == 0)
            jpeg_dump_xmp(options, cur, size);
          else if (strcmp(app_id, "http://ns.adobe.com/xmp/extension/") == 0)
            jpeg_dump_xmp_extension(options, cur, size);
        } else if (b1 == 0xe2) {  // APP2
          if (strcmp(app_id, "ICC_PROFILE") == 0)
            jpeg_dump_icc(options, cur, size);
          else if (strcmp(app_id, "MPF") == 0)
            jpeg_dump_mpf(options, cur, size);
        } else if (b1 == 0xed) {  // APP13
          if (strcmp(app_id, "Photoshop 3.0") == 0)
            jpeg_dump_photoshop_3(options, cur, size);
        }
        decrease_indent(options);
        break;
      }
      case 0xfe:
        printf(": Comment (COM)\n");
        assert(has_size);
        if (size > 2) {
          increase_indent(options);
          print_elided(options, cur + 2, size - 2);
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
      .dump_jpegs = false,
      .num_jpegs = 0,
      .dump_luts = false,
      .jpeg_icc_chunks = NULL,
  };
#define kDumpLuts 512
  struct option getopt_options[] = {
      {"dump", no_argument, NULL, 'd'},
      {"dump-luts", no_argument, NULL, kDumpLuts},
      {"help", no_argument, NULL, 'h'},
      {"scan", no_argument, NULL, 's'},
      {0, 0, 0, 0},
  };
  int opt;
  while ((opt = getopt_long(argc, argv, "dhs", getopt_options, NULL)) != -1) {
    switch (opt) {
      case 'd':
        options.dump_jpegs = true;
        break;
      case 'h':
        print_usage(stdout, program_name);
        return 0;
      case 's':
        options.jpeg_scan = true;
        break;
      case '?':
        print_usage(stderr, program_name);
        return 1;
      case kDumpLuts:
        options.dump_luts = true;
        break;
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

  uint8_t* contents = mmap(
      /*addr=*/0, (size_t)in_stat.st_size, PROT_READ, MAP_SHARED, in_file,
      /*offset=*/0);
  if (contents == MAP_FAILED)
    fatal("Failed to mmap: %d (%s)\n", errno, strerror(errno));

  enum { Jpeg, ICC } file_type = Jpeg;
  if (!options.jpeg_scan) {
    if (in_stat.st_size >= 128 + 4 &&
        strncmp((char*)contents + 36, "acsp", 4) == 0)
      file_type = ICC;
  }

  if (file_type == ICC)
    icc_dump(&options, contents, (size_t)in_stat.st_size);
  else if (file_type == Jpeg)
    jpeg_dump(&options, contents, contents + in_stat.st_size);

  munmap(contents, (size_t)in_stat.st_size);
  close(in_file);
}
