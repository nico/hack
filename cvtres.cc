/*
clang++ -std=c++11 -o cvtres cvtres.cc

A reimplemenation of cvtres.exe
*/
#include <experimental/string_view>
#include <map>
#include <unordered_map>
#include <vector>

#include <assert.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

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

struct ResEntry {
  uint32_t data_size;
  uint32_t header_size;  // Always 0x20 plus storage for type_str and name_str
                         // if type or name aren't numeric.

  bool type_is_id;  // determines which of the following two is valid.
  uint16_t type_id;
  std::vector<char16_t> type_str;

  bool name_is_id;  // determines which of the following two is valid.
  uint16_t name_id;
  std::vector<char16_t> name_str;

  uint32_t data_version;
  uint16_t memory_flags;
  uint16_t language_id;
  uint32_t version;
  uint32_t characteristics;

  uint8_t* data;  // weak
};

struct ResEntries {
  std::vector<ResEntry> entries;
};

static ResEntry load_resource_entry(uint8_t* data, uint32_t* n_read) {
  ResEntry entry;
  entry.data_size = read_little_long(&data);
  entry.header_size = read_little_long(&data);

  // https://msdn.microsoft.com/en-us/library/windows/desktop/ms648027(v=vs.85).aspx

  // if type, name start with 0xffff then they're numeric IDs. Else they're
  // inline zero-terminated utf-16le strings. After name, there might be one
  // word of padding to align data_version.
  uint8_t* string_start = data;
  uint16_t type = read_little_short(&data);
  entry.type_is_id = type == 0xffff;
  if (entry.type_is_id) {
    entry.type_id = read_little_short(&data);
  } else {
    while (type != 0) {
      entry.type_str.push_back(type);
      type = read_little_short(&data);
    }
  }
  uint16_t name = read_little_short(&data);
  entry.name_is_id = name == 0xffff;
  if (entry.name_is_id) {
    entry.name_id = read_little_short(&data);
  } else {
    while (name != 0) {
      entry.name_str.push_back(name);
      name = read_little_short(&data);
    }
  }
  // Pad to dword boundary:
  if ((data - string_start) & 2)
    data += 2;
  // Check that bigger headers are explained by string types and names.
  if (entry.header_size != 0x20 + (data - string_start - 8))
    fatal("unexpected header size\n");  // XXX error code

  entry.data_version = read_little_long(&data);
  entry.memory_flags = read_little_short(&data);
  entry.language_id = read_little_short(&data);
  entry.version = read_little_long(&data);
  entry.characteristics = read_little_long(&data);

  entry.data = data;

  uint32_t total_size = entry.data_size + entry.header_size;
  *n_read = total_size + ((4 - (total_size & 3)) & 3);  // DWORD-align.
  return entry;
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
_Static_assert(sizeof(StandardSymbolRecord) == 18, "");

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
_Static_assert(sizeof(SectionAuxSymbolRecord) == 18, "");
#pragma pack(pop)

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
_Static_assert(sizeof(Relocation) == 10, "");
#pragma pack(pop)

typedef struct {
  uint32_t Characteristics;
  uint32_t TimeDateStamp;
  uint16_t MajorVersion;
  uint16_t MinorVersion;
  uint16_t NumberOfNameEntries;
  uint16_t NumberOfIdEntries;
} ResourceDirectoryHeader;  // 16 bytes

typedef struct {
  uint32_t TypeNameLang;  // Either string address or id.
  // High bit 0: Address of a Resource Data Entry (a leaf).
  // High bit 1: Address of a Resource Directory Table.
  uint32_t DataRVA;
} ResourceDirectoryEntry;  // 8 bytes

typedef struct {
  uint32_t DataRVA;
  uint32_t Size;
  uint32_t Codepage;
  uint32_t Reserved;
} ResourceDataEntry;

struct NodeKey {
  bool is_id;  // determines which of the following two is valid.
  uint16_t id;
  const std::vector<char16_t>* str;

  bool operator<(const NodeKey& rhs) const {
    if (is_id != rhs.is_id)
      return is_id < rhs.is_id;  // Names come before ids.
    // If we come here, *this and rhs are either both numeric or both strings.
    if (is_id)
      return id < rhs.id;
    return *str < *rhs.str;
  }
};

static void write_rsrc_obj(const char* out_name, const ResEntries& entries) {
  // Want:
  // COFF headers
  // .rsrc$01 section with tree metadata (type->name->lang)
  //   - relocations for all ResourceDataEntries (ResourceDirectoryEntry leaves)
  // .rsrc$02 section with actual resource data
  // symbol table that relocations and section names refer to

  // Two phases:
  // 1. Compute all file layout information at first
  // 2. Write output in one pass

  // Phase 1: Compute layout.

  // Build type->name->lang resource tree.
  std::vector<uint16_t> string_table;
  std::map<std::experimental::u16string_view, uint32_t> strings;

  std::map<NodeKey, std::map<NodeKey, std::map<uint16_t, const ResEntry*>>>
      directory;
  for (const auto& entry : entries.entries) {
    NodeKey type_key{entry.type_is_id, entry.type_id, &entry.type_str};
    NodeKey name_key{entry.name_is_id, entry.name_id, &entry.name_str};

    auto& lang_map = directory[type_key][name_key];
    auto lang_it = lang_map.insert(std::make_pair(entry.language_id, &entry));
    if (!lang_it.second)
      fatal("duplicate element");

    // Also write type and name to the string table.
    if (!entry.type_is_id) {
      std::experimental::u16string_view s(entry.type_str.data(),
                                          entry.type_str.size());
      auto it = strings.insert(std::make_pair(s, 0));
      if (it.second) {
        // String wasn't in |strings| yet.  Add it to the string table, and
        // update |it| to contain the right value.
        off_t index = string_table.size() * sizeof(uint16_t);
        string_table.push_back(entry.type_str.size());
        string_table.insert(string_table.end(), entry.type_str.begin(),
                            entry.type_str.end());
        it.first->second = index;
      }
    }
    if (!entry.name_is_id) {
      std::experimental::u16string_view s(entry.name_str.data(),
                                          entry.name_str.size());
      auto it = strings.insert(std::make_pair(s, 0));
      if (it.second) {
        // String wasn't in |strings| yet.  Add it to the string table, and
        // update |it| to contain the right value.
        off_t index = string_table.size() * sizeof(uint16_t);
        string_table.push_back(entry.name_str.size());
        string_table.insert(string_table.end(), entry.name_str.begin(),
                            entry.name_str.end());
        it.first->second = index;
      }
    }
  }

  // Do tree layout pass.
  // The COFF spec says that the layout is:
  // - ResourceDirectoryHeaders each followed by its ResourceDirectoryEntries
  // - Strings. Each string is (len, chars).
  // - ResourceDataEntries (aligned)
  // - Actual resource data.
  //
  // cvtres.exe however writes data in this order:
  // - ResourceDirectoryHeaders each followed by its ResourceDirectoryEntries
  // - ResourceDataEntries (aligned)
  // - Strings. Each string is (len, chars).
  // - Relocations.
  // - Actual resource data.
  //
  // Match cvtres.exe's order.
  // For the tables, cvtres.exe writes all type headers, then all name headers,
  // then all lang headers (instead of depth-first).
  std::vector<uint32_t> offsets;
  uint32_t offset = sizeof(ResourceDirectoryHeader) +
                    directory.size() * sizeof(ResourceDirectoryEntry);
  for (auto& type : directory) {
    offsets.push_back(offset);
    offset += sizeof(ResourceDirectoryHeader) +
              type.second.size() * sizeof(ResourceDirectoryEntry);
  }
  for (auto& type : directory) {
    for (auto& name : type.second) {
      offsets.push_back(offset);
      offset += sizeof(ResourceDirectoryHeader) +
                name.second.size() * sizeof(ResourceDirectoryEntry);
    }
  }
  uint32_t resource_data_entry_start = offset;
  uint32_t string_table_start =
      resource_data_entry_start +
      entries.entries.size() * sizeof(ResourceDataEntry);
  uint32_t relocations_start =
      string_table_start + string_table.size() * sizeof(uint16_t);
  // XXX padding after string table?
  uint32_t rsrc01_data_size = relocations_start;
  uint32_t rsrc01_total_size =
      rsrc01_data_size + entries.entries.size() * sizeof(Relocation);
  // XXX padding after relocations?

  // Compute offsets of all resource data in .rsrc$02.
  std::vector<uint32_t> res_offsets;
  uint32_t res_offset = 0;
  for (const auto& entry : entries.entries) {
    res_offsets.push_back(res_offset);
    res_offset += entry.data_size;
    res_offset = res_offset + ((8 - (res_offset & 7)) & 7);  // 8-byte-align
  }
  uint32_t rsrc02_size = res_offset;

  // Phase 2: Write output.

  // XXX write to temp, atomic-rename at end
  FILE* out_file = fopen(out_name, "wb");

  uint32_t coff_header_size = sizeof(FileHeader) + 2*sizeof(SectionHeader);

  FileHeader coff_header = {};
  coff_header.Machine = 0x8664;  // FIXME: /arch: flag for picking 32 or 64 bit
  coff_header.NumberOfSections = 2;  // .rsrc$01, .rsrc$02
  coff_header.TimeDateStamp = 0;  // FIXME: need flag, for inc linking with link
  coff_header.PointerToSymbolTable =
      coff_header_size + rsrc01_total_size + rsrc02_size;
  // Symbols for section names have 1 aux entry each: (XXX)
  coff_header.NumberOfSymbols = 2*2 + entries.entries.size();
  coff_header.SizeOfOptionalHeader = 0;
  coff_header.Characteristics = 0x100;  // XXX
  fwrite(&coff_header, sizeof(coff_header), 1, out_file);

  SectionHeader rsrc01_header;
  memcpy(rsrc01_header.Name, ".rsrc$01", 8);
  rsrc01_header.VirtualSize = 0;
  rsrc01_header.VirtualAddress = 0;
  rsrc01_header.SizeOfRawData = rsrc01_data_size;
  rsrc01_header.PointerToRawData = coff_header_size;
  rsrc01_header.PointerToRelocations =
      rsrc01_header.PointerToRawData + relocations_start;
  rsrc01_header.PointerToLineNumbers = 0;
  rsrc01_header.NumberOfRelocations = entries.entries.size();
  rsrc01_header.NumberOfLinenumbers = 0;
  rsrc01_header.Characteristics = 0xc0000040;  // read + write + initialized
  fwrite(&rsrc01_header, sizeof(rsrc01_header), 1, out_file);

  SectionHeader rsrc02_header;
  memcpy(rsrc02_header.Name, ".rsrc$02", 8);
  rsrc02_header.VirtualSize = 0;
  rsrc02_header.VirtualAddress = 0;
  rsrc02_header.SizeOfRawData = rsrc02_size;
  rsrc02_header.PointerToRawData = coff_header_size + rsrc01_total_size;
  rsrc02_header.PointerToRelocations = 0;
  rsrc02_header.PointerToLineNumbers = 0;
  rsrc02_header.NumberOfRelocations = 0;
  rsrc02_header.NumberOfLinenumbers = 0;
  rsrc02_header.Characteristics = 0xc0000040;  // read + write + initialized
  fwrite(&rsrc02_header, sizeof(rsrc02_header), 1, out_file);

  // Write .rsrc$01 section.
  assert(ftello(out_file) == rsrc01_header.PointerToRawData);

  size_t num_types = directory.size();
  size_t num_named_types = 0;
  for (const auto& types : directory) {
    if (types.first.is_id)
      break;
    ++num_named_types;
  }

  ResourceDirectoryHeader type_dir = {};
  type_dir.NumberOfNameEntries = num_named_types;
  type_dir.NumberOfIdEntries= num_types - num_named_types;
  fwrite(&type_dir, sizeof(type_dir), 1, out_file);
  unsigned next_offset_index = 0;
  for (auto& type : directory) {
    ResourceDirectoryEntry entry;
    entry.DataRVA = offsets[next_offset_index++] | 0x80000000;
    if (!type.first.is_id) {
      std::experimental::u16string_view s(type.first.str->data(),
                                          type.first.str->size());
      auto it = strings.find(s);
      if (it == strings.end())
        fatal("type str should have been inserted above!\n");
      entry.TypeNameLang = string_table_start + it->second;
      // XXX cvtres.exe sets high bit of TypeNameLang for strings; should we?
    } else {
      entry.TypeNameLang = type.first.id;
    }
    fwrite(&entry, sizeof(entry), 1, out_file);
  }

  for (auto& type : directory) {
    size_t num_names = type.second.size();
    size_t num_named_names = 0;
    for (const auto& names : type.second) {
      if (names.first.is_id)
        break;
      ++num_named_names;
    }

    ResourceDirectoryHeader name_dir = {};
    name_dir.NumberOfNameEntries = num_named_names;
    name_dir.NumberOfIdEntries = num_names - num_named_names;
    fwrite(&name_dir, sizeof(name_dir), 1, out_file);
    for (auto& name : type.second) {
      ResourceDirectoryEntry entry;
      entry.DataRVA = offsets[next_offset_index++] | 0x80000000;
      if (!name.first.is_id) {
        std::experimental::u16string_view s(name.first.str->data(),
                                            name.first.str->size());
        auto it = strings.find(s);
        if (it == strings.end())
          fatal("name str should have been inserted above!\n");
        entry.TypeNameLang = string_table_start + it->second;
      // XXX cvtres.exe sets high bit of TypeNameLang for strings; should we?
      } else {
        entry.TypeNameLang = name.first.id;
      }
      fwrite(&entry, sizeof(entry), 1, out_file);
    }
  }

  unsigned int data_index = 0;
  for (auto& type : directory) {
    for (auto& name : type.second) {
      ResourceDirectoryHeader lang_dir = {};
      lang_dir.NumberOfNameEntries = 0;
      lang_dir.NumberOfIdEntries = name.second.size();
      fwrite(&lang_dir, sizeof(lang_dir), 1, out_file);
      for (auto& lang : name.second) {
        ResourceDirectoryEntry entry;
        entry.DataRVA = offset + data_index++ * sizeof(ResourceDataEntry);
        entry.TypeNameLang = lang.first;
        fwrite(&entry, sizeof(entry), 1, out_file);
      }
    }
  }

  // Write resource data entries (the COFF spec recommends to put these after
  // the string table, but cvtres.exe puts them before it).
  std::unordered_map<const ResEntry*, unsigned> ordered_entries;
  int entry_index = 0;
  for (auto& type : directory) {
    for (auto& name : type.second) {
      for (auto& lang : name.second) {
        ResourceDataEntry data_entry;
        data_entry.DataRVA = 0;  // Fixed up by a relocation.
        data_entry.Size = lang.second->data_size;
        data_entry.Codepage = 0;  // XXX
        data_entry.Reserved = 0;
        fwrite(&data_entry, sizeof(data_entry), 1, out_file);

        ordered_entries[lang.second] = entry_index++;
      }
    }
  }

  // Write string table after resource directory. (with padding)
  assert(ftello(out_file) == coff_header_size + string_table_start);
  fwrite(string_table.data(), string_table.size(), sizeof(uint16_t), out_file);

  // Write relocations.
  assert(ftello(out_file) == coff_header_size + relocations_start);
  for (unsigned i = 0; i < entries.entries.size(); ++i) {
    Relocation reloc;
    reloc.VirtualAddress =
        resource_data_entry_start +
        ordered_entries[&entries.entries[i]] * sizeof(ResourceDataEntry);
    reloc.SymbolTableInd = 8 + i;
    reloc.Type = 3;  // XXX is this correct in both 32-bit and 64-bit?
    fwrite(&reloc, sizeof(reloc), 1, out_file);
  }

  // XXX padding?

  // Write .rsrc$02 section.
  assert(ftello(out_file) == rsrc02_header.PointerToRawData);

  // Actual resource data.
  for (const auto& entry : entries.entries) {
    fwrite(entry.data, 1, entry.data_size, out_file);
    fwrite("padding", ((8 - (entry.data_size & 7)) & 7), 1, out_file);
  }

  // Write symbol table, followed by string table size.
  assert(ftello(out_file) == coff_header.PointerToSymbolTable);

  const int kIMAGE_SYM_CLASS_STATIC = 3;
  StandardSymbolRecord rsrc01_symbol;
  memcpy(rsrc01_symbol.Name, ".rsrc$01", 8);
  rsrc01_symbol.Value = 0;
  rsrc01_symbol.SectionNumber = 1;
  rsrc01_symbol.Type = 0;
  rsrc01_symbol.StorageClass = kIMAGE_SYM_CLASS_STATIC;
  rsrc01_symbol.NumberOfAuxSymbols = 1;
  fwrite(&rsrc01_symbol, sizeof(rsrc01_symbol), 1, out_file);

  SectionAuxSymbolRecord rsrc01_aux;
  rsrc01_aux.Length = rsrc01_data_size;
  rsrc01_aux.NumberOfRelocations = entries.entries.size();
  rsrc01_aux.NumberOfLinenumbers = 0;
  rsrc01_aux.CheckSum = 0;
  rsrc01_aux.Number = 0;
  rsrc01_aux.Selection = 0;
  rsrc01_aux.Pad0 = 0;
  rsrc01_aux.Pad1 = 0;
  rsrc01_aux.Pad2 = 0;
  fwrite(&rsrc01_aux, sizeof(rsrc01_aux), 1, out_file);

  StandardSymbolRecord rsrc02_symbol;
  memcpy(rsrc02_symbol.Name, ".rsrc$02", 8);
  rsrc02_symbol.Value = 0;
  rsrc02_symbol.SectionNumber = 2;
  rsrc02_symbol.Type = 0;
  rsrc02_symbol.StorageClass = kIMAGE_SYM_CLASS_STATIC;
  rsrc02_symbol.NumberOfAuxSymbols = 1;
  fwrite(&rsrc02_symbol, sizeof(rsrc02_symbol), 1, out_file);

  SectionAuxSymbolRecord rsrc02_aux;
  rsrc02_aux.Length = rsrc02_size;
  rsrc02_aux.NumberOfRelocations = 0;
  rsrc02_aux.NumberOfLinenumbers = 0;
  rsrc02_aux.CheckSum = 0;
  rsrc02_aux.Number = 0;
  rsrc02_aux.Selection = 0;
  rsrc02_aux.CheckSum = 0;
  rsrc02_aux.Pad0 = 0;
  rsrc02_aux.Pad1 = 0;
  rsrc02_aux.Pad2 = 0;
  fwrite(&rsrc02_aux, sizeof(rsrc02_aux), 1, out_file);

  for (uint32_t res_offset : res_offsets) {
    if (res_offset > 0xffffff)
      fatal("cannot handle resources this large yet\n");

    char buf[9];
    snprintf(buf, 9, "$R%06X", res_offset);
    StandardSymbolRecord res_symbol;
    memcpy(res_symbol.Name, buf, 8);
    res_symbol.Value = res_offset;
    res_symbol.SectionNumber = 2;
    res_symbol.Type = 0;
    res_symbol.StorageClass = kIMAGE_SYM_CLASS_STATIC;
    res_symbol.NumberOfAuxSymbols = 0;
    fwrite(&res_symbol, sizeof(res_symbol), 1, out_file);
  }

  // The length of the string table immediately follows the symbol table.
  // According to the coff spec, the size of the string table includes the size
  // field itself, so an empty string table has size 4.  However, cvtres.exe
  // writes 0 here, not 4, so match that.
  fwrite("\0\0\0", 1, 4, out_file);

  fclose(out_file);
}

int main(int argc, char* argv[]) {
  if (argc != 2)
    fatal("Expected args == 2, got %d\n", argc);

  const char *in_name = argv[1];

  // Read input.
  int in_file = open(in_name, O_RDONLY);
  if (!in_file)
    fatal("Unable to read \'%s\'\n", in_name);

  struct stat in_stat;
  if (fstat(in_file, &in_stat))
    fatal("Failed to stat \'%s\'\n", in_name);

  uint8_t* data = (uint8_t*)mmap(/*addr=*/0, in_stat.st_size, PROT_READ,
                                 MAP_SHARED, in_file,
                                 /*offset=*/0);
  if (data == MAP_FAILED)
    fatal("Failed to mmap: %d (%s)\n", errno, strerror(errno));

  uint8_t* end = data + in_stat.st_size;

  ResEntries entries;
  bool is_first = true;
  while (data < end) {
    uint32_t n_read;
    ResEntry entry = load_resource_entry(data, &n_read);
    if (is_first) {
      // Ignore not-16-bit marker.
      is_first = false;
      if (!entry.type_is_id || entry.type_id != 0 || !entry.name_is_id ||
          entry.name_id != 0)
        fatal("expected not-16-bit marker as first entry\n");
    } else {
      if (entry.type_is_id && entry.type_id == 0)
        fatal("0 type\n");
      if (entry.name_is_id && entry.name_id == 0)
        fatal("0 name\n");
      entries.entries.push_back(std::move(entry));
    }
    data += n_read;
  }

  write_rsrc_obj("rsrc.obj", entries);

  munmap(data, in_stat.st_size);
  close(in_file);
}
