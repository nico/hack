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
  // https://nokiatech.github.io/heif/technical.html
  switch (type) {
    case 0x63647363:  // 'cdsc'
      return "content description";
    case 0x64696d67:  // 'dimg'
      return "derived image inputs";
    case 0x64696e66:  // 'dinf'
      return "data information";
    case 0x64726566:  // 'dref'
      return "data reference";
    case 0x68646c72:  // 'hdlr'
      return "handler";
    case 0x69726f74:  // 'irot'
      return "image rotation";
    case 0x66747970:  // 'ftyp'
      return "file type";
    case 0x69646174:  // 'idat'
      return "item data";
    case 0x69696e66:  // 'iinf'
      return "item info";
    case 0x696c6f63:  // 'iloc'
      return "item location";
    case 0x696e6665:  // 'infe'
      return "item info entry";
    case 0x69726566:  // 'iref'
      return "item type reference";
    case 0x69737065:  // 'ispe'
      return "image spacial extent";
    case 0x6970636f:  // 'ipco'
      return "item property container";
    case 0x69706d61:  // 'ipma'
      return "item property association";
    case 0x69707270:  // 'iprp'
      return "item properties";
    case 0x6d657461:  // 'meta'
      return "metadata";
    case 0x7069746d:  // 'pitm'
      return "primary item number";
    case 0x70697869:  // 'pixi'
      return "pixel information";
    default:
      return NULL;
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
    fprintf(stderr, "iinf not at least 8 bytes, was %" PRIu64 "\n", size);
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

static void heif_dump_box_iloc(struct Options* options,
                               const uint8_t* begin,
                               uint64_t size) {
  // ISO_IEC_14496-12_2015.pdf, 8.11.3 The Item Location Box
  if (size < 6) {
    fprintf(stderr, "iloc not at least ^ bytes, was %" PRIu64 "\n", size);
    return;
  }

  uint32_t version_and_flags = be_uint32(begin);
  uint8_t version = version_and_flags >> 24;
  uint32_t flags = version_and_flags & 0xffffff;
  iprintf(options, "version %u, flags %u\n", version, flags);

  uint16_t sizes = be_uint16(begin + 4);
  unsigned offset_size = sizes >> 12;
  unsigned length_size = (sizes >> 8) & 0xf;
  unsigned base_offset_size = (sizes >> 4) & 0xf;
  unsigned index_size = 0;
  if (version == 1 || version == 2)
    index_size = sizes & 0xf;

  if (offset_size != 0 && offset_size != 4 && offset_size != 8) {
    printf("offset_size should be 0, 4, 8 but was %d\n", offset_size);
    return;
  }
  if (length_size != 0 && length_size != 4 && length_size != 8) {
    printf("length_size should be 0, 4, 8 but was %d\n", length_size);
    return;
  }
  if (base_offset_size != 0 && base_offset_size != 4 && base_offset_size != 8) {
    printf("base_offset_size should be 0, 4, 8 but was %d\n", base_offset_size);
    return;
  }
  if (index_size != 0 && index_size != 4 && index_size != 8) {
    printf("index_size should be 0, 4, 8 but was %d\n", index_size);
    return;
  }

  // FIXME: bounds checking

  unsigned offset;
  uint32_t item_count;
  if (version < 2) {
    item_count = be_uint16(begin + 6);
    offset = 8;
  } else {
    item_count = be_uint32(begin + 6);
    offset = 10;
  }

  iprintf(options, "offset_size %d\n", offset_size);
  iprintf(options, "offset_size %d\n", length_size);
  iprintf(options, "base_offset_size %d\n", base_offset_size);
  if (version == 1 || version == 2)
    iprintf(options, "index_size %d\n", index_size);

  increase_indent(options);
  for (unsigned i = 0; i < item_count; ++i) {
    uint32_t item_id;
    if (version < 2) {
      item_id = be_uint16(begin + offset);
      offset += 2;
    } else {
      item_id = be_uint32(begin + offset);
      offset += 4;
    }

    uint8_t construction_method = 0;
    if (version == 1 || version == 2) {
      uint16_t reserved = be_uint16(begin + offset);
      offset += 2;

      construction_method = reserved & 0xf;
    }

    uint16_t data_reference_index = be_uint16(begin + offset);
    offset += 2;

    offset += base_offset_size;  // TODO: read base_offset

    uint16_t extent_count = be_uint16(begin + offset);
    offset += 2;

    for (int j = 0; j < extent_count; ++j) {
      if ((version == 1 || version == 2) && index_size > 0) {
        offset += index_size;  // TODO: read extent_index
      }
      offset += offset_size;  // TODO: read extent_offset
      offset += length_size;  // TODO: read extent_length
    }

    iprintf(options, "item_id: %d\n", item_id);
    iprintf(options, "construction_method: %d\n", construction_method);
    iprintf(options, "data_reference_index: %d\n", data_reference_index);
  }
  decrease_indent(options);
}

static void heif_dump_box_infe(struct Options* options,
                               const uint8_t* begin,
                               uint64_t size) {
  // ISO_IEC_14496-12_2015.pdf, 8.11.6 Item Information Box
  if (size < 8) {
    fprintf(stderr, "infe not at least 8 bytes, was %" PRIu64 "\n", size);
    return;
  }

  // FIXME: bounds checking

  uint32_t version_and_flags = be_uint32(begin);
  uint8_t version = version_and_flags >> 24;
  uint32_t flags = version_and_flags & 0xffffff;
  iprintf(options, "version %u, flags %u\n", version, flags);

  if (version == 0 || version == 1) {
    uint16_t item_id = be_uint16(begin + 4);
    uint16_t item_protection_index = be_uint16(begin + 6);

    unsigned offset = 8;

    const char* item_name = (const char*)(begin + offset);
    offset += strlen(item_name) + 1;

    const char* content_type = (const char*)(begin + offset);
    offset += strlen(content_type) + 1;

    const char* content_encoding = NULL;
    if (offset < size) {
      content_encoding = (const char*)(begin + offset);
      offset += strlen(content_encoding) + 1;
    }

    iprintf(options, "item_id %u\n", item_id);
    iprintf(options, "item_protection_index %u\n", item_protection_index);
    iprintf(options, "item_name '%s'\n", item_name);
    iprintf(options, "content_type '%s'\n", content_type);
    if (content_encoding)
      iprintf(options, "content_encoding '%s'\n", content_encoding);
  }
  if (version == 1) {
    // TODO: extension_type, extension
  }
  if (version >= 2) {
    unsigned offset;
    uint32_t item_id;
    if (version == 2) {
      item_id = be_uint16(begin + 4);
      offset = 6;
    } else {
      item_id = be_uint32(begin + 4);
      offset = 8;
    }
    uint16_t item_protection_index = be_uint16(begin + offset);
    offset += 2;

    unsigned item_type_offset = offset;
    uint32_t item_type = be_uint32(begin + item_type_offset);
    offset += 4;

    const char* item_name = (const char*)(begin + offset);
    offset += strlen(item_name) + 1;

    iprintf(options, "item_id %u\n", item_id);
    iprintf(options, "item_protection_index %u\n", item_protection_index);
    iprintf(options, "item_type '%.4s' (0x%x)\n", begin + item_type_offset,
            item_type);
    iprintf(options, "item_name '%s'\n", item_name);

    if (item_type == 0x6d696d65) {  // 'mime'
      const char* content_type = (const char*)(begin + offset);
      offset += strlen(content_type) + 1;

      const char* content_encoding = NULL;
      if (offset < size) {
        content_encoding = (const char*)(begin + offset);
        offset += strlen(content_encoding) + 1;
      }

      iprintf(options, "content_type '%s'\n", content_type);
      if (content_encoding)
        iprintf(options, "content_encoding '%s'\n", content_encoding);
    } else if (item_type == 0x75726920) {  // 'uri '
      const char* item_uri_type = (const char*)(begin + offset);
      offset += strlen(item_name) + 1;
      iprintf(options, "item_uri_type '%s'\n", item_uri_type);
    }
  }
}

struct Box {
  uint64_t length;
  uint32_t type;
  const uint8_t* data_begin;
  uint64_t data_length;
};

static struct Box heif_read_box(const uint8_t* begin, uint64_t size) {
  if (size < 8)
    fatal("heif box must be at least 8 bytes but is %" PRIu64 "\n", size);

  uint64_t length = be_uint32(begin);
  uint32_t type = be_uint32(begin + 4);
  const uint8_t* data_begin = begin + 8;
  uint64_t data_length = length - 8;

  if (length == 1) {
    // length == 1: 64-bit length is stored after the type.
    if (size < 16)
      fatal(
          "heif box wth extended size must be at least 16 bytes but is %" PRIu64
          " \n",
          size);

    length = be_uint64(begin + 8);

    data_begin = begin + 16;
    data_length = length - 16;
  } else if (length == 0) {
    // length == 0: Box extends to end of file.
    length = size;
    data_length = length - 8;
  }

  if (size < length)
    fatal("heif box '%.4s' with size %" PRIu64 " must be at least %" PRIu64
          " bytes but is %" PRIu64 "\n",
          begin + 4, length, length, size);

  return (struct Box){
      .length = length,
      .type = type,
      .data_begin = data_begin,
      .data_length = data_length,
  };
}

static void print_box(struct Options* options,
                      const struct Box* box,
                      const uint8_t* type_str) {
  iprintf(options, "box '%.4s' (%08x", type_str, box->type);
  const char* box_name = heif_box_name(box->type);
  if (box_name)
    printf(", %s", box_name);
  printf("), length %" PRIu64 "\n", box->length);
}

static uint64_t heif_dump_box_item_reference(struct Options* options,
                                             const uint8_t* begin,
                                             uint64_t size) {
  // ISO_IEC_14496-12_2015.pdf, 8.11.12 Item Reference Box
  struct Box box = heif_read_box(begin, size);
  print_box(options, &box, begin + 4);

  if (box.data_length < 4 != 0) {
    fprintf(stderr, "iref size not at least 4, was %" PRIu64 "\n",
            box.data_length);
    return box.length;
  }
  if (box.data_length % 2 != 0) {
    fprintf(stderr, "iref size not multiple of 2, was %" PRIu64 "\n",
            box.data_length);
    return box.length;
  }

  uint16_t from = be_uint16(box.data_begin);
  uint16_t count = be_uint16(box.data_begin + 2);
  if (box.data_length != 4 + count * 2) {
    fprintf(stderr, "iref size not %u, was %" PRIu64 "\n", 4 + count * 2,
            box.data_length);
    return box.length;
  }

  iprintf(options, "  ref from %d to:", from);
  for (unsigned i = 0; i < count; ++i)
    printf(" %d", be_uint16(box.data_begin + 4 + 2 * i));
  printf("\n");

  return box.length;
}

static uint64_t heif_dump_box_item_reference_large(struct Options* options,
                                                   const uint8_t* begin,
                                                   uint64_t size) {
  // ISO_IEC_14496-12_2015.pdf, 8.11.12 Item Reference Box
  // ISO_IEC_14496-12_2015.pdf, 8.11.12 Item Reference Box
  struct Box box = heif_read_box(begin, size);
  print_box(options, &box, begin + 4);

  if (box.data_length < 6 != 0) {
    fprintf(stderr, "iref size not at least 4, was %" PRIu64 "\n",
            box.data_length);
    return box.length;
  }
  if ((box.data_length - 2) % 4 != 0) {
    fprintf(stderr, "iref size not multiple of 4, was %" PRIu64 "\n",
            box.data_length);
    return box.length;
  }

  uint32_t from = be_uint32(box.data_begin);
  uint16_t count = be_uint16(box.data_begin + 4);
  if (box.data_length != 6 + count * 4) {
    fprintf(stderr, "iref size not %u, was %" PRIu64 "\n", 6 + count * 4,
            box.data_length);
    return box.length;
  }

  iprintf(options, "  ref from %d to:", from);
  for (unsigned i = 0; i < count; ++i)
    printf(" %d", be_uint32(box.data_begin + 6 + 4 * i));
  printf("\n");

  return box.length;
}

static void heif_dump_box_iref(struct Options* options,
                               const uint8_t* begin,
                               uint64_t size) {
  // ISO_IEC_14496-12_2015.pdf, 8.11.12 Item Reference Box
  if (size < 4) {
    fprintf(stderr, "iref not at least 4 bytes, was %" PRIu64 "\n", size);
    return;
  }

  uint32_t version_and_flags = be_uint32(begin);
  uint8_t version = version_and_flags >> 24;
  uint32_t flags = version_and_flags & 0xffffff;
  iprintf(options, "version %u, flags %u\n", version, flags);

  uint64_t (*dump_ref)(struct Options*, const uint8_t*, uint64_t);
  if (version == 0)
    dump_ref = heif_dump_box_item_reference;
  else if (version == 1)
    dump_ref = heif_dump_box_item_reference_large;
  else {
    fprintf(stderr, "iref version %d unknown\n", version);
    return;
  }

  uint64_t offset = 4;
  while (offset < size) {
    uint64_t box_size = dump_ref(options, begin + offset, size - offset);

    if (box_size > size - offset) {
      printf("box has size %" PRIu64 " but only %" PRIu64 " bytes left\n",
             box_size, size - offset);
      return;
    }

    offset += box_size;
  }
}

static void heif_dump_box_pitm(struct Options* options,
                               const uint8_t* begin,
                               uint64_t size) {
  // ISO_IEC_14496-12_2015.pdf, 8.11.4 Primary Item Box
  if (size < 4) {
    fprintf(stderr, "pitm not at least 4 bytes, was %" PRIu64 "\n", size);
    return;
  }

  uint32_t version_and_flags = be_uint32(begin);
  uint8_t version = version_and_flags >> 24;
  uint32_t flags = version_and_flags & 0xffffff;
  iprintf(options, "version %u, flags %u\n", version, flags);

  if (version == 0 && size != 6) {
    fprintf(stderr, "pitm v0 not 6 bytes, was %" PRIu64 "\n", size);
    return;
  }
  if (version != 0 && size != 8) {
    fprintf(stderr, "pitm not 8 bytes, was %" PRIu64 "\n", size);
    return;
  }

  uint32_t item_id;
  if (version == 0)
    item_id = be_uint16(begin + 4);
  else
    item_id = be_uint32(begin + 4);
  iprintf(options, "item id %u\n", item_id);
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

  uint64_t offset = 4;
  while (offset < size) {
    uint64_t box_size = heif_dump_box(options, begin + offset, begin + size);

    if (box_size > size - offset) {
      printf("box has size %" PRIu64 " but only %" PRIu64 " bytes left\n",
             box_size, size - offset);
      return;
    }

    offset += box_size;
  }
}

static uint64_t heif_dump_box(struct Options* options,
                              const uint8_t* begin,
                              const uint8_t* end) {
  const size_t size = (size_t)(end - begin);
  struct Box box = heif_read_box(begin, size);
  print_box(options, &box, begin + 4);

  increase_indent(options);
  switch (box.type) {
    case 0x64726566:  // 'dref'
      heif_dump_box_dref(options, box.data_begin, box.data_length);
      break;
    case 0x66747970:  // 'ftyp'
      heif_dump_box_ftyp(options, box.data_begin, box.data_length);
      break;
    case 0x69696e66:  // 'iinf'
      heif_dump_box_iinf(options, box.data_begin, box.data_length);
      break;
    case 0x696c6f63:  // 'iloc'
      heif_dump_box_iloc(options, box.data_begin, box.data_length);
      break;
    case 0x696e6665:  // 'infe'
      heif_dump_box_infe(options, box.data_begin, box.data_length);
      break;
    case 0x69726566:  // 'iref'
      heif_dump_box_iref(options, box.data_begin, box.data_length);
      break;
    case 0x6d657461:  // 'meta'
      heif_dump_full_box_container(options, box.data_begin, box.data_length);
      break;
    case 0x7069746d:  // 'pitm'
      heif_dump_box_pitm(options, box.data_begin, box.data_length);
      break;
    case 0x64696e66:  // 'dinf'
    case 0x6970636f:  // 'ipco'
    case 0x69707270:  // 'iprp'
    case 0x6d646961:  // 'mdia'
    case 0x6d696e66:  // 'minf'
    case 0x6d6f6f76:  // 'moov'
    case 0x7374626c:  // 'stbl'
    case 0x7472616b:  // 'trak'
      heif_dump_box_container(options, box.data_begin,
                              box.data_begin + box.data_length);
      break;
      // TODO: infe, iloc, ipma, ...
  }
  decrease_indent(options);

  return box.length;
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
