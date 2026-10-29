#ifndef NSO_HOST_H
#define NSO_HOST_H
#include "../nso/nso_loader.h"
#include <stdbool.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
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
int nso_host_vsnprintf(char *out, size_t bytes, const char *format, const void *guest_va_list);
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
int64_t nso_host_os_convert_to_timespan(int64_t ticks);
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

/* movie::SetAllocator registers the game's own allocator with the SDK movie
 * library. ABI verified by an exact clang mangling round trip against
 * _ZN5movie12SetAllocatorEPFPvmmS0_EPFvS0_S0_EPFS0_S0_mS0_ES0_: four general
 * register arguments, no return value. The retail implementation lives in the
 * SDK's movie module (subsdk0); this runtime never loads it, so nothing calls
 * the registered callbacks back. The registration is still recorded so a
 * later movie:: stub that really allocates uses the allocator the guest chose.
 * The guest callbacks are never invoked from this host stub. */
typedef void *(*movie_alloc_fn)(size_t, size_t, void *);
typedef void (*movie_free_fn)(void *, void *);
typedef void *(*movie_realloc_fn)(void *, size_t, void *);
void nso_host_movie_set_allocator(movie_alloc_fn alloc, movie_free_fn free_fn,
                                  movie_realloc_fn realloc_fn, void *userdata);
movie_alloc_fn nso_host_movie_get_allocator(void);
movie_free_fn nso_host_movie_get_deallocator(void);
movie_realloc_fn nso_host_movie_get_reallocator(void);

/* nv::SetGraphicsAllocator and nv::SetGraphicsDevtoolsAllocator (main NSO
 * import indexes 350 and 351).
 *
 * Both take the same four arguments as the already bound
 * movie::SetAllocator (import 74): an allocation callback, a free callback,
 * a realloc callback and a userdata pointer, and neither returns anything.
 * The guest calls the two of them back to back in one straight-line block
 * (0x1434c90 and 0x1434ca4), so both are reached on every boot.
 *
 * The registration is recorded, not invoked: calling a guest callback from a
 * host stub needs the guest thread and the guest TLS context and would
 * corrupt the guest's thread state. The same rule already applies to the
 * movie allocator registration. */
typedef void *(*nv_alloc_fn)(size_t, size_t, void *);
typedef void (*nv_free_fn)(void *, void *);
typedef void *(*nv_realloc_fn)(void *, size_t, void *);
void nso_host_nv_set_graphics_allocator(nv_alloc_fn alloc,nv_free_fn free_fn,
                                         nv_realloc_fn realloc_fn,void *userdata);
void nso_host_nv_set_graphics_devtools_allocator(nv_alloc_fn alloc,nv_free_fn free_fn,
                                                  nv_realloc_fn realloc_fn,void *userdata);
nv_alloc_fn nso_host_nv_get_graphics_allocator(void);
nv_free_fn nso_host_nv_get_graphics_deallocator(void);
nv_realloc_fn nso_host_nv_get_graphics_reallocator(void);
void *nso_host_nv_get_graphics_allocator_userdata(void);
nv_alloc_fn nso_host_nv_get_graphics_devtools_allocator(void);
nv_free_fn nso_host_nv_get_graphics_devtools_deallocator(void);
nv_realloc_fn nso_host_nv_get_graphics_devtools_reallocator(void);
void *nso_host_nv_get_graphics_devtools_userdata(void);

/* nv::InitializeGraphics (main NSO import index 352).
 *
 * Takes the NVN initialisation parameter block and its size, and returns
 * nothing: the single guest call site (0x1434cb4) loads x0 and x1 right
 * before the call and never consumes the result. The guest passes its own
 * parameter block at module offset 0x82d6000 and the size 0x800000.
 *
 * The stub records both values and does not claim that a graphics device
 * exists. The real NVN device is the T04 Vulkan bridge, which is not in this
 * runtime yet. */
void nso_host_nv_initialize_graphics(void *param,uint64_t size);
void *nso_host_nv_get_initialize_graphics_param(void);
uint64_t nso_host_nv_get_initialize_graphics_size(void);

