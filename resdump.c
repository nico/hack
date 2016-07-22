/*
clang -o resdump resdump.c

Dumps res files created by rc.exe.
*/
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

static size_t dump_resource_entry(unsigned char* data) {
  uint32_t data_size = read_little_long(&data);
  uint32_t header_size = read_little_long(&data);

  printf("Resource Entry, data size %" PRIx32 ", header size %" PRIx32 "\n",
         data_size, header_size);

  if (header_size < 20)
    fatal("header too small");

  uint32_t type = read_little_long(&data);
  uint32_t name = read_little_long(&data);
  uint32_t data_version = read_little_long(&data);
  uint16_t memory_flags = read_little_short(&data);
  uint16_t language_id = read_little_short(&data);
  uint32_t version = read_little_long(&data);
  uint32_t characteristics = read_little_long(&data);

  printf("  type %" PRIx32 " name %" PRIx32 " dataversion %" PRIx32 "\n", type,
         name, data_version);
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
