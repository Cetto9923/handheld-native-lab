#ifndef NSO_HOST_H
#define NSO_HOST_H
#include "../nso/nso_loader.h"
#include <stdbool.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
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

typedef struct {
    void *handle;
} nn_fs_directory_handle;

#define NN_FS_MAX_PATH 0x301

typedef struct {
    char name[NN_FS_MAX_PATH];
    uint8_t pad[3];
    int8_t type;       /* 0 = Directory, 1 = File */
    uint8_t pad2[3];
    int64_t file_size; /* 0 for directory */
} nn_fs_directory_entry;

int32_t nso_host_fs_open_directory(nn_fs_directory_handle *out_handle, const char *path, int32_t mode);
int32_t nso_host_fs_get_directory_entry_count(int64_t *out_count, nn_fs_directory_handle handle);
int32_t nso_host_fs_read_directory(int64_t *out_entries_read, nn_fs_directory_entry *entries, nn_fs_directory_handle handle, int64_t max_entries);
void nso_host_fs_close_directory(nn_fs_directory_handle handle);
int32_t nso_host_fs_create_directory(const char *path);

int32_t nso_host_aoc_count_add_on_content(void);
int32_t nso_host_aoc_list_add_on_content(int32_t *out_indices, int32_t offset, int32_t count);
int32_t nso_host_fs_query_mount_add_on_content_cache_size(size_t *out_size, int32_t target_index);
int32_t nso_host_fs_mount_add_on_content(const char *name, int32_t target_index, void *buffer, size_t size);
void nso_host_aoc_get_add_on_content_list_changed_event(void *out_event);

void nso_host_os_wait_system_event(void *event);
void nso_host_os_signal_system_event(void *event);
void nso_host_os_destroy_system_event(void *event);

int32_t nso_host_nifm_initialize(void);
bool nso_host_nifm_is_initialized(void);

int32_t nso_host_socket_initialize(void *work_buffer, size_t work_buffer_size, size_t allocator_size, int32_t concurrency);
bool nso_host_socket_is_initialized(void);

int32_t nso_host_account_initialize(void);
bool nso_host_account_is_initialized(void);

typedef struct {
    char code[8];
} nn_settings_language_code;

int32_t nso_host_oe_initialize(void);
bool nso_host_oe_is_initialized(void);
nn_settings_language_code nso_host_oe_get_desired_language(void);

void nso_host_os_initialize_event(void *event, bool initially_signaled, int32_t clear_mode);
void nso_host_os_finalize_event(void *event);
void nso_host_os_signal_event(void *event);
void nso_host_os_clear_event(void *event);
void nso_host_os_wait_event(void *event);
bool nso_host_os_try_wait_event(void *event);
bool nso_host_os_timed_wait_event(void *event, int64_t timeout_ns);
bool nso_host_os_is_event_signaled(void *event);

int nso_host_posix_memalign(void **memptr, size_t alignment, size_t size);
void *nso_host_memalign(size_t alignment, size_t size);
void *nso_host_aligned_alloc(size_t alignment, size_t size);
void *nso_host_valloc(size_t size);
void nso_host_free(void *ptr);

int nso_host_cxa_thread_atexit(void (*dtor)(void *), void *obj, void *dso_symbol);
int nso_host_cxa_thread_atexit_impl(void (*dtor)(void *), void *obj, void *dso_symbol);
size_t nso_exit_registry_thread_pending(void);
void nso_exit_registry_thread_finalize(void);

uint64_t nso_host_diag_abort_impl(void);
int32_t nso_host_diag_get_backtrace(uintptr_t *out_backtrace, int32_t count);

void nso_host_std_mutex_lock(void *m);
void nso_host_std_mutex_unlock(void *m);
void nso_host_std_cv_notify_one(void *cv);
void nso_host_std_cv_notify_all(void *cv);
void nso_host_std_cv_wait(void *cv, void *unique_lock_ptr);
uint64_t nso_host_steady_clock_now(void);
int nso_host_pthread_cond_clockwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                                    clockid_t clock_id, const struct timespec *abstime);
void nso_host_throw_system_error(int ev, const char *what_arg);
void nso_host_libcpp_verbose_abort(const char *format, ...);
char *nso_host_setlocale(int category, const char *locale);
char *nso_host_getenv(const char *name);
char *nso_host_nl_langinfo(int item);
void nso_host_cxa_pure_virtual(void);
int nso_host_gettimeofday(struct timeval *tv, void *tz);
pid_t nso_host_getpid(void);
uint32_t nso_host_get_current_process_handle(void);
#ifdef __cplusplus
}
#endif
#endif
