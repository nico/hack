/*
clang -o dm3tool -Wall main.c
*/

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "dm3.h"

static _Bool verbose = 0;

static struct option options[] = {
  { "dump", no_argument, NULL, 'd' },
  { "help", no_argument, NULL, 'h' },
  { "verbose", no_argument, NULL, 'v' },
  { }
};

static void usage() {
  fprintf(stderr,
"usage: dm3tool [options] inputfile.dm3\n"
"\n"
"options:\n"
//"  -o FILE  write image data to FILE\n"  // Not implemented.
"  -d       dump file tree to stdout\n"
"  -v       verbose output\n"
          );
}

static void fatal(const char* msg, ...) {
  va_list args;
  va_start(args, msg);
  vfprintf(stderr, msg, args);
  va_end(args);
  exit(1);
}

static void iprintf(int indent, const char* msg, ...) {
  for (int i = 0; i < indent; ++i)
    printf("  ");

  va_list args;
  va_start(args, msg);
  vprintf(msg, args);
  va_end(args);
}

static uint16_t dm3_uint16(struct DM3Image* dm3, uint16_t i) {
  if (!dm3->is_little_endian || dm3->is_little_endian == 0x01000000)
    return ntohs(i);
  return i;  // Assumes a little-endian machine.
}

static uint32_t dm3_uint32(struct DM3Image* dm3, uint32_t i) {
  if (!dm3->is_little_endian || dm3->is_little_endian == 0x01000000)
    return ntohl(i);
  return i;  // Assumes a little-endian machine.
}

static uint32_t dump_dm3_tag_group(
    struct DM3Image* dm3, struct DM3TagGroup* tags, int indent);

// Returns the size of |def|.
// |def_len|: (in) Number of elements in |def|.
//            (out) Number of elements read from |def|.
static uint32_t dump_dm3_data_definition(
    struct DM3Image* dm3, uint32_t def[], uint32_t* def_len, int indent) {
  uint32_t def0 = dm3_uint32(dm3, def[0]);
  static const char const* def_names[] = {
    "unknown 0",
    "unknown 1",
    "short",  /* DM3_DEF_SHORT */
    "long",  /* DM3_DEF_LONG */
    "ushort",  /* DM3_DEF_USHORT */
    "ulong",  /* DM3_DEF_ULONG */
    "float",  /* DM3_DEF_FLOAT */
    "double",  /* DM3_DEF_DOUBLE */
    "bool",  /* DM3_DEF_BOOL */
    "char",  /* DM3_DEF_CHAR */
    "octet"   /* DM3_DEF_OCTET */
    // DM3_DEF_STRUCT, DM3_DEF_STRING, DM3_DEF_ARRAY are not in this table.
  };
  static const uint32_t def_lens[] = {
    0,
    0,
    2,  /* DM3_DEF_SHORT */
    4,  /* DM3_DEF_LONG */
    2,  /* DM3_DEF_USHORT */
    4,  /* DM3_DEF_ULONG */
    4,  /* DM3_DEF_FLOAT */
    8,  /* DM3_DEF_DOUBLE */
    1,  /* DM3_DEF_BOOL */
    1,  /* DM3_DEF_CHAR */
    1   /* DM3_DEF_OCTET */
    // DM3_DEF_STRUCT, DM3_DEF_STRING, DM3_DEF_ARRAY are not in this table.
  };

  if (def0 == DM3_DEF_STRUCT) {
    uint32_t size = 0;
    //uint32_t struct_name_len = dm3_uint32(dm3, def[1]);
    uint32_t struct_def_len = 3;
    uint32_t struct_field_count = dm3_uint32(dm3, def[2]);
    iprintf(indent, "struct with %d fields: {\n", struct_field_count);
    for (int i = 0; i < struct_field_count; ++i) {
      uint32_t field_type = dm3_uint32(dm3, def[4 + 2 * i]);
      uint32_t field_def_len = *def_len - 4 - 2 * i;
      if (field_type > DM3_DEF_OCTET)
        fatal("TODO: Implement complex structs\n");
      
      size += dump_dm3_data_definition(
          dm3, def + 4 + 2 * i, &field_def_len, indent + 1);
      struct_def_len += field_def_len + 1;  // 1 for field name.
    }
    iprintf(indent, "}\n");
    *def_len = struct_def_len;
    return size;
  } else if (def0 == DM3_DEF_STRING) {
    uint32_t str_len;
    if (*def_len != 2)
      fatal("Unexpected string def_len %d\n", *def_len);
    str_len = dm3_uint32(dm3, def[1]);
    iprintf(indent, "String of length %d\n", str_len);
    return 2 * str_len;
  } else if (def0 == DM3_DEF_ARRAY) {
    uint32_t array_type_len;
    uint32_t array_type_def_len = *def_len - 1;
    uint32_t array_len;

    iprintf(indent, "array of ");
    array_type_len = dump_dm3_data_definition(
        dm3, def + 1, &array_type_def_len, /*indent=*/0);
    array_len = dm3_uint32(dm3, def[1 + array_type_def_len]);
    iprintf(indent, "  -- %d elements\n", array_len);
    return array_len * array_type_len;
  } else {
    if (def0 > DM3_DEF_OCTET || def0 <= 1)
      fatal("Unexpected def %d\n", def0);
    iprintf(indent, "%s\n", def_names[def0]);
    *def_len = 1;
    return def_lens[def0];
  }
}

