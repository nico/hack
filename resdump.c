/*
clang -o resdump resdump.c

Dumps res files created by rc.exe.
*/
#include <assert.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <fcntl.h>
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

static uint32_t read_little_long(unsigned char** d) {
  uint32_t r = ((*d)[3] << 24) | ((*d)[2] << 16) | ((*d)[1] << 8) | (*d)[0];
  *d += sizeof(uint32_t);
  return r;
}

static uint16_t read_little_short(unsigned char** d) {
  uint16_t r = ((*d)[1] << 8) | (*d)[0];
  *d += sizeof(uint16_t);
  return r;
}

// https://msdn.microsoft.com/en-us/library/windows/desktop/ms648009(v=vs.85).aspx
enum {
  RT_CURSOR = 1,
  RT_BITMAP = 2,
  RT_ICON = 3,
  RT_MENU = 4,
  RT_DIALOG = 5,
  RT_STRING = 6,
  RT_FONTDIR = 7,
  RT_FONT = 8,
  RT_ACCELERATORS = 9,
  RT_RCDATA = 10,
  RT_MESSAGETABLE = 11,
  RT_GROUP_CURSOR = 12,
  RT_GROUP_ICON = 14,
  RT_VERSION = 16,    // Not stored in image file.
  RT_DLGINCLUDE = 17,
  RT_PLUGPLAY = 19,
  RT_VXD = 20,
  RT_ANICURSOR = 21,
  RT_ANIICON = 22,
  RT_HTML = 23,
  RT_MANIFEST = 24,
};

static const char* type_str(uint16_t type) {
  switch (type) {
  case 0: return "not 16-bit resource marker";  // First entry only.
  case RT_CURSOR: return "RT_CURSOR";
  case RT_BITMAP: return "RT_BITMAP";
  case RT_ICON: return "RT_ICON";
  case RT_MENU: return "RT_MENU";
  case RT_DIALOG: return "RT_DIALOG";
  case RT_STRING: return "RT_STRING";
  case RT_FONTDIR: return "RT_FONTDIR";
  case RT_FONT: return "RT_FONT";
  case RT_ACCELERATORS: return "RT_ACCELERATORS";
  case RT_RCDATA: return "RT_RCDATA";
  case RT_MESSAGETABLE: return "RT_MESSAGETABLE";
  case RT_GROUP_CURSOR: return "RT_GROUP_CURSOR";
  case RT_GROUP_ICON: return "RT_GROUP_ICON";
  case RT_VERSION: return "RT_VERSION";
  case RT_DLGINCLUDE: return "RT_DLGINCLUDE";
  case RT_PLUGPLAY: return "RT_PLUGPLAY";
  case RT_VXD: return "RT_VXD";
  case RT_ANICURSOR: return "RT_ANICURSOR";
  case RT_ANIICON: return "RT_ANIICON";
  case RT_HTML: return "RT_HTML";
  case RT_MANIFEST: return "RT_MANIFEST";
  default: printf("%x\n", type); assert(0 && "unknown type"); return "unknown";
  }
}

static size_t dump_resource_entry(uint8_t* data) {
  uint32_t data_size = read_little_long(&data);
  uint32_t header_size = read_little_long(&data);

  printf("Resource Entry, data size 0x%" PRIx32 ", header size 0x%" PRIx32 "\n",
         data_size, header_size);

  if (header_size < 20)
    fatal("header too small");

  // https://msdn.microsoft.com/en-us/library/windows/desktop/ms648027(v=vs.85).aspx

  // if type, name start with 0xffff then they're numeric IDs. Else they're
  // inline zero-terminated utf-16le strings. After name, there might be one
  // word of padding to align data_version.
  uint8_t* string_start = data;
  uint16_t type = read_little_short(&data);
  if (type == 0xffff) {
    type = read_little_short(&data);
    printf("  type 0x%" PRIx32 " (%s) ", type, type_str(type));
  } else {
    printf("  type \"");
    while (type != 0) {
      if (type < 128)
        fputc(type, stdout);
      else
        fputc('?', stdout);
      type = read_little_short(&data);
    }
    printf("\" ");
  }
  uint16_t name = read_little_short(&data);
  if (name == 0xffff) {
    name = read_little_short(&data);
    printf("name 0x%" PRIx32 " ", name);
  } else {
    printf("name \"");
    while (name != 0) {
      if (name < 128)
        fputc(name, stdout);
      else
        fputc('?', stdout);
      name = read_little_short(&data);
    }
    printf("\" ");
  }
  // Pad to dword boundary:
  if ((data - string_start) & 2)
    data += 2;

  uint32_t data_version = read_little_long(&data);
  uint16_t memory_flags = read_little_short(&data);
  uint16_t language_id = read_little_short(&data);
  uint32_t version = read_little_long(&data);
  uint32_t characteristics = read_little_long(&data);

  printf("dataversion 0x%" PRIx32 "\n", data_version);
  printf("  memflags 0x%" PRIx16 " langid %" PRIu16 " version %" PRIx32 "\n",
         memory_flags, language_id, version);
  printf("  characteristics %" PRIx32 "\n", characteristics);

  uint32_t total_size = data_size + header_size;
  return total_size + ((4 - (total_size & 3)) & 3);  // DWORD-align.
}

int main(int argc, char* argv[]) {
  if (argc != 2)
    fatal("Expected args == 2, got %d\n", argc);

  const char *in_name = argv[1];

  // Read input.
  int in_file = open(in_name, O_RDONLY);
  if (!in_file)
    fatal("Unable to read \'%s\'\n", in_name);

  struct stat in_stat;
  if (fstat(in_file, &in_stat))
    fatal("Failed to stat \'%s\'\n", in_name);

  uint8_t* data =
      mmap(/*addr=*/0, in_stat.st_size, PROT_READ, MAP_SHARED, in_file,
           /*offset=*/0);
  if (data == MAP_FAILED)
    fatal("Failed to mmap: %d (%s)\n", errno, strerror(errno));

  uint8_t* end = data + in_stat.st_size;

  while (data < end) {
    data += dump_resource_entry(data);
  }

  munmap(data, in_stat.st_size);
  close(in_file);
}
