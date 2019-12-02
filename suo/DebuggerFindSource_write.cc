/*
clang-cl -O2 /W4 -Wconversion DebuggerFindSource_write.cc
clang++ -std=c++17 -O2 -Wall -Wextra -Wconversion DebuggerFindSource_write.cc \
    -o DebuggerFindSource_write

Writes an MSVC .suo file with a DebuggerFindSource entry.
 */

// Need to write:
// - Header block
// - Depending on size of list, FAT or ministream data for DebuggerFindSource
//   data
// - Directory entries for Root Entry and DebuggerFindSource (always fits in
//   1 sector).
// - If needed, ministream and minifat
// - FAT
// - DIFAT

#include <stdint.h>

#include <iterator>
#include <vector>

void store_little_long(uint32_t v, std::vector<uint8_t>* data) {
  data->push_back(v & 0xff);
  data->push_back((v >> 8) & 0xff);
  data->push_back((v >> 16) & 0xff);
  data->push_back((v >> 24) & 0xff);
}

std::vector<uint8_t> debug_data() {
  const char* kDirectories[] = {
    "c:\\src\\chrome\\src\\",
    "c:\\src\\llvm-project\\",
  };

  const char* kBlockedFiles[] = {
    "foo.cc",
  };

  std::vector<uint8_t> data;

  store_little_long(0, &data);  // verDirCache

  store_little_long(0, &data);  // verStringList
  store_little_long(std::size(kDirectories), &data);
  for (const char* dir : kDirectories) {
    uint32_t l = static_cast<uint32_t>(strlen(dir));
    store_little_long(2 * l + 2, &data);
    for (uint32_t i = 0; i < l; ++i) {
      data.push_back(static_cast<uint8_t>(dir[i]));
      data.push_back('\0');
    }
    data.push_back('\0');
    data.push_back('\0');
  }

  store_little_long(0, &data);  // verStringList
  store_little_long(std::size(kBlockedFiles), &data);
  for (const char* file : kBlockedFiles) {
    uint32_t l = static_cast<uint32_t>(strlen(file));
    store_little_long(2 * l + 2, &data);
    for (uint32_t i = 0; i < l; ++i) {
      data.push_back(static_cast<uint8_t>(file[i]));
      data.push_back('\0');
    }
    data.push_back('\0');
    data.push_back('\0');
  }

  return data;
}

uint32_t align(uint32_t n, uint32_t align) {
  return (n + align - 1) / align;
}

void compute_blocks(uint32_t n_blocks,
                    uint32_t sector_size,
                    uint32_t* out_n_difat,
                    uint32_t* out_n_fat) {
  // Every FAT block can store 128 blocks (assuming 512 byte sectors), but one
  // of them is needed for the FAT block itself, so 127. (Actually, a bit less,
  // because of the DIFAT blocks.)
  uint32_t uint_count = sector_size / 4;
  uint32_t n_fat = align(n_blocks, uint_count - 1), n_difat;
  while (true) {
    n_difat = n_fat > 109 ? n_difat = align(n_fat - 109, uint_count - 1) : 0;
    uint32_t n_fat_prime = align((n_blocks + n_difat), uint_count - 1);
    if (n_fat_prime == n_fat)
      break;
    n_fat = n_fat_prime;
  }
  *out_n_difat = n_difat;
  *out_n_fat = n_fat;
}

void write_little_short(uint16_t v, FILE* f) {
  fputc(v & 0xff, f);
  fputc((v >> 8)& 0xff, f);
}

void write_little_long(uint32_t v, FILE* f) {
  fputc(v & 0xff, f);
  fputc((v >> 8)& 0xff, f);
  fputc((v >> 16)& 0xff, f);
  fputc((v >> 24)& 0xff, f);
}

int main(int argc, char* argv[]) {
  std::vector<uint8_t> data = debug_data();

  const uint32_t kMinistreamCutoffSize = 4096;

  const uint16_t kSectorShift = 9;
  const uint16_t kMiniSectorShift = 6;

  const int kSectorSize = 1 << kSectorShift;
  const int kMiniSectorSize = 1 << kMiniSectorShift;

  const int kNumDirectorySectors = 1;

  const uint32_t kEndOfList = 0xfffffffe;

  // Sector layout.
  // Order:
  // DIFAT, FAT, directory entries, mini FAT (if applicable), data

  uint32_t n_difat, n_fat;
  uint32_t first_mini_fat_sector_loc = kEndOfList;
  uint32_t num_minifat_sectors = 0;

  uint32_t data_size = static_cast<uint32_t>(data.size());
  if (data_size > kMinistreamCutoffSize) {
    // Store data in regular sectors.
    uint32_t num_data_sectors = align(data_size, kSectorSize);

    uint32_t num_sectors = kNumDirectorySectors + num_data_sectors;
    compute_blocks(num_sectors, kSectorSize, &n_difat, &n_fat);
  } else {
    // Store data in ministream.
    uint32_t num_data_mini_sectors = align(data_size, kMiniSectorSize);
    num_minifat_sectors = align(4 * num_data_mini_sectors, kSectorSize);
    uint32_t num_ministream_sectors = align(data_size, kSectorSize);

    uint32_t num_sectors =
        kNumDirectorySectors + num_minifat_sectors + num_ministream_sectors;
    compute_blocks(num_sectors, kSectorSize, &n_difat, &n_fat);

    first_mini_fat_sector_loc = n_difat + n_fat + kNumDirectorySectors;
  }

  uint32_t first_dir_sector_loc = n_difat + n_fat;

  // Write output.
  if (argc != 2) {
    fprintf(stderr, "Expected args == 2, got %d\n", argc);
    return 1;
  }

  const char *out_name = argv[1];
  // XXX Write to temp, rename at end (?)
  FILE* out = fopen(out_name, "wb");

  // Header.
  fwrite("\xD0\xCF\x11\xE0\xA1\xB1\x1a\xE1", 8, 1, out);  // signature
  for (int i = 0; i < 16; ++i) fputc(0, out);             // clsid
  write_little_short(0x3e, out);                          // version_minor
  write_little_short(0x3, out);                           // version_major
  write_little_short(0xfffe, out);                        // byte_order
  write_little_short(kSectorShift, out);
  write_little_short(kMiniSectorShift, out);
  for (int i = 0; i < 6; ++i) fputc(0, out);              // reserved
  write_little_long(0, out);  // num_difat_sectors, must be 0 for version 3
  write_little_long(n_fat, out);
  write_little_long(first_dir_sector_loc, out);
  write_little_long(0, out);                            // transaction_signature
  write_little_long(kMinistreamCutoffSize, out);
  write_little_long(first_mini_fat_sector_loc, out);
  write_little_long(num_minifat_sectors, out);
  if (n_difat > 0)                                     // first_difat_sector_loc
    write_little_long(0, out);
  else
    write_little_long(kEndOfList, out);
  write_little_long(n_difat, out);
  for (uint32_t i = 0; i < 109; ++i)
    write_little_long(i < n_fat ? n_difat + i : 0, out);

  // DIFAT sectors, if necessary.
  for (uint32_t i = 0; i < n_difat; ++i) {
    for (uint32_t j = 0; j < 127; ++j) {
      uint32_t n = 109 + i * 127 + j;
      write_little_long(n < n_fat ? n_difat + n : 0, out);
    }
    write_little_long(i + 1 == n_difat ? kEndOfList : i + 1, out);
  }

  fclose(out);
}
