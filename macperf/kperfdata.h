#pragma once

#include "types.h"

extern "C" {

// From https://gist.github.com/ibireme/173517c208c7dc333ba962c1f0d67d12

// -----------------------------------------------------------------------------
// <kperfdata.framework> header (reverse engineered)
// This framework provides some functions to access the local CPU database.
// These functions do not require root privileges.
// -----------------------------------------------------------------------------

// KPEP CPU archtecture constants.
#define KPEP_ARCH_I386 0
#define KPEP_ARCH_X86_64 1
#define KPEP_ARCH_ARM 2
#define KPEP_ARCH_ARM64 3

/// KPEP event (size: 48/28 bytes on 64/32 bit OS)
typedef struct kpep_event {
  const char* name;  ///< Unique name of a event, such as "INST_RETIRED.ANY".
  const char* description;  ///< Description for this event.
  const char* errata;       ///< Errata, currently NULL.
  const char* alias;        ///< Alias name, such as "Instructions", "Cycles".
  const char* fallback;     ///< Fallback event name for fixed counter.
  u32 mask;
  u8 number;
  u8 umask;
  u8 reserved;
  u8 is_fixed;
} kpep_event;

/// KPEP database (size: 144/80 bytes on 64/32 bit OS)
typedef struct kpep_db {
  const char* name;            ///< Database name, such as "haswell".
  const char* cpu_id;          ///< Plist name, such as "cpu_7_8_10b282dc".
  const char* marketing_name;  ///< Marketing name, such as "Intel Haswell".
  void* plist_data;            ///< Plist data (CFDataRef), currently NULL.
  void* event_map;  ///< All events (CFDict<CFSTR(event_name), kpep_event *>).
  kpep_event*
      event_arr;  ///< Event struct buffer (sizeof(kpep_event) * events_count).
  kpep_event** fixed_event_arr;  ///< Fixed counter events (sizeof(kpep_event *)
                                 ///< * fixed_counter_count)
  void* alias_map;  ///< All aliases (CFDict<CFSTR(event_name), kpep_event *>).
  usize reserved_1;
  usize reserved_2;
  usize reserved_3;
  usize event_count;  ///< All events count.
  usize alias_count;
  usize fixed_counter_count;
  usize config_counter_count;
  usize power_counter_count;
  u32 archtecture;  ///< see `KPEP CPU archtecture constants` above.
  u32 fixed_counter_bits;
  u32 config_counter_bits;
  u32 power_counter_bits;
} kpep_db;

/// KPEP config (size: 80/44 bytes on 64/32 bit OS)
typedef struct kpep_config {
  kpep_db* db;
  kpep_event** ev_arr;  ///< (sizeof(kpep_event *) * counter_count), init NULL
  usize* ev_map;        ///< (sizeof(usize *) * counter_count), init 0
  usize* ev_idx;        ///< (sizeof(usize *) * counter_count), init -1
  u32* flags;           ///< (sizeof(u32 *) * counter_count), init 0
  u64* kpc_periods;     ///< (sizeof(u64 *) * counter_count), init 0
  usize event_count;    /// kpep_config_events_count()
  usize counter_count;
  u32 classes;  ///< See `class mask constants` above.
  u32 config_counter;
  u32 power_counter;
  u32 reserved;
} kpep_config;

/// Error code for kpep_config_xxx() and kpep_db_xxx() functions.
typedef enum {
  KPEP_CONFIG_ERROR_NONE = 0,
  KPEP_CONFIG_ERROR_INVALID_ARGUMENT = 1,
  KPEP_CONFIG_ERROR_OUT_OF_MEMORY = 2,
  KPEP_CONFIG_ERROR_IO = 3,
  KPEP_CONFIG_ERROR_BUFFER_TOO_SMALL = 4,
  KPEP_CONFIG_ERROR_CUR_SYSTEM_UNKNOWN = 5,
  KPEP_CONFIG_ERROR_DB_PATH_INVALID = 6,
  KPEP_CONFIG_ERROR_DB_NOT_FOUND = 7,
  KPEP_CONFIG_ERROR_DB_ARCH_UNSUPPORTED = 8,
  KPEP_CONFIG_ERROR_DB_VERSION_UNSUPPORTED = 9,
  KPEP_CONFIG_ERROR_DB_CORRUPT = 10,
  KPEP_CONFIG_ERROR_EVENT_NOT_FOUND = 11,
  KPEP_CONFIG_ERROR_CONFLICTING_EVENTS = 12,
  KPEP_CONFIG_ERROR_COUNTERS_NOT_FORCED = 13,
  KPEP_CONFIG_ERROR_EVENT_UNAVAILABLE = 14,
  KPEP_CONFIG_ERROR_ERRNO = 15,
  KPEP_CONFIG_ERROR_MAX
} kpep_config_error_code;

/// Error description for kpep_config_error_code.
static const char* kpep_config_error_names[KPEP_CONFIG_ERROR_MAX] = {
    "none",
    "invalid argument",
    "out of memory",
    "I/O",
    "buffer too small",
    "current system unknown",
    "database path invalid",
    "database not found",
    "database architecture unsupported",
    "database version unsupported",
    "database corrupt",
    "event not found",
    "conflicting events",
    "all counters must be forced",
    "event unavailable",
    "check errno",
};

/// Error description.
static const char* kpep_config_error_desc(int code) {
  if (0 <= code && code < KPEP_CONFIG_ERROR_MAX) {
    return kpep_config_error_names[code];
  }
  return "unknown error";
}

/// Create a config.
/// @param db A kpep db, see kpep_db_create()
/// @param cfg_ptr A pointer to receive the new config.
/// @return kpep_config_error_code, 0 for success.
int kpep_config_create(kpep_db* db, kpep_config** cfg_ptr);

/// Free the config.
void kpep_config_free(kpep_config* cfg);

/// Add an event to config.
/// @param cfg The config.
/// @param ev_ptr A event pointer.
/// @param flag 0: all, 1: user space only
/// @param err Error bitmap pointer, can be NULL.
///            If return value is `CONFLICTING_EVENTS`, this bitmap contains
///            the conflicted event indices, e.g. "1 << 2" means index 2.
/// @return kpep_config_error_code, 0 for success.
int kpep_config_add_event(kpep_config* cfg,
                          kpep_event** ev_ptr,
                          u32 flag,
                          u32* err);

/// Remove event at index.
/// @return kpep_config_error_code, 0 for success.
int kpep_config_remove_event(kpep_config* cfg, usize idx);

/// Force all counters.
/// @return kpep_config_error_code, 0 for success.
int kpep_config_force_counters(kpep_config* cfg);

/// Get events count.
/// @return kpep_config_error_code, 0 for success.
int kpep_config_events_count(kpep_config* cfg, usize* count_ptr);

/// Get all event pointers.
/// @param buf A buffer to receive event pointers.
/// @param buf_size The buffer's size in bytes, should not smaller than
///                 kpep_config_events_count() * sizeof(void *).
/// @return kpep_config_error_code, 0 for success.
int kpep_config_events(kpep_config* cfg, kpep_event** buf, usize buf_size);

/// Get kpc register configs.
/// @param buf A buffer to receive kpc register configs.
/// @param buf_size The buffer's size in bytes, should not smaller than
///                 kpep_config_kpc_count() * sizeof(kpc_config_t).
/// @return kpep_config_error_code, 0 for success.
int kpep_config_kpc(kpep_config* cfg, kpc_config_t* buf, usize buf_size);

/// Get kpc register config count.
/// @return kpep_config_error_code, 0 for success.
int kpep_config_kpc_count(kpep_config* cfg, usize* count_ptr);

/// Get kpc classes.
/// @param classes See `class mask constants` above.
/// @return kpep_config_error_code, 0 for success.
int kpep_config_kpc_classes(kpep_config* cfg, u32* classes_ptr);

/// Get the index mapping from event to counter.
/// @param buf A buffer to receive indexes.
/// @param buf_size The buffer's size in bytes, should not smaller than
///                 kpep_config_events_count() * sizeof(kpc_config_t).
/// @return kpep_config_error_code, 0 for success.
int kpep_config_kpc_map(kpep_config* cfg, usize* buf, usize buf_size);

/// Open a kpep database file in "/usr/share/kpep/" or "/usr/local/share/kpep/".
/// @param name File name, for example "haswell", "cpu_100000c_1_92fb37c8".
///             Pass NULL for current CPU.
/// @return kpep_config_error_code, 0 for success.
int kpep_db_create(const char* name, kpep_db** db_ptr);

/// Free the kpep database.
void kpep_db_free(kpep_db* db);

/// Get the database's name.
/// @return kpep_config_error_code, 0 for success.
int kpep_db_name(kpep_db* db, const char** name);

/// Get the event alias count.
/// @return kpep_config_error_code, 0 for success.
int kpep_db_aliases_count(kpep_db* db, usize* count);

/// Get all alias.
/// @param buf A buffer to receive all alias strings.
/// @param buf_size The buffer's size in bytes,
///        should not smaller than kpep_db_aliases_count() * sizeof(void *).
/// @return kpep_config_error_code, 0 for success.
int kpep_db_aliases(kpep_db* db, const char** buf, usize buf_size);

/// Get counters count for given classes.
/// @param classes 1: Fixed, 2: Configurable.
/// @return kpep_config_error_code, 0 for success.
int kpep_db_counters_count(kpep_db* db, u8 classes, usize* count);

/// Get all event count.
/// @return kpep_config_error_code, 0 for success.
int kpep_db_events_count(kpep_db* db, usize* count);

/// Get all events.
/// @param buf A buffer to receive all event pointers.
/// @param buf_size The buffer's size in bytes,
///        should not smaller than kpep_db_events_count() * sizeof(void *).
/// @return kpep_config_error_code, 0 for success.
int kpep_db_events(kpep_db* db, kpep_event** buf, usize buf_size);

/// Get one event by name.
/// @return kpep_config_error_code, 0 for success.
int kpep_db_event(kpep_db* db, const char* name, kpep_event** ev_ptr);

/// Get event's name.
/// @return kpep_config_error_code, 0 for success.
int kpep_event_name(kpep_event* ev, const char** name_ptr);

/// Get event's alias.
/// @return kpep_config_error_code, 0 for success.
int kpep_event_alias(kpep_event* ev, const char** alias_ptr);

/// Get event's description.
/// @return kpep_config_error_code, 0 for success.
int kpep_event_description(kpep_event* ev, const char** str_ptr);
}
