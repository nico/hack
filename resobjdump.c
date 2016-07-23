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

uint32_t read_big_long(uint8_t* d) {
  return (d[0] << 24) | (d[1] << 16) | (d[2] << 8) | d[3];
}

uint32_t read_little_long(uint8_t* d) {
  return (d[3] << 24) | (d[2] << 16) | (d[1] << 8) | d[0];
}

unsigned short read_little_short(uint8_t* d) {
  return (d[1] << 8) | d[0];
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

#pragma pack(push, 1)
typedef struct {
  char Name[8];
  uint32_t Value;
  uint16_t SectionNumber;
  uint16_t Type;
  uint8_t StorageClass;
  uint8_t NumberOfAuxSymbols;
} StandardSymbolRecord;
_Static_assert(sizeof(StandardSymbolRecord) == 18, "");
#pragma pack(pop)

static void dump_real_object(FileHeader* object) {
  printf("Machine: 0x%x\n", object->Machine);
  printf("Num sections: %u\n", object->NumberOfSections);
  printf("TimeDateStamp: %u\n", object->TimeDateStamp);
  printf("Num symbols: %u\n", object->NumberOfSymbols);
  printf("Characteristic: 0x%x\n", object->Characteristics);

  // XXX dump relocations?

  // Dump symbol table.
  printf("Symbol table:\n");
  StandardSymbolRecord* symbols =
      (StandardSymbolRecord*)((uint8_t*)object + object->PointerToSymbolTable);
  for (int i = 0; i < object->NumberOfSymbols; ++i) {
    if (memcmp(symbols[i].Name, "\0\0\0", 4) == 0)
      printf("(indexed name)\n");
    else
      printf("%.8s\n", symbols[i].Name);
    i += symbols[i].NumberOfAuxSymbols;
    // XXX print more stuff.
  }

  // Dump string table size. It's supposed to be at least 4, but cvtres doesn't
  // write it correctly.
  uint32_t string_size = *(uint32_t*)(symbols + object->NumberOfSymbols);
  printf("String table size: %" PRIi32 "\n", string_size);

  printf("\n");
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
  printf("Raw Data: 0x%" PRIx32 "\n", header->PointerToRawData);
  printf("Num Relocs: %" PRIu16 "\n", header->NumberOfRelocations);
  printf("Num Lines: %" PRIu16 "\n", header->NumberOfLinenumbers);
  printf("Characteristics: 0x%" PRIx32 "\n", header->Characteristics);
}

typedef struct {
  uint32_t Characteristics;
  uint32_t TimeDateStamp;
  uint16_t MajorVersion;
  uint16_t MinorVersion;
  uint16_t NumberOfNameEntries;
  uint16_t NumberOfIdEntries;
} ResourceDirectoryHeader;

typedef struct {
  uint32_t TypeNameLang;  // Either string address or id.
  // High bit 0: Address of a Resource Data Entry (a leaf).
  // High bit 1: Address of a Resource Directory Table.
  uint32_t DataRVA;
} ResourceDirectoryEntry;

static void dump_rsrc_section(uint8_t* section_start,
                              uint8_t* start,
                              uint8_t* end,
                              int indent) {
  const char* kPad = "                                                  ";
  ResourceDirectoryHeader* header = (ResourceDirectoryHeader*)start;
  start += sizeof(*header);
  printf("%.*sCharacteristics 0x%" PRIx32 "\n", indent, kPad,
         header->Characteristics);
  printf("%.*sTimeDateStamp %" PRIu32 "\n", indent, kPad, header->TimeDateStamp);
  printf("%.*sMajorVersion %" PRIu16 "\n", indent, kPad, header->MajorVersion);
  printf("%.*sMinorVersion %" PRIu16 "\n", indent, kPad, header->MinorVersion);
  printf("%.*sNumberOfNameEntries %" PRIu16 "\n", indent, kPad,
         header->NumberOfNameEntries);
  printf("%.*sNumberOfIdEntries %" PRIu16 "\n", indent, kPad,
         header->NumberOfIdEntries);

  if (header->NumberOfNameEntries != 0)
    fatal("Cannot handle named resource directory entries yet\n");

  for (int i = 0; i < header->NumberOfIdEntries; ++i) {
    ResourceDirectoryEntry* entry = (ResourceDirectoryEntry*)start;

    printf("%.*sTypeNameLang %" PRIu32 "\n", indent, kPad, entry->TypeNameLang);
    printf("%.*sDataRVA %" PRIx32 "\n", indent, kPad, entry->DataRVA);
    if (entry->DataRVA & 0x80000000) {
      dump_rsrc_section(section_start,
                        section_start + (entry->DataRVA & ~0x80000000), end,
                        indent + 2);
    }

    start += sizeof(*entry);
  }
}

static void dump(uint8_t* contents, uint8_t* contents_end) {
  uint8_t* contents_start = contents;

  FileHeader* header = (FileHeader*)contents;
  if (header->SizeOfOptionalHeader)
    fatal("size of optional header should be 0 for .obj files\n");
  contents += sizeof(*header);

  dump_real_object(header);

  for (unsigned i = 0; i < header->NumberOfSections; ++i) {
    SectionHeader* header = (SectionHeader*)contents;
    dump_header(header);

    if (strncmp(header->Name, ".rsrc", 5) == 0) {  // Prefix-match .rsrc$02 etc
      dump_rsrc_section(
          contents_start + header->PointerToRawData,
          contents_start + header->PointerToRawData,
          contents_start + header->PointerToRawData + header->SizeOfRawData,
          /*indent=*/2);
    }

    printf("\n");
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