/* nvnBootstrapLoader (main NSO import index 353): the only NVN symbol the
 * main NSO imports dynamically. One argument (the entry point name) and one
 * result (a function pointer).
 *
 * This runtime has no NVN device, so the loader is a null device backend:
 * nvnDeviceInitialize reports failure, nvnDeviceGetProcAddress serves the
 * whole "nvn" namespace with a do-nothing entry point (the guest never
 * checks the pointer it gets back, so returning NULL there would send it to
 * pc=0), and every name outside that namespace is refused. */
void *nso_host_nvn_bootstrap_loader(const char *name);
void *nso_host_nvn_device_get_proc_address(void *device,const char *name);
uint64_t nso_host_nvn_resolved_count(void);
uint64_t nso_host_nvn_call_count(void);
void *nso_host_movie_get_allocator_userdata(void);

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
struct nso_local_uid;
int32_t nso_host_fs_set_save_root(const char *path);
bool nso_host_fs_is_save_data_existing(uint64_t application,const struct nso_local_uid *uid);
int32_t nso_host_fs_ensure_save_data(const struct nso_local_uid *uid);
int32_t nso_host_fs_mount_save_data(const char *name, const struct nso_local_uid *uid);
int32_t nso_host_fs_mount_save_data_read_only(const char *name, uint64_t app_id, const struct nso_local_uid *uid);
int32_t nso_host_fs_extend_save_data(const struct nso_local_uid *uid, int64_t save_size, int64_t journal_size);
int32_t nso_host_fs_commit_save_data(const char *name);
int32_t nso_host_fs_create_file(const char *path, int64_t size);
int32_t nso_host_fs_get_save_data_size(int64_t *size, int64_t *journal, const struct nso_local_uid *uid);
int32_t nso_host_fs_get_free_space_size(int64_t *size, const char *path);

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
void nso_host_os_bind_system_event(void *event);
bool nso_host_os_try_wait_system_event(void *event);
void nso_host_os_signal_system_event(void *event);
void nso_host_os_destroy_system_event(void *event);

int32_t nso_host_nifm_initialize(void);
bool nso_host_nifm_is_initialized(void);

/* nn::ldn::GetState() (main NSO import index 88).
 *
 * Zero arguments, returns the 32-bit local wireless state. The Itanium
 * mangling carries no return type, so the return type comes from the guest:
 * all four call sites (0x15bd4, 0x51dca8, 0x51ecb8, 0x942edc and 0x943198
 * in total) read the result in w0 with cbz, cbnz, cmp or cinc.
 *
 * The value 0 means "no Nintendo local wireless session" in every one of
 * them:
 *   0x15bd4  the refresh helper returns 0 straight away when the state is
 *            non-zero and only continues into the internet path on state 0.
 *   0x51dca8 the state feeds a three-valued network classification:
 *            internet available (3), local wireless session (2) or
 *            neither (1, state 0).
 *   0x51ecb8 the same classification in the notification handler.
 *   0x942edc the state is checked before the refresh helper runs.
 *   0x943198 the local wireless IPv4 address is only requested for the
 *            states 3 and 5, so state 0 takes the internet path instead.
 *
 * The RG477M has no Nintendo local wireless radio and runs no local wireless
 * session, so 0 is the state it has to report. No enumerator name is
 * claimed, because the tree carries no authoritative nn::ldn state list. */
int32_t nso_host_ldn_get_state(void);

int32_t nso_host_socket_initialize(void *work_buffer, size_t work_buffer_size, size_t allocator_size, int32_t concurrency);
bool nso_host_socket_is_initialized(void);

/* nn::socket event family. The descriptors handed to the guest are real host
 * descriptors so that nn::socket::Close and nn::socket::Poll operate on the
 * same object. See the "nn::socket event family" block in nso_host.c for the
 * retail ABI evidence and the documented host-mapping coverage. */
