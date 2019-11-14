
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

// [MS-CFB] spec is at
// https://docs.microsoft.com/en-us/openspecs/windows_protocols/ms-cfb/53989ce4-7b05-4f8d-829b-d08d6148375b

// Summary:
// - File is a sequence of 512 / 4096 byte blocks called "sectors"
// - First sector stores CFB header; the sector _after_ it is sector 0, then
//   sectors are named 1, 2, ... after that.
// - Sectors can be chained, where an array called "FAT" stores at index n
//   the next sector after sector n; entry 0xfffffffe ends a chain.
// - The FAT array is stored in several sectors if needed, an array called
//   DIFAT stores at DIFAT[n] where the n'th sector storing the FAT is at.
//   The first 109 DIFAT entries are in the header, the location of the first
//   dedicated DIFAT sector is also in the header, and the last entry in a
//   DIFAT sector stores the next sector in the DIFAT chain.

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

void dump_dir_entry(const std::vector<CFBDirEntry*>& dir_entries,
                    CFBHeader* header, uint32_t dir_num, int indent) {
  CFBDirEntry* entry = dir_entries[dir_num];

  // Validate.
  if (entry->dir_entry_name_len % 2 || entry->dir_entry_name_len > 64)
    fatal("invalid entry name length\n");
  if (entry->dir_entry_name[entry->dir_entry_name_len/2 - 1] != 0)
    fatal("entry name length not zero terminated\n");
  for (int i = 0; i < entry->dir_entry_name_len/2; i++) {
    uint16_t c = entry->dir_entry_name[i];
    if (c == '/' || c == '\\' || c == ':' || c == '!')
      fatal("invalid entry name\n");
  }

  if (entry->object_type != 0 && entry->object_type != 1 &&
      entry->object_type != 2 && entry->object_type != 5)
    fatal("invalid object type\n");

  if (entry->color != 0 && entry->color != 1)
    fatal("invalid color\n");

  if (entry->object_type == 1 && entry->starting_sector_loc != 0)
    fatal("non-0 starting sector for storage object\n");

  uint64_t stream_size = entry->stream_size_low;
  if (header->version_major == 4)
    stream_size |= static_cast<uint64_t>(entry->stream_size_high) << 32;
  if (entry->object_type == 1 && stream_size != 0)
    fatal("non-0 stream size for storage object\n");

  // XXX check left/right/child id validity
  // XXX check CLSID all zeroes for stream object
  // XXX check creation time zero for root object
  // XXX check file stream_size_low <= 0x80000000 for version_major == 3

  // Dump.
  if (entry->left_sibling_stream_id != 0xffffffff)
    dump_dir_entry(dir_entries, header, entry->left_sibling_stream_id, indent);
  printf("%*sid: 0x%x\n", indent, "", dir_num);
  printf("%*sname: ", indent, "");
  for (int i = 0; i < entry->dir_entry_name_len/2 - 1; i++)
    printf("%c", entry->dir_entry_name[i]);
  printf("\n");
  printf("%*sobject type: 0x%x\n", indent, "", entry->object_type);
  printf("%*scolor: %d\n", indent, "", entry->color);
  printf("%*sleft: 0x%x\n", indent, "", entry->left_sibling_stream_id);
  printf("%*sright: 0x%x\n", indent, "", entry->right_sibling_stream_id);
  printf("%*schild: 0x%x\n", indent, "", entry->child_object_stream_id);
  printf(entry->object_type == 5 ? "%*smini stream sector: 0x%0x\n"
                                 : "%*sstarting sector: 0x%x\n",
         indent, "", entry->starting_sector_loc);
  const char* size_type = entry->object_type == 5 ? "ministream " : "";
  printf("%*s%ssize: 0x%" PRIx64 "\n", indent, "", size_type, stream_size);
  printf("\n");
  if (entry->child_object_stream_id != 0xffffffff)
    dump_dir_entry(dir_entries, header, entry->child_object_stream_id,
                   indent + 2);
  if (entry->right_sibling_stream_id != 0xffffffff)
    dump_dir_entry(dir_entries, header, entry->right_sibling_stream_id, indent);
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

  if (size < 1 << header->sector_shift)
    fatal("file too small\n");
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
  printf("%d difat sectors\n", header->num_difat_sectors);
  printf("\n");

  // Version 3 has 512 byte sectors, and a max file size of 2 GiB. So the FAT
  // has at most 4 * 2**20 entries (unless there are lots of FREESECT entries)
  // and is hence (each entry is an uint32_t) at most 16 MiB, and the DIFAT has
  // at most (4 * 2**20 - 109) / 127 uint32_t entries and is at most 129 kiB.
  // Version 4 has 4096 byte sectors, so there roughly the same is true for
  // 16 GiB files.
  // So just keep FAT and DIFAT in memory instead of doing anything clever.

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

  // Validate FAT / DIFAT a bit.
  // DIFAT sectors should have 0xfffffffc entries in the FAT.
  std::set<uint32_t> difat_sectors;
  for (uint32_t next_difat = header->first_difat_sector_loc;
      next_difat != 0xfffffffe;) {
    difat_sectors.insert(next_difat);
    if (FAT[next_difat] != 0xfffffffc)
      fatal("invalid fat/difat\n");
    uint32_t* difat_data =
      (uint32_t*)(data + (next_difat + 1)*(1 << header->sector_shift));
    next_difat = difat_data[entries_per_difat_sector];
  }
  // FAT sectors should have 0xfffffffd in the FAT.
  std::set<uint32_t> fat_sectors;
  for (uint32_t i = 0; i < header->num_fat_sectors; ++i) {
    uint32_t fat_sector = DIFAT[i];
    fat_sectors.insert(fat_sector);
    if (FAT[fat_sector] != 0xfffffffd)
      fatal("invalid fat\n");
  }
  // Nothing else should have 0xfffffffc / 0xfffffffd.
  for (size_t i = 0; i < FAT.size(); ++i) {
    uint32_t F = FAT[i];
    if (F == 0xfffffffc && !difat_sectors.count(static_cast<uint32_t>(i)))
      fatal("0xfffffffc fat entry not actually part of difat\n");
    if (F == 0xfffffffd && !fat_sectors.count(static_cast<uint32_t>(i)))
      fatal("0xfffffffd fat entry not actually part of fat\n");
  }

  // XXX could add checks like number of distinct sector chains, checking that
  // there are no loops and that each sector is part of just a single chain,
  // and that there's exactly one directory entry pointing to the root of
  // every chain.

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

  dump_dir_entry(dir_entries, header, 0, 0);
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
