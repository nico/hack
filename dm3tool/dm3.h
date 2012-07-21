#ifndef DM3_H_
#define DM3_H_

#include <stdint.h>

#pragma pack(push, 1)  // This is not portable.

#define DM3_DEF_SHORT   2
#define DM3_DEF_LONG    3
#define DM3_DEF_USHORT  4
#define DM3_DEF_ULONG   5
#define DM3_DEF_FLOAT   6
#define DM3_DEF_DOUBLE  7
#define DM3_DEF_BOOL    8
#define DM3_DEF_CHAR    9  // single 8bit char
#define DM3_DEF_OCTET  10
#define DM3_DEF_STRUCT 15
#define DM3_DEF_STRING 18  // utf16 data
#define DM3_DEF_ARRAY  20

// DM3_DEF_STRUCT is followed by:
//   struct_name_len
//   num_fields
//   (field_name_len, field_type) x num_fields
//
// DM3_DEF_STRING is followed by:
//   string_length
//
// DM3_DEF_ARRAY is followed by:
//   array_type
//   array_length

struct DM3TagData {
  uint8_t tag[4];  // Always '%%%%'.
  uint32_t definition_length;
  uint32_t definition[];
  // Followed by the actual data.
};

#define DM3_TAG_ENTRY_TYPE_TAG_GROUP 20
#define DM3_TAG_ENTRY_TYPE_DATA      21

struct DM3TagEntry {
  uint8_t type;
  uint16_t label_length;
  uint8_t label[];
  // Followed by a DM3TagGroup if |type == DM3_TAG_ENTRY_TYPE_TAG_GROUP|
  // or a DM3TagData if |type == DM3_TAG_ENTRY_TYPE_DATA|.
};

struct DM3TagGroup {
  uint8_t is_sorted;
  uint8_t is_open;
  uint32_t num_tags;
  struct DM3TagEntry tags[];
};

struct DM3Image {
  uint32_t version;
  uint32_t length;
  uint32_t is_little_endian;  // This really means something else.
  struct DM3TagGroup tag_group;
};

#pragma pack(pop)

#endif  // DM3_H_