int32_t nso_host_socket_event_fd(uint64_t initval, int32_t flags);
int32_t nso_host_socket_event_fd_write(int32_t fd, uint64_t value);
int32_t nso_host_socket_event_fd_read(int32_t fd, uint64_t *out_value);
int32_t nso_host_socket_close(int32_t fd);
int32_t nso_host_socket_get_last_error(void);
void nso_host_socket_set_last_error(int32_t error);
int32_t nso_host_socket_poll(void *fds, uint64_t count, int32_t timeout_ms);
int32_t nso_host_socket_poll_c(void *fds, int32_t count, int32_t timeout_ms);

int32_t nso_host_account_initialize(void);
bool nso_host_account_is_initialized(void);
/* Runtime-local offline profile, not a Nintendo account or credential. The
 * 16-byte UID and 24-byte handle match nxnative's SDK account bridge. */
struct nso_local_uid { uint64_t words[2]; };
struct nso_local_user_handle { uint64_t words[3]; };
bool nso_host_account_try_open_preselected_user(struct nso_local_user_handle *out);
uint32_t nso_host_account_get_user_id(struct nso_local_uid *out, const struct nso_local_user_handle *handle);
void nso_host_account_close_user(const struct nso_local_user_handle *handle);
int32_t nso_host_account_push_open_users(void);
int32_t nso_host_account_pop_open_users(int32_t *out_count, struct nso_local_user_handle *out_handles, int32_t max_handles);

int nso_host_setjmp(void *env);
void nso_host_longjmp(void *env, int val);
int nso_host_sigsetjmp(void *env, int savemask);
void nso_host_siglongjmp(void *env, int val);

typedef struct {
    char code[8];
} nn_settings_language_code;

int32_t nso_host_oe_initialize(void);
bool nso_host_oe_is_initialized(void);
nn_settings_language_code nso_host_oe_get_desired_language(void);

/* nn::oe::CpuBoostMode (main NSO import index 75).
 *
 * ABI: void SetCpuBoostMode(nn::oe::CpuBoostMode) - one 32-bit enum argument in
 * w0 and no return value. Two independent confirmations: an exact clang mangling
 * round trip reproduces _ZN2nn2oe15SetCpuBoostModeENS0_12CpuBoostModeE for that
 * declaration, and all six guest call sites discard w0 after the call.
 *
 * The enum stays opaque on purpose. The guest only ever passes 0 or 1, and no
 * authoritative enumerator list exists in-tree, so the stub records the raw
 * value instead of inventing names for it. */
typedef int32_t nn_oe_cpu_boost_mode;
void nso_host_oe_set_cpu_boost_mode(nn_oe_cpu_boost_mode mode);
nn_oe_cpu_boost_mode nso_host_oe_get_cpu_boost_mode(void);

/* nn::hid keyboard and mouse subsystem initialisation (main NSO import indexes
 * 234 and 235).
 *
 * ABI: void InitializeKeyboard(void) and void InitializeMouse(void) - no
 * arguments and no return value. The Itanium mangling carries no return type,
 * so the return type was settled from the call sites instead: 0x51f618 and
 * 0x51f61c are adjacent BLs into the two import stubs and neither result is
 * consumed afterwards.
 *
 * These stubs only record that the subsystem was initialised. The state
 * readers (imports 236 GetKeyboardState and 237 GetMouseState) run every frame
 * and carry real data semantics, so they are deliberately not faked here. */
void nso_host_hid_initialize_keyboard(void);
bool nso_host_hid_keyboard_initialized(void);
void nso_host_hid_initialize_mouse(void);
bool nso_host_hid_mouse_initialized(void);