// Returns the size of |data|.
static uint32_t dump_dm3_tag_data(
    struct DM3Image* dm3, struct DM3TagData* data, int indent) {
  uint32_t def_len = dm3_uint32(dm3, data->definition_length);
  uint32_t data_size = sizeof(*data) + def_len * sizeof(uint32_t);

  if (verbose) iprintf(indent, "Tag Data\n");
  if (verbose) {
    iprintf(indent, "Tag: %c%c%c%c\n",
        data->tag[0], data->tag[1], data->tag[2], data->tag[3]);
  }
  if (verbose) iprintf(indent, "Definition length: %d\n", def_len);
  if (verbose) iprintf(indent, "Definition:");
  if (verbose) for (int i = 0; i < def_len; ++i) {
    printf(" %d", dm3_uint32(dm3, data->definition[i]));
  }
  if (verbose) printf("\n");
  return data_size +
      dump_dm3_data_definition(dm3, data->definition, &def_len, indent);
}

// Returns the size of |tag|.
static uint32_t dump_dm3_tag_entry(
    struct DM3Image* dm3, struct DM3TagEntry* tag, int indent) {
  struct DM3TagData* tag_data;
  struct DM3TagGroup* tag_group;

  uint16_t label_len = dm3_uint16(dm3, tag->label_length);
  uint32_t tag_size = sizeof(*tag) + label_len;

  if (verbose) iprintf(indent, "Type: %d\n", tag->type);
  if (verbose) iprintf(indent, "Label length: %d\n", label_len);
  iprintf(indent, "Label: \'");
  for (int i = 0; i < label_len; ++i)
    printf("%c", tag->label[i]);
  printf("\'\n");

  switch (tag->type) {
    case DM3_TAG_ENTRY_TYPE_TAG_GROUP:
      tag_group = (struct DM3TagGroup*)(tag->label + label_len);
      return tag_size + dump_dm3_tag_group(dm3, tag_group, indent + 1);
      break;
    case DM3_TAG_ENTRY_TYPE_DATA:
      tag_data = (struct DM3TagData*)(tag->label + label_len);
      return tag_size + dump_dm3_tag_data(dm3, tag_data, indent + 1);
      break;
    default:
      fatal("Unknown tag type %d\n", tag->type);
  }

  return 0;
}

// Returns the size of |tags|.
static uint32_t dump_dm3_tag_group(
    struct DM3Image* dm3, struct DM3TagGroup* tags, int indent) {
  uint8_t* tag_data = (uint8_t*)tags->tags;
  uint32_t num_tags = dm3_uint32(dm3, tags->num_tags);

  if (verbose) iprintf(indent, "Tag Group\n");
  if (verbose) iprintf(indent, "Sorted: %d\n", tags->is_sorted);
  if (verbose) iprintf(indent, "Open: %d\n", tags->is_open);
  if (verbose) iprintf(indent, "Number of tags: %d\n", num_tags);

  for (int i = 0; i < num_tags; ++i) {
    struct DM3TagEntry* cur_tag = (struct DM3TagEntry*)tag_data;
    uint32_t tag_size = dump_dm3_tag_entry(dm3, cur_tag, indent + 1);
    tag_data += tag_size;
  }
  return sizeof(*tags) + tag_data - (uint8_t*)tags->tags;
}

static void dump_dm3(struct DM3Image* dm3) {
  uint32_t version = dm3_uint32(dm3, dm3->version);
  printf("Version: %u\n", version);
  if (verbose) printf("Length: %u\n", dm3_uint32(dm3, dm3->length));
  if (verbose)
    printf("Is little-endian: %u\n", dm3_uint32(dm3, dm3->is_little_endian));
  if (version != 3)
    fatal("Unsupported file version\n");
  printf("------\n");
  dump_dm3_tag_group(dm3, &dm3->tag_group, /*indent=*/0);
}

int main(int argc, char* argv[]) {
  int in_file;
  struct stat in_stat;
  struct DM3Image* in_dm3;

  // Parse options.
  _Bool do_dump = 0;
  const char* in_name = NULL;
  const char* out_name = NULL;

  int opt;
  while ((opt = getopt_long(argc, argv, "o:dhv", options, NULL)) != -1) {
    switch (opt) {
      case 'd':
        do_dump = 1;
        break;
      case 'o':
        out_name = optarg;
        break;
      case 'v':
        verbose = 1;
        break;
      case 'h':
      default:
        usage();
        return 1;
    }
  }
  argv += optind;
  argc -= optind;

  if (argc != 1) {
    usage();
    return 1;
  }
  in_name = argv[0];

  // Read input.
  in_file = open(in_name, O_RDONLY);
  if (!in_file)
    fatal("Unable to read \'%s\'\n", in_name);

  if (fstat(in_file, &in_stat))
    fatal("Failed to stat \'%s\'\n", in_name);

  // Casting memory like this is not portable.
  in_dm3 = (struct DM3Image*)mmap(/*addr=*/0, in_stat.st_size,
                                  PROT_READ, MAP_SHARED, in_file, /*offset=*/0);
  if (in_dm3 == MAP_FAILED)
    fatal("Failed to mmap: %d (%s)\n", errno, strerror(errno));

  if (do_dump)
    dump_dm3(in_dm3);

  munmap(in_dm3, in_stat.st_size);
  close(in_file);

  return 0;
}
