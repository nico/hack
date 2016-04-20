/*
cl libdump.c

Dumps lib.exe import libraries, PE spec section 8.
*/
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <windows.h>

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
  char Name[16];
  char Date[12];
  char UID[6];
  char GID[6];
  char Mode[8];
  char Size[10];  // Excluding size of header.
  char End[2];
} ArchiveMemberHeader;

static void dump_header(ArchiveMemberHeader* header) {
  printf("Name: %.16s\n", header->Name);
  printf("Date: %.12s\n", header->Date);
  printf("UID: %.6s\n", header->UID);
  printf("GID: %.6s\n", header->GID);
  printf("Mode: %.8s\n", header->Mode);
  printf("Size: %.10s\n", header->Size);
  printf("End: %.2s\n", header->End);
}

static void dump_first_header(unsigned char* contents,
                              uint32_t* num_offsets, uint32_t** offsets) {
  uint32_t num_symbols = read_big_long(contents);
  contents += 4;
  printf("Num symbols: %u\n", num_symbols);
  printf("Offsets:\n");
  *offsets = malloc(num_symbols * sizeof(uint32_t));  // Leaks; that's ok.
  uint32_t di = 0, last_offset = 0;
  for (unsigned i = 0; i < num_symbols; ++i) {
    unsigned offset = read_big_long(contents);
    // In the first header, the same offset might appear multiple times.
    // (In the second header, this isn't needed due to the index table.)
    if (offset != last_offset)
      (*offsets)[di++] = offset;
    contents += 4;
    printf("%u\n", offset);
  }
  *num_offsets = di;

  printf("String table:\n");
  for (unsigned i = 0; i < num_symbols; ++i) {
    unsigned char* end = (unsigned char*)strchr((char*)contents, '\0');
    printf("%.*s\n", (int)(end - contents), contents);
    contents += end - contents + 1;
  }
  printf("\n");
}

static void dump_second_header(unsigned char* contents,
                               uint32_t* num_offsets, uint32_t** offsets) {
  uint32_t num_members = read_little_long(contents);
  contents += 4;
  printf("Num members: %u\n", num_members);
  printf("Offsets:\n");
  *num_offsets = num_members;
  *offsets = malloc(num_members * sizeof(uint32_t));  // Leaks; that's ok.
  for (unsigned i = 0; i < num_members; ++i) {
    unsigned offset = read_little_long(contents);
    (*offsets)[i] = offset;
    contents += 4;
    printf("%u\n", offset);
  }

  unsigned num_symbols = read_little_long(contents);
  contents += 4;
  printf("Num symbols: %u\n", num_symbols);
  printf("Indices:\n");
  for (unsigned i = 0; i < num_symbols; ++i) {
    unsigned short index = read_little_short(contents);
    contents += 2;
    printf("%u\n", index);
  }

  printf("String table:\n");
  for (unsigned i = 0; i < num_symbols; ++i) {
    unsigned char* end = (unsigned char*)strchr((char*)contents, '\0');
    printf("%.*s\n", (int)(end - contents), contents);
    contents += end - contents + 1;
  }
  printf("\n");
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
  printf("real object\n");
  printf("Machine: 0x%x\n", object->Machine);
  printf("Num sections: %u\n", object->NumberOfSections);
  printf("TimeDateStamp: %u\n", object->TimeDateStamp);
}

// This is 20 bytes like a regular COFF File Header (sec 3.3), but the first
// 4 bytes are always 00 00 FF FF, which changes the meaning of all the
// following fields.
typedef struct {
  uint16_t Sig1;  // Must be 0 for import headers.
  uint16_t Sig2;  // Must be 0xffff for import headers.

  uint16_t Version;
  uint16_t Machine;
  uint32_t Timestamp;
  uint32_t Size;  // Excluding headers.
  uint16_t OrdinalOrHint;  // Depending on NameType.
  // Flags:
  // 2 bits Type
  // 3 bits NameType
  // 11 bits Reserved
  uint16_t Flags;
} ImportHeader;

static void dump_import_header(ImportHeader* import) {
  printf("import header\n");
}

static void dump_object(uint16_t* object) {
  if (object[0] == 0 && object[1] == 0xffff)
    dump_import_header((ImportHeader*)object);
  else
    dump_real_object((FileHeader*)object);
}

static void dump(unsigned char* contents, unsigned char* contents_end) {
  unsigned char* contents_start = contents;

  const char kLibMagic[] = "!<arch>\n";
  if (strncmp((char*)contents, kLibMagic, strlen(kLibMagic))) {
    fatal("File does not start with '%s', got '%.*s'.\n",
          kLibMagic, strlen(kLibMagic), contents);
  }
  contents += strlen(kLibMagic);

  uint32_t num_offsets, *offsets;
  ArchiveMemberHeader* first_linker = (ArchiveMemberHeader*)contents;
  printf("First linker header:\n");
  dump_header(first_linker);
  dump_first_header(contents + sizeof(*first_linker), &num_offsets, &offsets);
  contents += sizeof(*first_linker) + atoi(first_linker->Size);

  ArchiveMemberHeader* second_linker = (ArchiveMemberHeader*)contents;
  if (second_linker->Name[0] != '/' || second_linker->Name[1] == '/') {
    // The PE spec says this must always exist, but link-lld doesn't write it.
    printf("No second linker header.\n");
  } else {
    printf("Second linker header:\n");
    dump_header(second_linker);
    dump_second_header(
        contents + sizeof(*second_linker), &num_offsets, &offsets);
    contents += sizeof(*second_linker) + atoi(second_linker->Size);
  }

  ArchiveMemberHeader* longnames = (ArchiveMemberHeader*)contents;
  if (longnames->Name[0] != '/' || longnames->Name[1] != '/') {
    // The PE spec says this must always exist, but lib.exe doesn't always
    // write it.
    printf("No longnames header.\n");
  } else {
    printf("Longnames header:\n");
    dump_header(longnames);
    // XXX contents
    contents += sizeof(*longnames) + atoi(longnames->Size);
  }

  for (unsigned i = 0; i < num_offsets; ++i) {
    ArchiveMemberHeader* object =
        (ArchiveMemberHeader*)(contents_start + offsets[i]);
    dump_header(object);
    dump_object((uint16_t*)(object + 1));
  }
}

int main(int argc, char* argv[]) {
  if (argc != 2)
    fatal("Expected args == 2, got %d\n", argc);
  const char* in_name = argv[1];

  // Read input.
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
  uint8_t* lib_contents = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
  if (lib_contents == NULL)
    fatal("Failed to MapViewOfFile: %s\n", in_name);

  dump(lib_contents, lib_contents + size);

  UnmapViewOfFile(lib_contents);
  CloseHandle(mapping);
  CloseHandle(in_file);
}