/* nn::oe focus and notification setup (main NSO import indexes 222 to 226).
 *
 * The guest calls these five from one straight-line, unconditional sequence
 * (0x51da10, then 0x51da38, 0x51da40, 0x51da48 and 0x51da50 with only the
 * already bound nn::oe::Initialize between them), so every one of them is
 * provably reached on each boot.
 *
 * ABI, settled from the guest code rather than from the mangled name, because
 * the Itanium mangling carries no return type:
 *   222 nn::oe::GetCurrentFocusState()                            -> u32 in w0
 *   223 nn::oe::SetPerformanceModeChangedNotificationEnabled(bool)  -> void
 *   224 nn::oe::SetOperationModeChangedNotificationEnabled(bool)    -> void
 *   225 nn::oe::SetFocusHandlingMode(FocusHandlingMode)              -> void
 *   226 nn::oe::SetResumeNotificationEnabled(bool)                   -> void
 * The three setters take their argument in w0 (the guest passes 1, 1 and 2)
 * and their result is never consumed.
 *
 * Android focus callbacks now supply 1 (focused), 2 (unfocused), or 3
 * (background), matching the reference host. Focus notifications are queued
 * only when requested; an empty TryPop returns false and leaves output alone.
 * Operation/performance changes have no host producer yet. */
void nso_host_oe_enter_exit_section(void);
void nso_host_oe_leave_exit_section(void);
bool nso_host_oe_host_exit_ready(bool request);
uint32_t nso_host_oe_get_current_focus_state(void);
int64_t nso_host_oe_get_program_total_active_time(void);
void nso_host_oe_set_host_focus(uint32_t state);
bool nso_host_oe_try_pop_notification_message(uint32_t *out);
void nso_host_oe_set_performance_mode_changed_notification_enabled(bool enabled);
void nso_host_oe_set_operation_mode_changed_notification_enabled(bool enabled);
void nso_host_oe_set_focus_handling_mode(int32_t mode);
void nso_host_oe_set_resume_notification_enabled(bool enabled);
bool nso_host_oe_performance_mode_changed_notification_enabled(void);
bool nso_host_oe_operation_mode_changed_notification_enabled(void);
int32_t nso_host_oe_focus_handling_mode(void);
bool nso_host_oe_resume_notification_enabled(void);

/* nn::oe::GetOperationMode() (main NSO import index 227).
 *
 * Zero arguments, returns the 32-bit operation mode the guest compares
 * against 1 at both call sites (0x51dc84 and 0x51ee40).
 *
 * The value is settled from the guest code, not from the mangled name,
 * because the Itanium mangling carries no return type. The guest uses the
 * result in exactly one way: it caches "mode == 1" as a byte flag at
 * 0x7c90614, and the four readers of that flag (0x51e98c, 0x51f06c,
 * 0x523e98, 0x525104) each select a screen-size scale factor of either
 * 0.026873588 (flag set) or 0.0 (flag clear). The factor multiplies the
 * runtime screen width and height, so the flag-clear branch degenerates
 * the scaled screen size to zero, which no released game can use as its
 * normal state. The working state is therefore the flag-set one, so the
 * value returned here is 1. No enumerator name is claimed for it, because
 * no authoritative enumerator list is available in-tree. */
uint32_t nso_host_oe_get_operation_mode(void);

/* nn::oe performance configuration (main NSO import indexes 239 and 240).
 *
 * nn::oe::SetPerformanceConfiguration(PerformanceMode mode, int clock) takes
 * two 32-bit arguments and returns nothing: every one of the four guest
 * call sites (0x520518, 0x520524, 0x5205bc and 0x5205c8) loads w0 and w1
 * right before the call and never consumes the result. The guest asks for
 * mode 0 with clock 0x20003 or 0x20004 and then mode 1 with clock 0x10001.
 *
 * The stub records the request and does not change the real CPU or GPU
 * frequency. That is the same rule the already bound nn::oe::SetCpuBoostMode
 * (import 75) follows: the Android governor owns the frequency and the
 * cpufreq sysfs nodes need root.
 *
 * nn::oe::GetPerformanceMode() takes no argument and returns the 32-bit
 * performance mode; its single guest call site (0x5203c4) compares the
 * result against 1. It reports the mode that was requested last, which is
 * what the reference platform does, because the guest asks for a mode and
 * then reads it back. The initial value is 0, the mode in effect before the
 * guest asked for anything. */
