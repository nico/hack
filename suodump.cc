
/*
clang -o suodump suodump.c

Dumps suo files created by Visual Studio.
*/
#include <assert.h>
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

// [MS-CFB] spec is at
// https://docs.microsoft.com/en-us/openspecs/windows_protocols/ms-cfb/53989ce4-7b05-4f8d-829b-d08d6148375b

struct CFBHeader {
  // MUST be set to the value 0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1.
  uint8_t signature[8];

  uint8_t clsid[16];  // MUST be all zeroes.

  uint16_t version_minor;  // SHOULD be 0x003E when version_major is 3 or 4.
  uint16_t version_major;  // MUST be 3 or 4.

  uint16_t byte_order;  // MUST be 0xFFFE (implying little endian).
  uint16_t sector_shift;  // MUST be 9 if version is 3, 0xc if version is 12.


  uint16_t mini_sector_shift;  // MUST be 6.
  uint8_t reserved[6];  // MUST be all zeroes.

  uint32_t num_dir_sectors;  // MUST be 0 for version 3.

  uint32_t num_fat_sectors;

  uint32_t first_dir_sector_loc;

  uint32_t transaction_signature;

  uint32_t mini_stream_cutoff_size;  // MUST be 4096.

  uint32_t first_mini_fat_sector_loc;


  uint32_t num_mini_fat_sectors;

  uint32_t first_difat_sector_loc;

  uint32_t num_difat_sectors;

  uint32_t difat[109];
  // MUST be followed by (1 << sector_shift) - 512 bytes of zeroes.
};
static_assert(sizeof(CFBHeader) == 512, "");

// Storage object: Like a directory in a file system.
// Stream object: Like a file in a file system.
struct CFBDirEntry {
  uint16_t dir_entry_name[32];

  uint16_t dir_entry_name_len; // In bytes, MUST be multiple of 2.
  uint8_t object_type; // 0: unknown/unallocated, 1: storage, 2: stream, 5: root
  uint8_t color;  // 0: red, 1: black.

  uint32_t left_sibling_stream_id;
  uint32_t right_sibling_stream_id;
  uint32_t child_object_stream_id;

  // MUST be all zeroes for stream object; set for root or storage object.
  uint8_t clsid[16];

  uint32_t state_bits;

  // For root and stream object, this MUST be all zeroes.
  uint32_t creation_time_low;  // FILETIME
  uint32_t creation_time_high;  // FILETIME

  // For stream object, this MUST be all zeroes, for root it MAY be.
  uint32_t modification_time_low;  // FILETIME
  uint32_t modification_time_high;  // FILETIME

  // MUST be zeroes for storage object.
  // For root object, stores first sector of mini stream.
  // For steam object, location of first sector.
  uint32_t starting_sector_loc;

  // For version 3, this must be < 2 GiB, but it's recommended that
  // stream_size_high is treated as 0 even if it's not.
  uint32_t stream_size_low;
  uint32_t stream_size_high;
};
static_assert(sizeof(CFBDirEntry) == 128, "");

void dump_dir_entry(uint8_t* data, size_t size,
                    CFBHeader* header, uint32_t dir_num) {
  // XXX don't cast like this
  CFBDirEntry* entry = (CFBDirEntry*)(
      data + (dir_num + 1) * (1 << header->sector_shift));

  // XXX Validate.

  // Dump.
  printf("name: ");
  for (int i = 0; i < entry->dir_entry_name_len/2 - 1; i++)
    printf("%c", entry->dir_entry_name[i]);
  printf("\n");
  printf("object type: 0x%x\n", entry->object_type);
  printf("color: %d\n", entry->color);
  printf("left: 0x%x\n", entry->left_sibling_stream_id);
  printf("right: 0x%x\n", entry->right_sibling_stream_id);
  printf("child: 0x%x\n", entry->child_object_stream_id);
  printf(entry->object_type == 5 ? "mini stream sector: 0x%0x\n"
                                 : "starting sector: 0x%x\n",
         entry->starting_sector_loc);
  const char* size_type = entry->object_type == 5 ? "ministream " : "";
  if (header->version_major != 3 && entry->stream_size_high)
    printf("%ssize: 0x%x%08x\n",
           size_type, entry->stream_size_high, entry->stream_size_low);
  else
    printf("%ssize: 0x%x\n", size_type, entry->stream_size_low);
}

