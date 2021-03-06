/*
clang-cl -O2 /W4 -Wconversion DebuggerFindSource_write.cc /Zi ^
    /D_CRT_SECURE_NO_DEPRECATE
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

#ifdef _WIN32
#include <errno.h>
#include <io.h>
#endif

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

void write_directory_entry(const char* dir_entry_name,
                           uint16_t dir_entry_name_len,
                           uint8_t object_type,
                           uint32_t child_object_stream_id,
                           uint32_t first_data_sector_loc,
                           uint32_t data_size,
                           FILE* out) {
  const uint32_t kNoChild = 0xffffffff;
  fwrite(dir_entry_name, dir_entry_name_len, 1, out);  // dir_entry_name
  for (int i = 0; i < 64 - dir_entry_name_len; ++i)
    fputc(0, out);                             // pad remaining dir_entry_name
  write_little_short(dir_entry_name_len, out);
  fputc(object_type, out);
  fputc(1, out);                               // color (1: black)
  write_little_long(kNoChild, out);            // left_sibling_stream_id
  write_little_long(kNoChild, out);            // right_sibling_stream_id
  write_little_long(child_object_stream_id, out);
  for (int i = 0; i < 16; ++i) fputc(0, out);  // clsid
  write_little_long(0, out);                   // state_bits
  write_little_long(0, out);                   // creation_time_low
  write_little_long(0, out);                   // creation_time_high
  write_little_long(0, out);                   // modification_time_low
  write_little_long(0, out);                   // modification_time_high
  write_little_long(first_data_sector_loc, out);
  write_little_long(data_size, out);           // stream_size_low
  write_little_long(0, out);  // stream_size_high, must be 0 for version 3 files
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

  // Number of (normal) sectors needed to store the mini FAT.
  uint32_t num_minifat_sectors = 0;

  // Number of mini sectors in the ministream.
  uint32_t num_data_mini_sectors = 0;

  // Number of (normal) sectors used to store data. Either stores the
  // actual data, or the ministream's data and the actual data is in the
  // ministream. (In either case this stores the actual data in a way.)
  uint32_t num_data_sectors;

  uint32_t data_size = static_cast<uint32_t>(data.size());
  if (data_size > kMinistreamCutoffSize) {
    // Store data in regular sectors.
    num_data_sectors = align(data_size, kSectorSize);

    uint32_t num_sectors = kNumDirectorySectors + num_data_sectors;
    compute_blocks(num_sectors, kSectorSize, &n_difat, &n_fat);
  } else {
    // Store data in ministream.
    num_data_mini_sectors = align(data_size, kMiniSectorSize);
    num_minifat_sectors = align(4 * num_data_mini_sectors, kSectorSize);
    uint32_t num_ministream_sectors = align(data_size, kSectorSize);
    num_data_sectors = num_ministream_sectors;

    uint32_t num_sectors =
        kNumDirectorySectors + num_minifat_sectors + num_ministream_sectors;
    compute_blocks(num_sectors, kSectorSize, &n_difat, &n_fat);

    first_mini_fat_sector_loc = n_difat + n_fat + kNumDirectorySectors;
  }

  uint32_t first_dir_sector_loc = n_difat + n_fat;

  uint32_t first_data_sector_loc =
      n_difat + n_fat + kNumDirectorySectors + num_minifat_sectors;

  // Write output.
  if (argc != 2) {
    fprintf(stderr, "Expected args == 2, got %d\n", argc);
    return 1;
  }

  const char *out_name = argv[1];
  // XXX Write to temp, rename at end (?)
  FILE* out = fopen(out_name, "wb");
  if (!out) {
#ifdef _WIN32
    // MSVC makes its main .suo file a hidden file, and those can't be
    // written to in mode "w" (try `echo hi > hidden.txt` in cmd).
    // Instead, open as "r+" and then truncate.
    if (errno == EACCES) {
      out = fopen(out_name, "r+b");
      if (!out) {
        fprintf(stderr, "failed to open '%s': %s\n", out_name, strerror(errno));
        return 1;
      }
      if (_chsize(_fileno(out), 0) != 0) {
        fprintf(stderr, "failed to _chsize '%s': %s\n",
                out_name, strerror(errno));
        return 1;
      }
    }
#else
    fprintf(stderr, "failed to open '%s': %s\n", out_name, strerror(errno));
    return 1;
#endif
  }

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

  // FAT sectors.
  const uint32_t kDifat = 0xfffffffc;
  const uint32_t kFat = 0xfffffffd;
  const uint32_t kFree = 0xffffffff;
  for (uint32_t i = 0; i < n_fat; ++i) {
    for (uint32_t j = 0; j < 128; ++j) {
      uint32_t n = i * 128 + j;
      if (n < n_difat) {
        // DIFAT sectors.
        write_little_long(kDifat, out);
        continue;
      }
      n -= n_difat;
      if (n < n_fat) {
        // FAT sectors.
        write_little_long(kFat, out);
        continue;
      }
      n -= n_fat;
      if (n < kNumDirectorySectors) {
        // Directory entry sectors.
        write_little_long(n + 1 == kNumDirectorySectors ? kEndOfList : n + 1,
                          out);
        continue;
      }
      n -= kNumDirectorySectors;
      if (n  < num_minifat_sectors) {
        // Mini FAT sectors.
        write_little_long(n + 1 == num_minifat_sectors ? kEndOfList : n + 1,
                          out);
        continue;
      }
      n -= num_minifat_sectors;
      if (n < num_data_sectors) {
        // Main data stream sectors (either full stream, or mini stream).
        write_little_long(n + 1 == num_data_sectors ? kEndOfList : n + 1, out);
        continue;
      }
      write_little_long(kFree, out);
    }
  }

  // Directory entries.
  // First entry, root object.
  const uint32_t kNoChild = 0xffffffff;
  write_directory_entry(
      "R\0o\0o\0t\0 \0E\0n\0t\0r\0y\0\0\0", 22,
      /*object_type=*/5 /* 5: root */,
      /*child_object_stream_id=*/1,
      num_minifat_sectors ? first_data_sector_loc : kEndOfList,
      num_minifat_sectors ? data_size : 0, out);

  // Second entry, DebuggerFindSource data object.
  write_directory_entry(
      "D\0e\0b\0u\0g\0g\0e\0r\0F\0i\0n\0d\0S\0o\0u\0r\0c\0e\0\0", 38,
      /*object_type=*/2 /* 2: stream */, kNoChild,
      num_minifat_sectors ? 0 : first_data_sector_loc, data_size, out);

  // Two entries padding to fill up the sector.
  for (int j = 0; j < 2; ++j) {
    // All entries must be null, except for sibling and child ids.
    for (int i = 0; i < 68; ++i) fputc(0, out);
    for (int i = 0; i < 12; ++i) fputc(0xff, out);
    for (int i = 0; i < 48; ++i) fputc(0, out);
  }

  // Mini FAT, if applicable.
  for (uint32_t i = 0; i < num_minifat_sectors; ++i)
    for (uint32_t j = 0; j < 128; ++j) {
      uint32_t n = i * 128 + j;
      if (n < num_data_mini_sectors) {
        write_little_long(n + 1 == num_data_mini_sectors ? kEndOfList : n + 1,
                          out);
      } else
        write_little_long(kFree, out);
    }

  // Actual data, padded to section size.
  fwrite(data.data(), data_size, 1, out);
  uint32_t padding = align(data_size, kSectorSize) * kSectorSize - data_size;
  for (uint32_t i = 0; i < padding; ++i) fputc(0, out);

  fclose(out);
}