void nso_host_oe_set_performance_configuration(int32_t mode,int32_t clock);
int32_t nso_host_oe_get_performance_mode(void);
int32_t nso_host_oe_performance_clock(void);

/* nn::vi display bridge (main NSO import indexes 228 to 232).
 *
 * The guest builds one display object (0x60 bytes) and calls these five
 * imports in one straight-line block: Initialize, OpenDefaultDisplay,
 * GetDisplayVsyncEvent, CreateLayer, GetNativeWindow. All five return
 * nothing: after every one of them the next instruction overwrites x0 or
 * loads x1 and none of them reads a result, so the Itanium mangling carries
 * no return type.
 *
 * The guest stores the handles this bridge writes and hands them back to the
 * other four calls; it never dereferences them. The handles are opaque host
 * tokens that carry a magic value, so a wrong or stale handle is detected
 * instead of being silently accepted.
 *
 * The platform window is never created here. It is the window the Activity
 * published, read through nso_host_native_window_get(). */
typedef struct nso_vi_display nso_vi_display;
typedef struct nso_vi_layer nso_vi_layer;

void nso_host_vi_initialize(void);
void nso_host_vi_open_default_display(nso_vi_display **out_display);
/* The event argument is the guest's own nn::os::SystemEventType object (the
 * guest passes the address of its field), not a slot the host writes a
 * pointer into. The host initialises that object in place. */
void nso_host_vi_get_display_vsync_event(void *event, nso_vi_display *display);
void nso_host_vi_create_layer(nso_vi_layer **out_layer, nso_vi_display *display);
void nso_host_vi_get_native_window(void **out_window, nso_vi_layer *layer);
uint32_t nso_host_vi_get_latest_frame_number(uint64_t *out_frame_number, const void *layer);
int nso_host_vi_list_frame_history(void *out_frame_infos, int count, const void *layer);
/* Raises the vsync event of the display the guest opened. This is a host
 * entry point, not a guest import: it may only be called after a real
 * present, because the event stands for a frame that was actually drawn. */
void nso_host_vi_signal_vsync_event(void);

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

/* nn::diag::detail::AbortImpl (main NSO import index 12).
 *
 * The guest calls this when the game reaches a fatal error, and it does not
 * expect the call to return: the RG477M v62 boot shows the guest falling
 * straight through into a udf #0 trap right after the call, because the stub
 * used to print the message and hand control back. The stub therefore ends
 * the run, with the game's own message on stderr.
 *
 * The exit code is the one a real abort() produces, that is 128 + SIGABRT. */
#define NN_DIAG_ABORT_EXIT_CODE 134
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
uint32_t nso_host_nn_result_is_failure(uint32_t result);
uint32_t nso_host_nn_result_is_success(uint32_t result);
uint32_t nso_host_nn_result_get_module(uint32_t result);
uint32_t nso_host_nn_result_get_description(uint32_t result);
uint32_t nso_host_nn_result_success(void);

int32_t nso_host_ssl_initialize(void);
int32_t nso_host_ssl_finalize(void);
bool nso_host_ssl_is_initialized(void);

/* stdio: data-object bindings and the print/write entry points. The stream
 * objects resolve to the address of a host FILE* mirror that the guest
 * dereferences; the print entry points take an explicit va_list because the
 * import gateway rebuilds it from the guest registers. */
uint64_t nso_host_stdio_object(const char *name);
int nso_host_vprintf(const char *fmt, va_list ap);
int nso_host_vfprintf(FILE *stream, const char *fmt, va_list ap);
int nso_host_vsprintf(char *buf, const char *fmt, va_list ap);
int nso_host_fflush(FILE *stream);
size_t nso_host_fwrite(const void *ptr, size_t size, size_t count, FILE *stream);
int nso_host_fputs(const char *text, FILE *stream);
int nso_host_fputc(int c, FILE *stream);
int nso_host_puts(const char *text);
#ifdef __cplusplus
}
#endif
#endif
