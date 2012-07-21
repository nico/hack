#include "dm3_api.h"

#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "dm3.h"

struct DM3File {
  struct DM3Image* image;
  int file;
  size_t stat_size;
};

struct DM3List {
  struct DM3TagGroup* group;
};

struct DM3File* dm3_open(const char* filename) {
  struct stat in_stat;
  int file = open(filename, O_RDONLY);
  if (!file)
    return NULL;

  if (fstat(file, &in_stat)) {
    close(file);
    return NULL;
  }

  struct DM3Image* in_dm3;
  in_dm3 = (struct DM3Image*)mmap(/*addr=*/0, in_stat.st_size,
                                  PROT_READ, MAP_SHARED, file, /*offset=*/0);
  if (in_dm3 == MAP_FAILED) {
    close(file);
    return NULL;
  }

  DM3File* result = malloc(sizeof(DM3File));
  result->image = in_dm3;
  result->file = file;
  result->stat_size = in_stat.st_size;
  return result;
}

void dm3_close(DM3File* dm3) {
  if (!dm3) return;
  munmap(dm3->image, dm3->stat_size);
  close(dm3->file);
  free(dm3);
}

int dm3_is_little_endian(DM3File* dm3) {
  return dm3->image->is_little_endian &&
         dm3->image->is_little_endian != 0x01000000;
}

DM3List* dm3_file_get_list(DM3File* dm3) {
  return dm3->image->tag_group;
}

size_t dm3_list_num_children(DM3List* list);
int dm3_list_child_type(size_t index);
const char* dm3_list_get_label(size_t index);

struct DM3List* dm3_list_get_list(DM3List* list, size_t index);
struct DM3List* dm3_list_find_list(DM3List* list, const char* label);

struct DM3Data* dm3_list_get_data(DM3List* list, size_t index);
struct DM3Data* dm3_list_find_data(DM3List* list, const char* label);

uint32_t* dm3_data_get_definition(DM3Data* data);
int dm3_data_get_definition_length(DM3Data* data);
const void* dm3_data_get_data(DM3Data* data);
