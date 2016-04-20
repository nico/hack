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

unsigned int read_big_long(unsigned char* d) {
  return (d[0] << 24) | (d[1] << 16) | (d[2] << 8) | d[3];
}

unsigned int read_little_long(unsigned char* d) {
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

static void dump_first_header(unsigned char* contents) {
  unsigned num_symbols = read_big_long(contents);
  contents += 4;
  printf("Num symbols: %lu\n", num_symbols);
  printf("Offsets:\n");
  for (unsigned i = 0; i < num_symbols; ++i) {
    unsigned offset = read_big_long(contents);
    contents += 4;
    printf("%lu\n", offset);
  }

  printf("String table:\n");
  for (unsigned i = 0; i < num_symbols; ++i) {
    unsigned char* end = strchr(contents, '\0');
    printf("%.*s\n", end - contents, contents);
    contents += end - contents + 1;
  }
  printf("\n");
}

static void dump_second_header(unsigned char* contents) {
  unsigned num_members = read_little_long(contents);
  contents += 4;
  printf("Num members: %lu\n", num_members);
  printf("Offsets:\n");
  for (unsigned i = 0; i < num_members; ++i) {
    unsigned offset = read_little_long(contents);
    contents += 4;
    printf("%lu\n", offset);
  }

  unsigned num_symbols = read_little_long(contents);
  contents += 4;
  printf("Num symbols: %lu\n", num_symbols);
  printf("Indices:\n");
  for (unsigned i = 0; i < num_symbols; ++i) {
    unsigned short index = read_little_short(contents);
    contents += 2;
    printf("%lu\n", index);
  }

  printf("String table:\n");
  for (unsigned i = 0; i < num_symbols; ++i) {
    unsigned char* end = strchr(contents, '\0');
    printf("%.*s\n", end - contents, contents);
    contents += end - contents + 1;
  }
  printf("\n");
}

static void dump(unsigned char* contents) {
  //void* file_start = contents;
  const char kLibMagic[] = "!<arch>\n";
  if (strncmp(contents, kLibMagic, strlen(kLibMagic))) {
    fatal("File does not start with '%s', got '%.*s'.\n",
          kLibMagic, strlen(kLibMagic), contents);
  }
  contents += strlen(kLibMagic);

  ArchiveMemberHeader* first_linker = (ArchiveMemberHeader*)contents;
  printf("First linker header:\n");
  dump_header(first_linker);
  dump_first_header(contents + sizeof(*first_linker));
  contents += sizeof(*first_linker) + atoi(first_linker->Size);

  ArchiveMemberHeader* second_linker = (ArchiveMemberHeader*)contents;
  printf("Second linker header:\n");
  dump_header(second_linker);
  dump_second_header(contents + sizeof(*second_linker));
  contents += sizeof(*second_linker) + atoi(second_linker->Size);

  ArchiveMemberHeader* longnames = (ArchiveMemberHeader*)contents;
  ArchiveMemberHeader* first_object;
  if (strcmp(longnames->Name, "//")) {
    // The PE spec says this must always exist, but lib.exe doesn't always
    // write it.
    first_object = longnames;
  } else {
    printf("Longnames header:\n");
    dump_header(longnames);
    // XXX contents
    contents += sizeof(*longnames) + atoi(longnames->Size);
    first_object = (ArchiveMemberHeader*)contents;
  }

  // XXX actual archives
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

  HANDLE mapping = CreateFileMapping(in_file, NULL, PAGE_READONLY, 0, 0, NULL);
  if (mapping == NULL)
    fatal("Unable to map \'%s\'\n", in_name);

  // Casting memory like this is not portable.
  void* lib_contents = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
  if (lib_contents == NULL)
    fatal("Failed to MapViewOfFile: %s\n", in_name);

  dump(lib_contents);

  UnmapViewOfFile(lib_contents);
  CloseHandle(mapping);
  CloseHandle(in_file);
}
