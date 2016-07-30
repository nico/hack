/*
clang++ -std=c++11 -o cvtres cvtres.cc

A reimplemenation of cvtres.exe
*/
#include <vector>

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

struct ResEntry {
  uint32_t data_size;
  uint32_t header_size;  // Always 0x20 plus storage for type_str and name_str
                         // if type or name aren't numeric.

  bool type_is_str;  // determines which of the following two is valid.
  uint16_t type;
  std::vector<char16_t> type_str;

  bool name_is_str;  // determines which of the following two is valid.
  uint16_t name;
  std::vector<char16_t> name_str;

  uint32_t data_version;
  uint16_t memory_flags;
  uint16_t language_id;
  uint32_t version;
  uint32_t characteristics;

  uint8_t* data;  // weak
  // XXX make this owned (?)
  //std::unique_ptr<uint8_t[]> data;
};

struct ResEntries {
  std::vector<ResEntry> entries;
};

static ResEntry load_resource_entry(uint8_t* data, uint32_t* n_read) {
  ResEntry entry;
  entry.data_size = read_little_long(&data);
  entry.header_size = read_little_long(&data);

  // https://msdn.microsoft.com/en-us/library/windows/desktop/ms648027(v=vs.85).aspx

  // if type, name start with 0xffff then they're numeric IDs. Else they're
  // inline zero-terminated utf-16le strings. After name, there might be one
  // word of padding to align data_version.
  uint8_t* string_start = data;
  uint16_t type = read_little_short(&data);
  entry.type_is_str = type != 0xffff;
  if (!entry.type_is_str) {
    entry.type = read_little_short(&data);
  } else {
    while (type != 0) {
      entry.type_str.push_back(type);
      type = read_little_short(&data);
    }
  }
  uint16_t name = read_little_short(&data);
  entry.name_is_str = name != 0xffff;
  if (!entry.name_is_str) {
    entry.name = read_little_short(&data);
  } else {
    while (name != 0) {
      entry.name_str.push_back(name);
      name = read_little_short(&data);
    }
  }
  // Pad to dword boundary:
  if ((data - string_start) & 2)
    data += 2;
  // Check that bigger headers are explained by string types and names.
  if (entry.header_size != 0x20 + (data - string_start - 8))
    fatal("unexpected header size\n");  // XXX error code

  entry.data_version = read_little_long(&data);
  entry.memory_flags = read_little_short(&data);
  entry.language_id = read_little_short(&data);
  entry.version = read_little_long(&data);
  entry.characteristics = read_little_long(&data);

  entry.data = data;

  uint32_t total_size = entry.data_size + entry.header_size;
  *n_read = total_size + ((4 - (total_size & 3)) & 3);  // DWORD-align.
  return entry;
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

  uint8_t* data = (uint8_t*)mmap(/*addr=*/0, in_stat.st_size, PROT_READ,
                                 MAP_SHARED, in_file,
                                 /*offset=*/0);
  if (data == MAP_FAILED)
    fatal("Failed to mmap: %d (%s)\n", errno, strerror(errno));

  uint8_t* end = data + in_stat.st_size;

  ResEntries entries;
  while (data < end) {
    uint32_t n_read;
    entries.entries.push_back(load_resource_entry(data, &n_read));
    data += n_read;
  }

  munmap(data, in_stat.st_size);
  close(in_file);
}
