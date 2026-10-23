#ifndef NSO_HOST_H
#define NSO_HOST_H
#include "../nso/nso_loader.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Optional exact-name extension, e.g. a typed NVN bootstrap resolver.
 * Return zero for unsupported names. No graphics headers or generic stub ABI. */
void nso_host_set_extension_resolver(nso_import_fn resolver);
/* Android builds strongly reference the linked compiler C++ ABI guard runtime.
 * Link libc++abi (plus its platform dependencies); no dlsym export is required.
 * Other hosts can exercise the same path with NSO_WITH_CXX_GUARDS=1. */
uint64_t nso_host_resolve(uint32_t index,const char *name);
struct nn_uuid { uint64_t w0, w1; };
struct nn_uuid nso_host_generate_uuid(void);
struct nn_location_name { char name[36]; };
uint32_t nso_host_time_initialize(void);
bool nso_host_time_is_initialized(void);
uint32_t nso_host_time_get_current_time(int64_t *out);
uint32_t nso_host_time_get_device_location_name(struct nn_location_name *out);
uint32_t nso_host_time_load_timezone_rule(void *rule, const struct nn_location_name *name);
int nso_host_time_get_day_of_week(int y, int m, int d);
int64_t nso_host_os_get_system_tick_frequency(void);
int64_t nso_host_os_get_system_tick(void);
struct nn_calendar_time {
    int16_t year;
    int8_t month;
    int8_t day;
    int8_t hour;
    int8_t minute;
    int8_t second;
    int8_t pad;
};
struct nn_calendar_additional_info {
    uint32_t day_of_week;
    int32_t day_of_year;
    char timezone_name[8];
    bool is_dst;
    char pad[3];
    int32_t utc_offset_seconds;
};
uint32_t nso_host_time_to_calendar_time(struct nn_calendar_time *out_cal,
                                        struct nn_calendar_additional_info *out_info,
                                        const int64_t *posix_time,
                                        const void *rule);
uint64_t nso_host_time_to_calendar_time_in_utc(const int64_t *posix_time);
void nso_host_tls_init_noop(void);

int32_t nso_host_create_thread(void *thread, void (*entry)(void *), void *arg, void *stack, size_t stack_size, int32_t priority);
int32_t nso_host_create_thread_7(void *thread, void (*entry)(void *), void *arg, void *stack, size_t stack_size, int32_t priority, int32_t ideal_core);
void nso_host_start_thread(void *thread);
void nso_host_wait_thread(void *thread);
void nso_host_destroy_thread(void *thread);
void nso_host_set_thread_name(void *thread, const char *name);
void nso_host_set_thread_name_pointer(void *thread, const char *name);
const char *nso_host_get_thread_name_pointer(const void *thread);
int32_t nso_host_get_thread_priority(const void *thread);
int32_t nso_host_get_thread_current_priority(const void *thread);
void nso_host_change_thread_priority(void *thread, int32_t priority);
uint64_t nso_host_get_thread_id(const void *thread);
void nso_host_sleep_thread(int64_t nanoseconds);
void nso_host_yield_thread(void);
void nso_host_set_thread_core_mask(void *thread, int32_t ideal_core, uint64_t affinity_mask);
void nso_host_get_thread_core_mask(int32_t *out_ideal_core, uint64_t *out_affinity_mask, const void *thread);
uint64_t nso_host_get_thread_available_core_mask(void);
void *nso_host_get_current_thread(void);
int32_t nso_host_movie_get_thread_priority(void);
void nso_host_movie_set_thread_priority(int32_t p);

typedef void *(*nn_fs_alloc_fn)(size_t);
typedef void (*nn_fs_free_fn)(void *, size_t);
void nso_host_fs_set_allocator(nn_fs_alloc_fn alloc, nn_fs_free_fn free_fn);
nn_fs_alloc_fn nso_host_fs_get_allocator(void);
nn_fs_free_fn nso_host_fs_get_deallocator(void);
int32_t nso_host_fs_query_mount_rom_cache_size(size_t *out_size);
int32_t nso_host_fs_mount_rom(const char *name, void *buffer, size_t size);
int nso_host_fs_is_rom_mounted(const char *name);
const char *nso_host_fs_get_rom_mount_path(const char *name);
int32_t nso_host_fs_unmount(const char *name);

typedef struct {
    void *handle;
} nn_fs_file_handle;

int32_t nso_host_fs_open_file(nn_fs_file_handle *out_handle, const char *path, int32_t mode);
int32_t nso_host_fs_get_file_size(int64_t *out_size, nn_fs_file_handle handle);
int32_t nso_host_fs_read_file(size_t *out_bytes_read, nn_fs_file_handle handle, int64_t offset, void *buffer, size_t size);
int32_t nso_host_fs_read_file_no_out(nn_fs_file_handle handle, int64_t offset, void *buffer, size_t size);
void nso_host_fs_close_file(nn_fs_file_handle handle);
int32_t nso_host_fs_flush_file(nn_fs_file_handle handle);
int32_t nso_host_fs_set_file_size(nn_fs_file_handle handle, int64_t size);
int32_t nso_host_fs_write_file(nn_fs_file_handle handle, int64_t offset, const void *data, size_t size, const void *option);
int32_t nso_host_fs_get_entry_type(int32_t *out_entry_type, const char *path);
void nso_host_fs_disable_global_file_data_cache(void);
void nso_host_fs_enable_global_file_data_cache(void *buffer, size_t size);

int32_t nso_host_aoc_count_add_on_content(void);
int32_t nso_host_aoc_list_add_on_content(int32_t *out_indices, int32_t offset, int32_t count);
int32_t nso_host_fs_query_mount_add_on_content_cache_size(size_t *out_size, int32_t target_index);
int32_t nso_host_fs_mount_add_on_content(const char *name, int32_t target_index, void *buffer, size_t size);

int32_t nso_host_nifm_initialize(void);
bool nso_host_nifm_is_initialized(void);
#ifdef __cplusplus
}
#endif
#endif
