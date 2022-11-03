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

struct Options {
  int current_indent;
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

// HEIC is built on top of the "ISOBMFF ISO Base Media File Format", an old
// version of it is at ISO_IEC_14496-12_2015.pdf
// e.g. here https://b.goeswhere.com/ISO_IEC_14496-12_2015.pdf
// The text seems to not generally be freely available.

// There's not super great authoritative documentation as far as I can tell.

// There are a few ok documents online.

// Readable master thesis on the format:
// https://trepo.tuni.fi/bitstream/handle/123456789/24147/heikkila.pdf

// Blog post on the format.
// http://cheeky4n6monkey.blogspot.com/2017/10/monkey-takes-heic.html

// Relatively useless Library Of Congress page:
// https://www.loc.gov/preservation/digital/formats/fdd/fdd000079.shtml

// There are a bunch of .doc (!) files in zips floating around, e.g:
// https://mpeg.chiariglione.org/standards/mpeg-h/image-file-format/text-isoiec-cd-23008-12-image-file-format
// => w14148.zip => W14148-HEVC-still-WD.doc

static void heif_dump_box_container(struct Options* options,
                                    const uint8_t* begin,
                                    const uint8_t* end);
static uint64_t heif_dump_box(struct Options* options,
                              const uint8_t* begin,
                              const uint8_t* end);

static const char* heif_box_name(uint32_t type) {
  switch (type) {
    case 0x68646c72:  // 'hdlr'
      return "handler";
    //case 0x64726566:  // 'dref'
    case 0x66747970:  // 'ftyp'
      return "file type";
    case 0x69696e66:  // 'iinf'
      return "item info";
    //case 0x69726566:  // 'iref'
    case 0x69737065:  // 'ispe'
      return "image spacial extent";
    //case 0x6d657461:  // 'meta'
    //case 0x64696e66:  // 'dinf'
    case 0x6970636f:  // 'ipco'
      return "item property container";
    //case 0x69707270:  // 'iprp'
    case 0x7069746d:  // 'pitm'
      return "primary item number";
    default: return NULL;
  }
}

static void heif_dump_box_dref(struct Options* options,
                               const uint8_t* begin,
                               uint64_t size) {
  // ISO_IEC_14496-12_2015.pdf, 8.7.2 Data Reference Box
  if (size < 8) {
    fprintf(stderr, "dref not at least 8 bytes, was %" PRIu64 "\n", size);
    return;
  }

  uint32_t version_and_flags = be_uint32(begin);
  uint8_t version = version_and_flags >> 24;
  uint32_t flags = version_and_flags & 0xffffff;
  iprintf(options, "version %u, flags %u\n", version, flags);

  uint32_t num_subboxes = be_uint32(begin + 4);
  iprintf(options, "num subboxes %d\n", num_subboxes);

  // TODO: Why does 'dref' store num_subboxes, unlike all other containers?
  // TODO: Check num_subboxes vs num boxes printed by size.
  heif_dump_box_container(options, begin + 8, begin + size);
}

static void heif_dump_box_ftyp(struct Options* options,
                               const uint8_t* begin,
                               uint64_t size) {
  // ISO_IEC_14496-12_2015.pdf, 4.3 File Type Box
  if (size < 8) {
    fprintf(stderr, "ftyp not at least 8 bytes, was %" PRIu64 "\n", size);
    return;
  }
  if (size % 4 != 0) {
    fprintf(stderr, "ftyp size not multiple of 4, was %" PRIu64 "\n", size);
    return;
  }

  uint32_t major_brand = be_uint32(begin);
  uint32_t minor_version = be_uint32(begin + 4);

  iprintf(options, "major brand '%.4s' (0x%x), minor version %u\n", begin,
          major_brand, minor_version);

  iprintf(options, "minor brands:\n");
  for (uint64_t i = 8; i < size; i += 4) {
    uint32_t minor_brand = be_uint32(begin + i);
    iprintf(options, " '%.4s' (0x%x)\n", begin + i, minor_brand);
  }
}

static void heif_dump_box_iinf(struct Options* options,
                               const uint8_t* begin,
                               uint64_t size) {
  // ISO_IEC_14496-12_2015.pdf, 8.11.6 Item Information Box
  if (size < 8) {
    fprintf(stderr, "dref not at least 8 bytes, was %" PRIu64 "\n", size);
    return;
  }

  uint32_t version_and_flags = be_uint32(begin);
  uint8_t version = version_and_flags >> 24;
  uint32_t flags = version_and_flags & 0xffffff;
  iprintf(options, "version %u, flags %u\n", version, flags);

  uint32_t num_subboxes;
  int offset;
  if (version == 0) {
    num_subboxes = be_uint16(begin + 4);
    offset = 6;
  } else {
    num_subboxes = be_uint32(begin + 4);
    offset = 8;
  }
  iprintf(options, "num subboxes %d\n", num_subboxes);

  // TODO: Why does 'iinf' store num_subboxes, unlike all other containers?
  // TODO: Check num_subboxes vs num boxes printed by size.
  heif_dump_box_container(options, begin + offset, begin + size);
}

static void heif_dump_full_box_container(struct Options* options,
                                         const uint8_t* begin,
                                         uint64_t size) {
  // ISO_IEC_14496-12_2015.pdf, 4.2 Object Structure, class FullBox
  if (size < 4) {
    fprintf(stderr, "meta not at least 4 bytes, was %" PRIu64 "\n", size);
    return;
  }

  uint32_t version_and_flags = be_uint32(begin);
  uint8_t version = version_and_flags >> 24;
  uint32_t flags = version_and_flags & 0xffffff;
  iprintf(options, "version %u, flags %u\n", version, flags);

  int i = 0;
  uint64_t offset = 4;
  while (offset < size) {
    uint64_t box_size = heif_dump_box(options, begin + offset, begin + size);

    if (box_size > size - offset) {
      printf("box has size %" PRIu64 " but only %" PRIu64 " bytes left\n",
             box_size, size - offset);
      return;
    }

    offset += box_size;
    i++;
  }
}

static uint64_t heif_dump_box(struct Options* options,
                              const uint8_t* begin,
                              const uint8_t* end) {
  const size_t size = (size_t)(end - begin);
  if (size < 8)
    fatal("heif box must be at least 8 bytes but is %zu\n", size);

  uint64_t length = be_uint32(begin);
  uint32_t type = be_uint32(begin + 4);
  const uint8_t* data_begin = begin + 8;
  uint64_t data_length = length - 8;

  if (length == 1) {
    // length == 1: 64-bit length is stored after the type.
    if (size < 16)
      fatal("heif box wth extended size must be at least 16 bytes but is %zu\n",
            size);

    length = be_uint64(begin + 8);

    data_begin = begin + 16;
    data_length = length - 16;
  } else if (length == 0) {
    // length == 0: Box extends to end of file.
    length = size;
  }

  if (size < length)
    fatal("heif box '%.4s' with size %" PRIu64 " must be at least %" PRIu64
          " bytes but is %zu\n",
          begin + 4, length, length, size);

  iprintf(options, "box '%.4s' (%08x", begin + 4, type);
  const char* box_name = heif_box_name(type);
  if (box_name)
    printf(", %s", box_name);
  printf("), length %" PRIu64 "\n", length);

  increase_indent(options);
  switch (type) {
    case 0x64726566:  // 'dref'
      heif_dump_box_dref(options, data_begin, data_length);
      break;
    case 0x66747970:  // 'ftyp'
      heif_dump_box_ftyp(options, data_begin, data_length);
      break;
    case 0x69696e66:  // 'iinf'
      heif_dump_box_iinf(options, data_begin, data_length);
      break;
    case 0x69726566:  // 'iref'
    case 0x6d657461:  // 'meta'
      heif_dump_full_box_container(options, data_begin, data_length);
      break;
    case 0x64696e66:  // 'dinf'
    case 0x6970636f:  // 'ipco'
    case 0x69707270:  // 'iprp'
      heif_dump_box_container(options, data_begin, data_begin + data_length);
      break;
    // TODO: infe, iloc, ipma, ...
  }
  decrease_indent(options);

  return length;
}

static void heif_dump_box_container(struct Options* options,
                                    const uint8_t* begin,
                                    const uint8_t* end) {
  // ISO_IEC_14496-12_2015.pdf
  // e.g. https://b.goeswhere.com/ISO_IEC_14496-12_2015.pdf
  while (begin < end) {
    uint64_t box_size = heif_dump_box(options, begin, end);

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

  struct Options options = {
      .current_indent = 0,
  };
  heif_dump_box_container(&options, contents,
                          (uint8_t*)contents + in_stat.st_size);

  munmap(contents, (size_t)in_stat.st_size);
  close(in_file);
}