void dump_suo(uint8_t* data, size_t size) {
  if (size < 512)
    fatal("file too small\n");

  // XXX don't cast like this
  CFBHeader* header = (CFBHeader*)data;

  // Validate.
  const char kSig[] = "\xD0\xCF\x11\xE0\xA1\xB1\x1a\xE1";
  if (strcmp(kSig, (const char*)header->signature))
    fatal("invalid signature\n");

  for (int i = 0; i < 16; ++i)
    if (header->clsid[i] != 0)
      fatal("invalid clsid\n");

  if (header->byte_order != 0xFFFE)
    fatal("invalid byte order\n");

  if (header->version_major != 3 && header->version_major != 4)
    fatal("invalid version\n");

  if (!((header->version_major == 3 && header->sector_shift == 9) ||
        (header->version_major == 4 && header->sector_shift == 12)))
    fatal("invalid sector shift\n");

  if (header->mini_sector_shift != 6)
    fatal("invalid mini sector shift\n");

  for (int i = 0; i < 6; ++i)
    if (header->reserved[i] != 0)
      fatal("invalid reserved\n");

  if (header->version_major == 3 && header->num_dir_sectors != 0)
    fatal("invalid number of directory sectors\n");

  if (header->mini_stream_cutoff_size != 4096)
    fatal("invalid mini stream cutoff size\n");

  for (int i = sizeof(CFBHeader); i < 1 << header->sector_shift; ++i)
    if (data[i] != 0)
      fatal("invalid fill, %d %d\n", i, data[i]);

  // Dump.
  printf("version 0x%x.0x%x\n", header->version_major, header->version_minor);
  if (header->version_major == 4)
    printf("%d dir sectors\n", header->num_dir_sectors);
  printf("%d fat sectors\n", header->num_fat_sectors);
  printf("first dir sector loc 0x%x\n", header->first_dir_sector_loc);
  printf("transaction signature 0x%x\n", header->transaction_signature);
  printf("\n");
  printf("first mini fat sector loc 0x%x\n", header->first_mini_fat_sector_loc);
  printf("%d mini fat sectors\n", header->num_mini_fat_sectors);
  printf("\n");
  printf("first difat sector loc 0x%x\n", header->first_difat_sector_loc);
  printf("%d difat sectors\n", header->num_dir_sectors);
  printf("\n");
  dump_dir_entry(data, size, header, 0);
}

int main(int argc, char* argv[]) {
  if (argc != 2)
    fatal("Expected args == 2, got %d\n", argc);

  const char *in_name = argv[1];

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

  uint8_t* data = (uint8_t*)MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
  if (data == NULL)
    fatal("Failed to MapViewOfFile: %s\n", in_name);
#else
  int in_file = open(in_name, O_RDONLY);
  if (!in_file)
    fatal("Unable to read \'%s\'\n", in_name);

  struct stat in_stat;
  if (fstat(in_file, &in_stat))
    fatal("Failed to stat \'%s\'\n", in_name);

  size_t size = in_stat.st_size;
  uint8_t* data = mmap(/*addr=*/0, size, PROT_READ, MAP_SHARED, in_file,
                       /*offset=*/0);
  if (data == MAP_FAILED)
    fatal("Failed to mmap: %d (%s)\n", errno, strerror(errno));
#endif

  dump_suo(data, size);

#if defined(_WIN32)
  UnmapViewOfFile(data);
  CloseHandle(mapping);
  CloseHandle(in_file);
#else
  munmap(data, in_stat.st_size);
  close(in_file);
#endif
}
