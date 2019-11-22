/*
clang-cl -O2 /W4 -Wconversion DebuggerFindSource_write.cc

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

void write_little_long(uint32_t v, std::vector<uint8_t>* data) {
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

  write_little_long(0, &data);  // verDirCache

  write_little_long(0, &data);  // verStringList
  write_little_long(std::size(kDirectories), &data);
  for (const char* dir : kDirectories) {
    uint32_t l = static_cast<uint32_t>(strlen(dir));
    write_little_long(2 * l + 2, &data);
    for (uint32_t i = 0; i < l; ++i) {
      data.push_back(static_cast<uint8_t>(dir[i]));
      data.push_back('\0');
    }
    data.push_back('\0');
    data.push_back('\0');
  }

  write_little_long(0, &data);  // verStringList
  write_little_long(std::size(kBlockedFiles), &data);
  for (const char* file : kBlockedFiles) {
    uint32_t l = static_cast<uint32_t>(strlen(file));
    write_little_long(2 * l + 2, &data);
    for (uint32_t i = 0; i < l; ++i) {
      data.push_back(static_cast<uint8_t>(file[i]));
      data.push_back('\0');
    }
    data.push_back('\0');
    data.push_back('\0');
  }

  return data;
}

int main() {
  std::vector<uint8_t> data = debug_data();

  const uint32_t kMinistreamCutoffSize = 4096;

  const uint16_t kSectorShift = 9;
  const uint16_t kMiniSectorShift = 6;

  const int kSectorSize = 1 << kSectorShift;
  const int kMiniSectorSize = 1 << kMiniSectorShift;

  if (data.size() > kMinistreamCutoffSize) {
    // Store data in regular sectors.
    size_t num_data_sectors = (data.size() + kSectorSize - 1) / kSectorSize;
  } else {
    // Store data in ministream.
    size_t num_data_mini_sectors =
        (data.size() + kMiniSectorSize - 1) / kMiniSectorSize;
  }
}
