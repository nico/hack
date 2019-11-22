/*
clang-cl -O2 /W4 -Wconversion DebuggerFindSource_dump.cc

Dumps the DebuggerFindSource data in an MSVC .suo file.
 */
#include <assert.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <set>
#include <vector>

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

// See suodump.cc in this directory for .suo file format information.

struct CFBHeader {
  // 0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1.
  uint8_t signature[8];
  uint8_t clsid[16];

  uint16_t version_minor;
  uint16_t version_major;
  uint16_t byte_order;

  uint16_t sector_shift;
  uint16_t mini_sector_shift;

  uint8_t reserved[6];

  uint32_t num_dir_sectors;  // 0 for version 3.
  uint32_t num_fat_sectors;

  uint32_t first_dir_sector_loc;

  uint32_t transaction_signature;

  uint32_t mini_stream_cutoff_size;

  uint32_t first_mini_fat_sector_loc;
  uint32_t num_mini_fat_sectors;

  uint32_t first_difat_sector_loc;
  uint32_t num_difat_sectors;

  uint32_t difat[109];
};
static_assert(sizeof(CFBHeader) == 512, "");

struct CFBDirEntry {
  uint16_t dir_entry_name[32];
  uint16_t dir_entry_name_len;

  uint8_t object_type; // 0: unknown/unallocated, 1: storage, 2: stream, 5: root
  uint8_t color;

  uint32_t left_sibling_stream_id;
  uint32_t right_sibling_stream_id;
  uint32_t child_object_stream_id;

  uint8_t clsid[16];
  uint32_t state_bits;

  uint32_t creation_time_low;
  uint32_t creation_time_high;

  uint32_t modification_time_low;
  uint32_t modification_time_high;

  uint32_t starting_sector_loc;

  uint32_t stream_size_low;
  uint32_t stream_size_high;
};
static_assert(sizeof(CFBDirEntry) == 128, "");

int CFBDirEntryKeyCompare(uint32_t l_name_len, const char* l_name,
                           uint32_t r_name_len, const char* r_name) {
  // See '2.6.4 Red-Black Tree' in [MS-CFB].pdf for a description of this
  // function.
  // This implementation only supports ascii in utf-16le names.
  // Since the search key ("DebuggerFindSource") is known to be ascii, this is
  // fine even if the keys in the file aren't.
  if (l_name_len != r_name_len)
    return l_name_len < r_name_len ? -1 : 1;
  for (uint32_t i = 0; i < l_name_len; i += 2)
    if (toupper(l_name[i]) != toupper(r_name[i]))
      return toupper(l_name[i]) < toupper(r_name[i]) ? -1 : 1;
  return 0;
}

