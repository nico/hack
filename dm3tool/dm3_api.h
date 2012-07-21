#ifndef DM3_API_H_
#define DM3_API_H_

#include <stdint.h>
#include <stddef.h>

// Implementation details, hence only forward-declared.
typedef struct DM3File DM3File;
typedef struct DM3List DM3List;
typedef struct DM3Data DM3Data;




struct DM3File* dm3_open(const char* filename);
void dm3_close(DM3File* dm3);

int dm3_is_little_endian(DM3File* dm3);


struct DM3List* dm3_file_get_list(DM3File* dm3);

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

#endif  // DM3_API_H_
