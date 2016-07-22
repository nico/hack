/*
cl resobjdump.c
clang -o resobjdump resobjdump.c

Dumps PE object files with rsrc sections, PE spec section 6.8.
*/
#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

static void fatal(const char* msg, ...) {
  va_list args;
  va_start(args, msg);
  vfprintf(stderr, msg, args);
  va_end(args);
  exit(1);
}

uint32_t read_big_long(unsigned char* d) {
  return (d[0] << 24) | (d[1] << 16) | (d[2] << 8) | d[3];
}

uint32_t read_little_long(unsigned char* d) {
  return (d[3] << 24) | (d[2] << 16) | (d[1] << 8) | d[0];
}

unsigned short read_little_short(unsigned char* d) {
  return (d[1] << 8) | d[0];
}

typedef struct {
  char Name[8];
  uint32_t VirtualSize;
  uint32_t VirtualAddress;
  uint32_t SizeOfRawData;
  uint32_t PointerToRawData;
  uint32_t PointerToRelocations;
  uint32_t PointerToLineNumbers;
  uint16_t NumberOfRelocations;
  uint16_t NumberOfLinenumbers;
  uint32_t Characteristics;
} SectionHeader;


static void dump_header(SectionHeader* header) {
  printf("Name: %.8s\n", header->Name);
  printf("Virtual Size: %" PRIu32 "\n", header->VirtualSize);
  printf("Raw Size: %" PRIu32 "\n", header->SizeOfRawData);
  printf("Num Relocs: %" PRIu16 "\n", header->NumberOfRelocations);
  printf("Num Lines: %" PRIu16 "\n", header->NumberOfLinenumbers);
  printf("Characteristics: %" PRIx32 "\n", header->Characteristics);
}

typedef struct {
  uint16_t Machine;
  uint16_t NumberOfSections;
  uint32_t TimeDateStamp;
  uint32_t PointerToSymbolTable;
  uint32_t NumberOfSymbols;
  uint16_t SizeOfOptionalHeader;
  uint16_t Characteristics;
} FileHeader;

static void dump_real_object(FileHeader* object) {
  printf("Machine: 0x%x\n", object->Machine);
  printf("Num sections: %u\n", object->NumberOfSections);
  printf("TimeDateStamp: %u\n", object->TimeDateStamp);
  printf("Num symbols: %u\n", object->NumberOfSymbols);
  printf("Characteristic: 0x%x\n", object->Characteristics);

  // XXX dump relocations
  // XXX dump symbol table
  // XXX dump string table
}

static void dump(unsigned char* contents, unsigned char* contents_end) {
  FileHeader* header = (FileHeader*)contents;
  if (header->SizeOfOptionalHeader)
    fatal("size of optional header should be 0 for .obj files");
  contents += sizeof(*header);

  dump_real_object(header);

  for (unsigned i = 0; i < header->NumberOfSections; ++i) {
    SectionHeader* header = (SectionHeader*)contents;
    dump_header(header);
    contents += sizeof(*header);
  }
}

int main(int argc, char* argv[]) {
  if (argc != 2)
    fatal("Expected args == 2, got %d\n", argc);
  const char* in_name = argv[1];

  // Read input.
#if defined(_WIN32)
  HANDLE in_file = CreateFile(in_name, GENERIC_READ, FILE_SHARE_READ, NULL,
                              OPEN_EXISTING, FILE_ATTRIBUTE_READONLY, NULL);
  if (in_file == INVALID_HANDLE_VALUE)
    fatal("Unable to read \'%s\'\n", in_name);

  DWORD size = GetFileSize(in_file, NULL);  // 4GB ought to be enough for anyone
  if (size == INVALID_FILE_SIZE)
    fatal("Unable to get file size of \'%s\'\n", in_name);

  HANDLE mapping = CreateFileMapping(in_file, NULL, PAGE_READONLY, 0, 0, NULL);
  if (mapping == NULL)
    fatal("Unable to map \'%s\'\n", in_name);

  // Casting memory like this is not portable.
  uint8_t* obj_contents = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
  if (obj_contents == NULL)
    fatal("Failed to MapViewOfFile: %s\n", in_name);
#else
  int in_file = open(in_name, O_RDONLY);
  if (!in_file)
    fatal("Unable to read \'%s\'\n", in_name);

  struct stat in_stat;
  if (fstat(in_file, &in_stat))
    fatal("Failed to stat \'%s\'\n", in_name);

  size_t size = in_stat.st_size;
  uint8_t* obj_contents = mmap(/*addr=*/0, size, PROT_READ, MAP_SHARED, in_file,
                               /*offset=*/0);
  if (obj_contents == MAP_FAILED)
    fatal("Failed to mmap: %d (%s)\n", errno, strerror(errno));
#endif

  dump(obj_contents, obj_contents + size);

#if defined(_WIN32)
  UnmapViewOfFile(obj_contents);
  CloseHandle(mapping);
  CloseHandle(in_file);
#else
  munmap(obj_contents, in_stat.st_size);
  close(in_file);
#endif
}
