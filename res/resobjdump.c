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

const char kPad[] = "                                                  ";

static void fatal(const char* msg, ...) {
  va_list args;
  va_start(args, msg);
  vfprintf(stderr, msg, args);
  va_end(args);
  exit(1);
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
  int16_t SectionNumber;  // 1-based index, or a special value (0, -1, -2)
  uint16_t Type;
  uint8_t StorageClass;
  uint8_t NumberOfAuxSymbols;
} StandardSymbolRecord;
#if !defined(_MSC_VER) || defined(__clang__)
_Static_assert(sizeof(StandardSymbolRecord) == 18, "");
#endif

typedef struct {
  uint32_t Length;
  int16_t NumberOfRelocations;
  int16_t NumberOfLinenumbers;
  uint32_t CheckSum;
  int16_t Number;
  uint8_t Selection;
  uint8_t Pad0;
  uint8_t Pad1;
  uint8_t Pad2;
} SectionAuxSymbolRecord;
#if !defined(_MSC_VER) || defined(__clang__)
_Static_assert(sizeof(SectionAuxSymbolRecord) == 18, "");
#endif
#pragma pack(pop)

static void dump_real_object(FileHeader* object) {
  printf("Machine: 0x%x\n", object->Machine);
  printf("Num sections: %u\n", object->NumberOfSections);
  printf("TimeDateStamp: %u\n", object->TimeDateStamp);
  printf("Num symbols: %u\n", object->NumberOfSymbols);
  printf("Characteristic: 0x%x\n", object->Characteristics);

  // Dump symbol table.
  printf("Symbol table (%" PRId32 " entries):\n", object->NumberOfSymbols);
  StandardSymbolRecord* symbols =
      (StandardSymbolRecord*)((uint8_t*)object + object->PointerToSymbolTable);
  for (int i = 0; i < object->NumberOfSymbols; ++i) {
    if (memcmp(symbols[i].Name, "\0\0\0", 4) == 0)
      printf("(indexed name) ");
    else
      printf("%.8s ", symbols[i].Name);

    printf("Value 0x%08" PRIx32 ", ", symbols[i].Value);
    printf("Section %2" PRId16 ", ", symbols[i].SectionNumber);
    printf("Type %" PRIx16 ", ", symbols[i].Type);
    printf("Storage class %" PRId8 "\n", symbols[i].StorageClass);

    if (symbols[i].StorageClass == 3 && symbols[i].Value == 0 &&
        symbols[i].NumberOfAuxSymbols == 1) {
      SectionAuxSymbolRecord* aux = (SectionAuxSymbolRecord*)(symbols + i + 1);
      printf("    len %" PRIu32 " numrel %" PRIu16 " numstr %" PRIu16
             " checksum %" PRIu32 " num %" PRIu16 " sel %d pad %d %d %d\n",
             aux->Length, aux->NumberOfRelocations, aux->NumberOfLinenumbers,
             aux->CheckSum, aux->Number, aux->Selection, aux->Pad0, aux->Pad1,
             aux->Pad2);
    } else {
      // Print a line for each aux symbol.  The index in a relocation includes
      // aux symbols too, so this makes it easier to manually resolve indices.
      for (int j = 0; j < symbols[i].NumberOfAuxSymbols; ++j)
        printf("    (aux symbol)\n");

    }

    i += symbols[i].NumberOfAuxSymbols;
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

#pragma pack(push, 1)
typedef struct {
  uint32_t VirtualAddress;
  uint32_t SymbolTableInd;  // zero-based
  uint16_t Type;
} Relocation;
#if !defined(_MSC_VER) || defined(__clang__)
_Static_assert(sizeof(Relocation) == 10, "");
#endif
#pragma pack(pop)

static void dump_section_header(uint8_t* contents_start,
                                SectionHeader* header) {
  printf("Name: %.8s\n", header->Name);
  printf("Virtual Size: %" PRIu32 "\n", header->VirtualSize);
  printf("Virtual Address: %" PRIu32 "\n", header->VirtualAddress);
  printf("Raw Size: %" PRIu32 "\n", header->SizeOfRawData);
  printf("Raw Data: 0x%" PRIx32 "\n", header->PointerToRawData);
  printf("Num Relocs: %" PRIu16 "\n", header->NumberOfRelocations);
  printf("Num Lines: %" PRIu16 "\n", header->NumberOfLinenumbers);
  printf("Characteristics: 0x%" PRIx32 "\n", header->Characteristics);

  Relocation* relocs =
      (Relocation*)(contents_start + header->PointerToRelocations);
  printf("%d relocations at 0x%x\n", header->NumberOfRelocations,
         header->PointerToRelocations);
  for (int i = 0; i < header->NumberOfRelocations; ++i) {
    printf("addr 0x%" PRIx32 " symbol ind %" PRId32 " type %" PRId16 "\n",
           relocs[i].VirtualAddress, relocs[i].SymbolTableInd, relocs[i].Type);
  }
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

typedef struct {
  uint32_t DataRVA;
  uint32_t Size;
  uint32_t Codepage;
  uint32_t Reserved;
} ResourceDataEntry;

static void dump_rsrc_data_entry(uint8_t* start, int indent) {
  ResourceDataEntry* entry = (ResourceDataEntry*)start;
  printf("%.*sDataRVA %" PRId32 "\n", indent, kPad, entry->DataRVA);
  printf("%.*sSize %" PRId32 "\n", indent, kPad, entry->Size);
  if (entry->Codepage)
    printf("%.*sCodepage %" PRId32 "\n", indent, kPad, entry->Codepage);
  if (entry->Reserved)
    printf("%.*sReserved %" PRIu32 "\n", indent, kPad, entry->Reserved);
}

static void dump_rsrc_section(uint8_t* section_start,
                              uint8_t* start,
                              uint8_t* end,
                              int indent) {
  ResourceDirectoryHeader* header = (ResourceDirectoryHeader*)start;
  start += sizeof(*header);

  if (header->Characteristics)
    printf("%.*sCharacteristics 0x%" PRIx32 "\n", indent, kPad,
           header->Characteristics);
  if (header->TimeDateStamp)
    printf("%.*sTimeDateStamp %" PRIu32 "\n", indent, kPad,
           header->TimeDateStamp);
  if (header->MajorVersion)
    printf("%.*sMajorVersion %" PRIu16 "\n", indent, kPad,
           header->MajorVersion);
  if (header->MinorVersion)
    printf("%.*sMinorVersion %" PRIu16 "\n", indent, kPad,
           header->MinorVersion);
  //printf("%.*sNumberOfNameEntries %" PRIu16 "\n", indent, kPad,
         //header->NumberOfNameEntries);
  //printf("%.*sNumberOfIdEntries %" PRIu16 "\n", indent, kPad,
         //header->NumberOfIdEntries);

  for (int i = 0; i < header->NumberOfNameEntries + header->NumberOfIdEntries;
       ++i) {
    ResourceDirectoryEntry* entry = (ResourceDirectoryEntry*)start;

    if (i < header->NumberOfNameEntries) {
      printf("%.*sTypeNameLang str (0x%x) ", indent, kPad, entry->TypeNameLang);
      // It looks like cvtres.exe sets the high bit if TypeNameLang is a string,
      // even though the COFF spec doesn't mention that (and it's redundant
      // with having both NumberOfNameEntries and NumberOfIdEntries fields).
      uint16_t* str =
          (uint16_t*)(section_start + (entry->TypeNameLang & ~0x80000000));
      for (unsigned i = 0, e = *str; i < e; ++i)
        printf("%c", str[i + 1]);
      printf("\n");
    } else {
      printf("%.*sTypeNameLang %" PRIu32 "\n", indent, kPad,
             entry->TypeNameLang);
    }
    printf("%.*sDataRVA %" PRIx32 "\n", indent, kPad, entry->DataRVA);
    if (entry->DataRVA & 0x80000000) {
      dump_rsrc_section(section_start,
                        section_start + (entry->DataRVA & ~0x80000000), end,
                        indent + 2);
    } else {
      dump_rsrc_data_entry(section_start + entry->DataRVA, indent + 2);
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
    dump_section_header(contents_start, header);

    // It looks like cvtres puts the resource tree description metadata in
    // .rsrc$01 and the actual resource data in .rsrc$02.
    if (strncmp(header->Name, ".rsrc$01", 8) == 0) {
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