CFBDirEntry *find_node(
    uint32_t name_len,  // in bytes
    const char* name,  // in utf-16le
    uint32_t dir_num,
    const std::vector<CFBDirEntry*>& dir_entries) {
  if (dir_num >= dir_entries.size())
    fatal("invalid stream id\n");
  CFBDirEntry* entry = dir_entries[dir_num];

  // Validate.
  if (entry->dir_entry_name_len % 2 || entry->dir_entry_name_len > 64)
    fatal("invalid entry name length\n");
  if (entry->dir_entry_name[entry->dir_entry_name_len/2 - 1] != 0)
    fatal("entry name length not zero terminated\n");

  int cmp = CFBDirEntryKeyCompare(
      name_len, name,
      entry->dir_entry_name_len, (const char*)entry->dir_entry_name);

#if 0
  printf("name: ");
  for (int i = 0; i < entry->dir_entry_name_len/2 - 1; i++)
    printf("%c", entry->dir_entry_name[i]);
  printf("\n");
  printf("compare: %d\n", cmp);
#endif

  if (cmp < 0)
    return find_node(
        name_len, name, entry->left_sibling_stream_id, dir_entries);
  if (cmp > 0)
    return find_node(
        name_len, name, entry->right_sibling_stream_id, dir_entries);

  return entry;
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

  if (header->byte_order != 0xFFFE)
    fatal("invalid byte order\n");

  if (header->version_major != 3 && header->version_major != 4)
    fatal("invalid version\n");

  if (!((header->version_major == 3 && header->sector_shift == 9) ||
        (header->version_major == 4 && header->sector_shift == 12)))
    fatal("invalid sector shift\n");

  if (header->mini_sector_shift != 6)
    fatal("invalid mini sector shift\n");

  if (header->version_major == 3 && header->num_dir_sectors != 0)
    fatal("invalid number of directory sectors\n");

  if (header->mini_stream_cutoff_size != 4096)
    fatal("invalid mini stream cutoff size\n");

  if (size < 1 << header->sector_shift)
    fatal("file too small\n");

  // Read DIFAT.
  uint32_t entries_per_difat_sector = (1 << (header->sector_shift - 2)) - 1;
  std::vector<uint32_t> DIFAT;
  DIFAT.reserve(109 + header->num_difat_sectors*entries_per_difat_sector);
  DIFAT.insert(
      DIFAT.end(), &header->difat[0], &header->difat[109]);
  uint32_t next_difat_sector = header->first_difat_sector_loc;
  for (uint32_t i = 0; i < header->num_difat_sectors; ++i) {
    if (next_difat_sector > 0xfffffffa)
      fatal("invalid difat\n");
    if ((next_difat_sector + 2)*(1 << header->sector_shift) > size)
      fatal("file too small\n");
    uint32_t* difat_data =
      (uint32_t*)(data + (next_difat_sector + 1)*(1 << header->sector_shift));
    DIFAT.insert(
        DIFAT.end(), difat_data, difat_data + entries_per_difat_sector);
    next_difat_sector = difat_data[entries_per_difat_sector];
  }
  if (next_difat_sector != 0xfffffffe)
    fatal("invalid difat\n");

  // Read FAT.
  std::vector<uint32_t> FAT;
  uint32_t entries_per_fat_sector = (1 << (header->sector_shift - 2));
  FAT.reserve(header->num_fat_sectors * entries_per_fat_sector);
  for (uint32_t i = 0; i < header->num_fat_sectors; ++i) {
    if (i >= DIFAT.size())
      fatal("invalid fat/difat\n");
    uint32_t fat_sector = DIFAT[i];
    if (fat_sector > 0xfffffffa)
      fatal("invalid fat/difat\n");
    if ((fat_sector + 2)*(1 << header->sector_shift) > size)
      fatal("file too small\n");
    uint32_t* fat_data =
      (uint32_t*)(data + (fat_sector + 1)*(1 << header->sector_shift));
    FAT.insert(
        FAT.end(), fat_data, fat_data + entries_per_fat_sector);
  }

  // Read Mini FAT. (Ministream start sector and size is in root dir entry.)
  // XXX mention that it's not super reasonable to keep this in ram, and why.
  std::vector<uint32_t> mini_FAT;
  uint32_t minifat_sector = header->first_mini_fat_sector_loc;
  for (uint32_t i = 0; i < header->num_mini_fat_sectors; ++i) {
    if (minifat_sector > 0xfffffffa)
      fatal("invalid minifat\n");
    if ((minifat_sector + 2)*(1 << header->sector_shift) > size)
      fatal("file too small\n");
    uint32_t* minifat_data =
      (uint32_t*)(data + (minifat_sector + 1)*(1 << header->sector_shift));
    mini_FAT.insert(
        mini_FAT.end(), minifat_data, minifat_data + entries_per_fat_sector);
    if (minifat_sector >= FAT.size())
      fatal("invalid FAT");
    minifat_sector = FAT[minifat_sector];
  }
  if (minifat_sector != 0xfffffffe)
    fatal("invalid minifat\n");

  // Collect directory entries. These could make up more than half the total
  // file size in theory, and a measurable fraction of it in practice, so
  // just loading the whole directory into memory is less sound than doing that
  // for the DIFAT and FAT. Even if it's just pointers: For a 2 GiB v3 file,
  // this could be several 100 MiB of pointers.
  std::vector<CFBDirEntry*> dir_entries;
  uint32_t dir_entry_sector = header->first_dir_sector_loc;
  while (dir_entry_sector != 0xfffffffe) {
    if ((dir_entry_sector + 2)*(1 << header->sector_shift) > size)
      fatal("file too small\n");
    // XXX don't cast like this
    CFBDirEntry* entry_data =
      (CFBDirEntry*)(data + (dir_entry_sector + 1)*(1 << header->sector_shift));
    for (int i = 0; i < 1 << (header->sector_shift - 7); ++i)
      dir_entries.push_back(entry_data + i);

    if (dir_entry_sector >= FAT.size())
      fatal("invalid FAT");
    dir_entry_sector = FAT[dir_entry_sector];
  }
  if (dir_entries.empty())
    fatal("missing dir entries\n");

  CFBDirEntry *root = dir_entries[0];
  if (root->dir_entry_name_len != 22 ||
      strncmp("R\0o\0o\0t\0 \0E\0n\0t\0r\0y\0\0\0",
              (const char*)root->dir_entry_name, 22))
    fatal("invalid root entry name\n");

  std::vector<uint32_t> ministream;
  uint32_t ministream_sector = root->starting_sector_loc;
  while (ministream_sector != 0xfffffffe) {
    if (ministream_sector > 0xfffffffa)
      fatal("invalid ministream\n");

    ministream.push_back(ministream_sector);

    if ((ministream_sector + 2)*(1 << header->sector_shift) > size)
      fatal("file too small\n");
    if (ministream_sector >= FAT.size())
      fatal("invalid FAT");
    ministream_sector = FAT[ministream_sector];
  }
  if (ministream_sector != 0xfffffffe)
    fatal("invalid ministream\n");
  // XXX check that round(root->stream_size / sector_size) == ministream.size()

  const char kKey[] =
      "D\0e\0b\0u\0g\0g\0e\0r\0F\0i\0n\0d\0S\0o\0u\0r\0c\0e\0\0";
  CFBDirEntry *debug_node = find_node(
      static_cast<uint32_t>(sizeof(kKey)), kKey,
      root->child_object_stream_id, dir_entries);
  if (!data)
    fatal("did not find node\n");

  if (debug_node->object_type != 2)
    fatal("unexpected DebuggerFindSource node type\n");

  uint64_t stream_size = debug_node->stream_size_low;
  if (header->version_major == 4)
    stream_size |= static_cast<uint64_t>(debug_node->stream_size_high) << 32;

  std::vector<uint8_t> debug_data;
  debug_data.reserve(stream_size);  // XXX round up
  if (stream_size > header->mini_stream_cutoff_size) {
    // Data is in regular FAT sectors.
    uint32_t sector = debug_node->starting_sector_loc;
    while (sector != 0xfffffffe) {
      if ((sector + 2)*(1 << header->sector_shift) > size)
        fatal("file too small\n");

      uint8_t* sector_data = data + (sector + 1)*(1 << header->sector_shift);
      debug_data.insert(
          debug_data.end(),
          sector_data,
          sector_data + (1 << header->sector_shift));
      if (sector >= FAT.size())
        fatal("invalid FAT");
      sector = FAT[sector];
    }
    if (debug_data.size() < stream_size)
      fatal("not enough debug data in stream\n");
  } else {
    // Data is in the ministream.
    uint32_t sector = debug_node->starting_sector_loc;
    uint32_t ministream_sectors_per_sector =
      1 << (header->sector_shift - header->mini_sector_shift);
    uint32_t full_sector_size = 1 << header->sector_shift;
    while (sector != 0xfffffffe) {
      uint32_t fat_sector = ministream[sector / ministream_sectors_per_sector];
      uint32_t start =
          (fat_sector + 1) * full_sector_size +
          (sector % ministream_sectors_per_sector) *
              (1 << header->mini_sector_shift);

      if (start + (1 << header->mini_sector_shift) > size)
        fatal("file too small\n");

      debug_data.insert(
          debug_data.end(),
          data + start,
          data + start + (1 << header->mini_sector_shift));

      if (sector >= FAT.size())
        fatal("invalid FAT");
      sector = mini_FAT[sector];
    }
    if (debug_data.size() < stream_size)
      fatal("not enough debug data in stream\n");
  }

#if 0
  FILE* f = fopen("tmp.dmp", "wb");
  fwrite(debug_data.data(), 1, debug_data.size(), f);
  fclose(f);
#endif

  // According to
  // https://microsoft.public.vstudio.extensibility.narkive.com/ONn7ZAb3/accessing-sln-properties-in-common-properties-debug-source-files
  // the format of the DebuggerFindSource stream is:
  // uint32_t verDirCache (0)
  // uint32_t verStringList (0)
  // uint32_t num source dirs
  // For each source dir:
  //   uint32_t numBytes
  //   That many bytes UTF-16LE dir
  // uint32_t verStringList;
  // uint32_t num ignored files
  // For each ignored file:
  //   uint32_t numBytes
  //   That many bytes UTF-16LE file
  // XXX don't cast like this
  uint8_t* debug_data_p = debug_data.data();
  uint32_t verDirCache = read_little_long(&debug_data_p);
  uint32_t verStringList = read_little_long(&debug_data_p);
  printf("verDirCache: %d\n", verDirCache);
  printf("source dirs verStringList: %d\n", verStringList);
  if (verDirCache != 0 || verStringList != 0)
    return;
  printf("Source dirs:\n");
  uint32_t num_source_dirs = read_little_long(&debug_data_p);
  for (uint32_t i = 0; i < num_source_dirs; ++i) {
    // In bytes, including nul.
    uint32_t str_len = read_little_long(&debug_data_p);
    if (str_len % 2)
      fatal("unexpected string length\n");
    for (uint32_t j = 0; j < str_len; j += 2) {
      uint16_t c = read_little_short(&debug_data_p);
      if (j + 2 != str_len)  // Skip nul;
        printf("%c", c);
    }
    printf("\n");
  }
  uint32_t verStringList2 = read_little_long(&debug_data_p);
  printf("ignored files verStringList: %d\n", verStringList2);
  if (verStringList2 != 0)
    return;
  printf("Ignored files:\n");
  uint32_t num_ignored_files = read_little_long(&debug_data_p);
  for (uint32_t i = 0; i < num_ignored_files; ++i) {
    // In bytes, including nul.
    uint32_t str_len = read_little_long(&debug_data_p);
    if (str_len % 2)
      fatal("unexpected string length\n");
    for (uint32_t j = 0; j < str_len; j += 2) {
      uint16_t c = read_little_short(&debug_data_p);
      if (j + 2 != str_len)  // Skip nul;
        printf("%c", c);
    }
    printf("\n");
  }
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
