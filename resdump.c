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

uint32_t read_little_long(unsigned char** d) {
  uint32_t r = ((*d)[3] << 24) | ((*d)[2] << 16) | ((*d)[1] << 8) | (*d)[0];
  *d += sizeof(uint32_t);
  return r;
}

uint16_t read_little_short(unsigned char** d) {
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
  RT_VERSION = 16,
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
  case 0: return "null";
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

static size_t dump_resource_entry(unsigned char* data) {
  uint32_t data_size = read_little_long(&data);
  uint32_t header_size = read_little_long(&data);

  printf("Resource Entry, data size %" PRIx32 ", header size %" PRIx32 "\n",
         data_size, header_size);

  if (header_size < 20)
    fatal("header too small");

  // FIXME: if type, name start with 0xffff then they're numeric IDs. Else
  // they're inline unicode strings.
  uint32_t type = read_little_long(&data);
  if ((type & 0xffff) != 0xffff)
    fatal("string types not yet supported");
  uint32_t name = read_little_long(&data);
  if ((name & 0xffff) != 0xffff)
    fatal("string names not yet supported");
  uint32_t data_version = read_little_long(&data);
  uint16_t memory_flags = read_little_short(&data);
  uint16_t language_id = read_little_short(&data);
  uint32_t version = read_little_long(&data);
  uint32_t characteristics = read_little_long(&data);

  printf("  type %" PRIx32 " (%s) name %" PRIx32 " dataversion %" PRIx32 "\n",
         type, type_str(type >> 16), name, data_version);
  printf("  memflags %" PRIx32 " langid %" PRIx32 " version %" PRIx32 "\n",
         memory_flags, language_id, version);
  printf("  characteristics %" PRIx32 "\n", characteristics);

  uint32_t total_size = data_size + header_size;
  return total_size + (total_size & 1);  // Pad.
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

  unsigned char* data =
      mmap(/*addr=*/0, in_stat.st_size, PROT_READ, MAP_SHARED, in_file,
           /*offset=*/0);
  if (data == MAP_FAILED)
    fatal("Failed to mmap: %d (%s)\n", errno, strerror(errno));

  unsigned char* end = data + in_stat.st_size;

  while (data < end) {
    data += dump_resource_entry(data);
  }

  munmap(data, in_stat.st_size);
  close(in_file);
}
