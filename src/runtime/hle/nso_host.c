#include "nso_host.h"
#include "address_region.h"
#include "mutex.h"
#include "exit_registry.h"
#include "guest_process.h"
#include "guest_tp.h"
#include "../nso/native_boundary.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <sched.h>
#include <inttypes.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#if defined(__linux__) && !defined(__ANDROID__)
#include <sys/syscall.h>
#endif
#if defined(__ANDROID__) || (defined(NSO_WITH_CXX_GUARDS) && NSO_WITH_CXX_GUARDS)
/* Official NDK cxxabi.h: AArch64 uses the uint64_t guard ABI. Main's observed
 * LDARB/TBZ bit-0 fast path agrees with that ABI. All state transitions and
 * synchronization belong to the compiler runtime, never to this resolver. */
_Static_assert(sizeof(void*)==8 && _Alignof(uint64_t)==8,"guard bridge requires the verified 64-bit ABI");
extern int __cxa_guard_acquire(uint64_t *guard);
extern void __cxa_guard_release(uint64_t *guard);
extern void __cxa_guard_abort(uint64_t *guard);
#endif
static nso_import_fn extension_resolver;
/* Supplied SDK build 0023a1b0..., offset 0x86670, size 4: RET.
 * This API is a retained link reference, not an OS initialization operation.
 * The logging is diagnostic; no synthetic integer success value is returned. */
static void refer_symbol(const void *symbol) {
    fprintf(stderr,"[hle] nn::util::ReferSymbol(%p): verified SDK link anchor\n",symbol);
}
struct nn_uuid nso_host_generate_uuid(void) {
    uint32_t sp[4];
    arc4random_buf(sp, sizeof(sp));
    uint32_t w10 = sp[0];
    uint32_t w9  = sp[1];
    uint32_t w11 = sp[2];
    uint32_t w12 = sp[3];

    uint32_t w8 = 0x40000000 | (w9 & 0x0fffffff);
    uint64_t x9 = (uint64_t)(w11 & 0x0000ff3f);
    uint64_t x11 = (uint64_t)(w11 >> 16) | ((uint64_t)w12 << 16);
    w8 = (w8 >> 16) | (w8 << 16);
    uint64_t x8 = (uint64_t)w8 | ((uint64_t)w10 << 32);

    uint64_t x11_rev = __builtin_bswap64(x11);
    uint64_t x12 = x11_rev & 0xffffffff00000000ULL;
    uint64_t w11_masked = (x11_rev & 0xffffffffULL) & 0xffff0000ULL;
    uint64_t x0 = __builtin_bswap64(x8);
    x9 = x9 | x12 | w11_masked;
    uint64_t x1 = x9 | 0x80ULL;

    struct nn_uuid u = { .w0 = x0, .w1 = x1 };
    return u;
}
static bool s_time_initialized = false;
uint32_t nso_host_time_initialize(void) {
    s_time_initialized = true;
    return 0;
}
bool nso_host_time_is_initialized(void) {
    return s_time_initialized;
}
uint32_t nso_host_time_get_current_time(int64_t *out) {
    if(!out) return 0x10;
    *out = (int64_t)time(NULL);
    return 0;
}
uint32_t nso_host_time_get_device_location_name(struct nn_location_name *out) {
    if(!out) return 0x10;
    memset(out, 0, sizeof(*out));
    strncpy(out->name, "UTC", sizeof(out->name) - 1);
    return 0;
}
uint32_t nso_host_time_load_timezone_rule(void *rule, const struct nn_location_name *name) {
    (void)name;
    if(rule) memset(rule, 0, 0x1000);
    return 0;
}
int nso_host_time_get_day_of_week(int y, int m, int d) {
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) y -= 1;
    return (y + y/4 - y/100 + y/400 + t[m-1] + d) % 7;
}
uint32_t nso_host_time_to_calendar_time(struct nn_calendar_time *out_cal,
                                        struct nn_calendar_additional_info *out_info,
                                        const int64_t *posix_time,
                                        const void *rule) {
    (void)rule;
    time_t sec = posix_time ? (time_t)*posix_time : time(NULL);
    struct tm tm;
    gmtime_r(&sec, &tm);
    if(out_cal) {
        out_cal->year = (int16_t)(tm.tm_year + 1900);
        out_cal->month = (int8_t)(tm.tm_mon + 1);
        out_cal->day = (int8_t)tm.tm_mday;
        out_cal->hour = (int8_t)tm.tm_hour;
        out_cal->minute = (int8_t)tm.tm_min;
        out_cal->second = (int8_t)tm.tm_sec;
        out_cal->pad = 0;
    }
    if(out_info) {
        memset(out_info, 0, sizeof(*out_info));
        out_info->day_of_week = (uint32_t)tm.tm_wday;
        out_info->day_of_year = (int32_t)tm.tm_yday;
        strncpy(out_info->timezone_name, "UTC", sizeof(out_info->timezone_name) - 1);
        out_info->is_dst = false;
        out_info->utc_offset_seconds = 0;
    }
    return 0;
}
uint64_t nso_host_time_to_calendar_time_in_utc(const int64_t *posix_time) {
    struct nn_calendar_time cal;
    nso_host_time_to_calendar_time(&cal, NULL, posix_time, NULL);
    uint64_t ret = 0;
    memcpy(&ret, &cal, sizeof(ret));
    return ret;
}
int64_t nso_host_os_get_system_tick_frequency(void) {
    return 19200000LL;
}
int64_t nso_host_os_get_system_tick(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 19200000LL + (int64_t)ts.tv_nsec * 19200000LL / 1000000000LL;
}
void nso_host_tls_init_noop(void) {
    /* No-op C++ thread_local init for trivial / statically initialized TLS objects */
}

__attribute__((weak)) nb_scope *guest_process_current_boundary(void) {
    return NULL;
}
__attribute__((weak)) int guest_process_current_tls_template(guest_tp_template *out) {
    (void)out;
    return 0;
}
__attribute__((weak)) void guest_process_bind_child_thread(void) {}
__attribute__((weak)) void guest_process_unbind_child_thread(void) {}
__attribute__((weak)) nb_status nb_call_child(nb_scope *scope, void (*guest_entry)(void),
                                              nb_registers *registers, uintptr_t guest_sp) {
    (void)scope; (void)guest_entry; (void)registers; (void)guest_sp;
    return NB_UNSUPPORTED;
}

enum {
    NN_THREAD_TYPE_SIZE = 0x1c0,
    NN_THREAD_STATE_OFFSET = 0x40,
    NN_THREAD_PRIORITY_OFFSET = 0x44,
    NN_THREAD_STACK_OFFSET = 0x48,
    NN_THREAD_STACK_SIZE_OFFSET = 0x58,
    NN_THREAD_ARG_OFFSET = 0x60,
    NN_THREAD_ENTRY_OFFSET = 0x68,
    NN_THREAD_NAME_BUF_OFFSET = 0x180,
    NN_THREAD_NAME_PTR_OFFSET = 0x1a0,
    NN_THREAD_HANDLE_OFFSET = 0x1b0,
    NN_THREAD_ID_OFFSET = 0x1b8,
};

#define MAX_HOST_THREADS 32

typedef struct nso_host_thread {
    pthread_t host_thread;
    uint64_t tid;
    void *guest_thread_type;
    void (*guest_entry)(void *);
    void *guest_arg;
    uintptr_t guest_sp;
    size_t guest_stack_size;
    int priority;
    char name[32];
    guest_tp_mirror *tp_mirror;
    nb_scope *boundary_scope;
    pthread_mutex_t mutex;
    pthread_cond_t start_cond;
    pthread_cond_t ready_cond;
    pthread_cond_t done_cond;
    bool ready;
    bool started;
    bool finished;
    bool joined;
    bool active;
} nso_host_thread;

static pthread_mutex_t s_threads_mutex = PTHREAD_MUTEX_INITIALIZER;
static nso_host_thread s_host_threads[MAX_HOST_THREADS] = {{0}};

static uint64_t current_native_tid(void) {
#if defined(__ANDROID__)
    return (uint64_t)gettid();
#elif defined(__APPLE__)
    uint64_t tid = 0;
    pthread_threadid_np(NULL, &tid);
    return tid;
#elif defined(__linux__)
    return (uint64_t)syscall(SYS_gettid);
#else
    return (uint64_t)(uintptr_t)pthread_self();
#endif
}

static nso_host_thread *find_thread_locked(const void *thread) {
    if (!thread) return NULL;
    for (int i = 0; i < MAX_HOST_THREADS; i++) {
        if (s_host_threads[i].active && s_host_threads[i].guest_thread_type == thread) {
            return &s_host_threads[i];
        }
    }
    return NULL;
}

static void *thread_worker(void *arg) {
    nso_host_thread *th = (nso_host_thread *)arg;
    uint64_t tid = current_native_tid();

    pthread_mutex_lock(&th->mutex);
    th->tid = tid;
    if (th->guest_thread_type) {
        *(uint64_t *)((char *)th->guest_thread_type + NN_THREAD_ID_OFFSET) = tid;
    }
    th->ready = true;
    pthread_cond_broadcast(&th->ready_cond);

    while (!th->started) {
        pthread_cond_wait(&th->start_cond, &th->mutex);
    }
    pthread_mutex_unlock(&th->mutex);

    if (guest_process_bind_child_thread) {
        guest_process_bind_child_thread();
    }

    if (th->name[0]) {
#if defined(__ANDROID__)
        pthread_setname_np(pthread_self(), th->name);
#elif defined(__APPLE__)
        pthread_setname_np(th->name);
#endif
    }

    if (th->tp_mirror) {
        guest_tp_bind_current(th->tp_mirror);
        fprintf(stderr, "[hle] thread tid=%" PRIu64 " bound guest TP base=%p span=%zu\n",
                th->tid, (void *)guest_tp_mirror_base(th->tp_mirror), guest_tp_mirror_span(th->tp_mirror));
    }

    nb_scope *scope = th->boundary_scope ? th->boundary_scope :
                      (guest_process_current_boundary ? guest_process_current_boundary() : NULL);
    if (scope && nb_call_child) {
        nb_registers pkt = {0};
        pkt.x[0] = (uint64_t)(uintptr_t)th->guest_arg;
        fprintf(stderr, "[hle] child thread tid=%" PRIu64 " entering guest_entry=%p sp=0x%" PRIxPTR " arg=%p\n",
                th->tid, (void *)th->guest_entry, th->guest_sp, th->guest_arg);
        nb_status s = nb_call_child(scope, (void (*)(void))th->guest_entry, &pkt, th->guest_sp);
        fprintf(stderr, "[hle] child thread tid=%" PRIu64 " returned from guest_entry status=%d x0=0x%" PRIx64 "\n",
                th->tid, s, pkt.x[0]);
    } else {
        if (th->guest_entry) {
            th->guest_entry(th->guest_arg);
        }
    }

    if (th->tp_mirror) {
        guest_tp_unbind_current();
        th->tp_mirror = NULL;
    }
    if (guest_process_unbind_child_thread) {
        guest_process_unbind_child_thread();
    }

    pthread_mutex_lock(&th->mutex);
    th->finished = true;
    if (th->guest_thread_type) {
        *(uint8_t *)((char *)th->guest_thread_type + NN_THREAD_STATE_OFFSET) = 3; /* Finished */
    }
    pthread_cond_broadcast(&th->done_cond);
    pthread_mutex_unlock(&th->mutex);

    return NULL;
}

int32_t nso_host_create_thread(void *thread, void (*entry)(void *), void *arg,
                               void *stack, size_t stack_size, int32_t priority) {
    if (!thread || !entry || !stack || stack_size < 16384) {
        fprintf(stderr, "[hle] CreateThread invalid params: t=%p entry=%p stack=%p size=%zu prio=%d\n",
                thread, (void *)entry, stack, stack_size, priority);
        return 0x64;
    }

    memset(thread, 0, NN_THREAD_TYPE_SIZE);
    *(uint8_t *)((char *)thread + NN_THREAD_STATE_OFFSET) = 1;
    *(int16_t *)((char *)thread + NN_THREAD_PRIORITY_OFFSET) = (int16_t)priority;
    *(void **)((char *)thread + NN_THREAD_STACK_OFFSET) = stack;
    *(size_t *)((char *)thread + NN_THREAD_STACK_SIZE_OFFSET) = stack_size;
    *(void **)((char *)thread + NN_THREAD_ARG_OFFSET) = arg;
    *(void **)((char *)thread + NN_THREAD_ENTRY_OFFSET) = (void *)entry;
    ((char *)thread + NN_THREAD_NAME_BUF_OFFSET)[0] = '\0';
    *(const char **)((char *)thread + NN_THREAD_NAME_PTR_OFFSET) = (char *)thread + NN_THREAD_NAME_BUF_OFFSET;

    uintptr_t initial_sp = ((uintptr_t)stack + stack_size) & ~15ULL;

    guest_tp_template tpl = {0};
    if (!guest_process_current_tls_template || !guest_process_current_tls_template(&tpl) || tpl.total_size == 0) {
        tpl = (guest_tp_template){
            .initialized_data = NULL,
            .initialized_size = 0,
            .total_size = 0x1019,
            .alignment = 8,
            .reserved_header_size = 0x10,
        };
    }
    guest_tp_mirror *mirror = NULL;
    struct guest_tp_result tpres = guest_tp_mirror_create(&tpl, &mirror);
    if (tpres.status != GUEST_TP_OK || !mirror) {
        fprintf(stderr, "[hle] CreateThread failed to allocate guest TP mirror status=%d\n", tpres.status);
        return 0x64;
    }

    pthread_mutex_lock(&s_threads_mutex);
    nso_host_thread *th = NULL;
    for (int i = 0; i < MAX_HOST_THREADS; i++) {
        if (!s_host_threads[i].active) {
            th = &s_host_threads[i];
            break;
        }
    }
    if (!th) {
        pthread_mutex_unlock(&s_threads_mutex);
        guest_tp_mirror_free(mirror);
        fprintf(stderr, "[hle] CreateThread: out of thread slots\n");
        return 0x64;
    }

    memset(th, 0, sizeof(*th));
    th->active = true;
    th->guest_thread_type = thread;
    th->guest_entry = entry;
    th->guest_arg = arg;
    th->guest_sp = initial_sp;
    th->guest_stack_size = stack_size;
    th->priority = priority;
    th->tp_mirror = mirror;
    th->boundary_scope = guest_process_current_boundary ? guest_process_current_boundary() : NULL;
    pthread_mutex_init(&th->mutex, NULL);
    pthread_cond_init(&th->start_cond, NULL);
    pthread_cond_init(&th->ready_cond, NULL);
    pthread_cond_init(&th->done_cond, NULL);

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 1024 * 1024);
    int rc = pthread_create(&th->host_thread, &attr, thread_worker, th);
    pthread_attr_destroy(&attr);
    if (rc != 0) {
        th->active = false;
        pthread_mutex_unlock(&s_threads_mutex);
        guest_tp_mirror_free(mirror);
        fprintf(stderr, "[hle] CreateThread: pthread_create failed rc=%d\n", rc);
        return 0x64;
    }

    pthread_mutex_lock(&th->mutex);
    while (!th->ready) {
        pthread_cond_wait(&th->ready_cond, &th->mutex);
    }
    uint64_t tid = th->tid;
    pthread_mutex_unlock(&th->mutex);
    pthread_mutex_unlock(&s_threads_mutex);

    fprintf(stderr, "[hle] nn::os::CreateThread: thread=%p entry=%p arg=%p stack=%p size=0x%zx prio=%d tid=%" PRIu64 "\n",
            thread, (void *)entry, arg, stack, stack_size, priority, tid);
    return 0;
}

int32_t nso_host_create_thread_7(void *thread, void (*entry)(void *), void *arg,
                                 void *stack, size_t stack_size, int32_t priority, int32_t ideal_core) {
    (void)ideal_core;
    return nso_host_create_thread(thread, entry, arg, stack, stack_size, priority);
}

void nso_host_start_thread(void *thread) {
    if (!thread) return;
    pthread_mutex_lock(&s_threads_mutex);
    nso_host_thread *th = find_thread_locked(thread);
    if (th) {
        pthread_mutex_lock(&th->mutex);
        th->started = true;
        *(uint8_t *)((char *)thread + NN_THREAD_STATE_OFFSET) = 2; /* Started */
        pthread_cond_signal(&th->start_cond);
        fprintf(stderr, "[hle] nn::os::StartThread: thread=%p tid=%" PRIu64 "\n", thread, th->tid);
        pthread_mutex_unlock(&th->mutex);
    }
    pthread_mutex_unlock(&s_threads_mutex);
}

void nso_host_wait_thread(void *thread) {
    if (!thread) return;
    pthread_mutex_lock(&s_threads_mutex);
    nso_host_thread *th = find_thread_locked(thread);
    if (!th) {
        pthread_mutex_unlock(&s_threads_mutex);
        return;
    }
    pthread_mutex_lock(&th->mutex);
    pthread_mutex_unlock(&s_threads_mutex);

    while (!th->finished) {
        pthread_cond_wait(&th->done_cond, &th->mutex);
    }
    if (!th->joined) {
        th->joined = true;
        pthread_mutex_unlock(&th->mutex);
        pthread_join(th->host_thread, NULL);
        fprintf(stderr, "[hle] nn::os::WaitThread: thread=%p tid=%" PRIu64 " joined\n", thread, th->tid);
    } else {
        pthread_mutex_unlock(&th->mutex);
    }
}

void nso_host_destroy_thread(void *thread) {
    if (!thread) return;
    pthread_mutex_lock(&s_threads_mutex);
    nso_host_thread *th = find_thread_locked(thread);
    if (th) {
        if (!th->joined) {
            th->joined = true;
            pthread_join(th->host_thread, NULL);
        }
        if (th->tp_mirror) {
            guest_tp_mirror_free(th->tp_mirror);
            th->tp_mirror = NULL;
        }
        pthread_mutex_destroy(&th->mutex);
        pthread_cond_destroy(&th->start_cond);
        pthread_cond_destroy(&th->ready_cond);
        pthread_cond_destroy(&th->done_cond);
        th->active = false;
        fprintf(stderr, "[hle] nn::os::DestroyThread: thread=%p\n", thread);
    }
    *(uint8_t *)((char *)thread + NN_THREAD_STATE_OFFSET) = 0;
    pthread_mutex_unlock(&s_threads_mutex);
}

void nso_host_set_thread_name(void *thread, const char *name) {
    if (!thread) return;
    char *buf = (char *)thread + NN_THREAD_NAME_BUF_OFFSET;
    strncpy(buf, name ? name : "", 31);
    buf[31] = '\0';
    *(const char **)((char *)thread + NN_THREAD_NAME_PTR_OFFSET) = buf;

    pthread_mutex_lock(&s_threads_mutex);
    nso_host_thread *th = find_thread_locked(thread);
    if (th) {
        strncpy(th->name, buf, 31);
        th->name[31] = '\0';
#if defined(__ANDROID__)
        if (th->started && !th->finished) {
            pthread_setname_np(th->host_thread, th->name);
        }
#endif
    }
    pthread_mutex_unlock(&s_threads_mutex);
    fprintf(stderr, "[hle] nn::os::SetThreadName: thread=%p name='%s'\n", thread, buf);
}

void nso_host_set_thread_name_pointer(void *thread, const char *name) {
    if (!thread) return;
    *(const char **)((char *)thread + NN_THREAD_NAME_PTR_OFFSET) = name;
    fprintf(stderr, "[hle] nn::os::SetThreadNamePointer: thread=%p name='%s'\n", thread, name ? name : "");
}

const char *nso_host_get_thread_name_pointer(const void *thread) {
    if (!thread) return "";
    const char *p = *(const char **)((const char *)thread + NN_THREAD_NAME_PTR_OFFSET);
    return p ? p : "";
}

int32_t nso_host_get_thread_priority(const void *thread) {
    if (!thread) return 16;
    return *(const int16_t *)((const char *)thread + NN_THREAD_PRIORITY_OFFSET);
}

int32_t nso_host_get_thread_current_priority(const void *thread) {
    return nso_host_get_thread_priority(thread);
}

void nso_host_change_thread_priority(void *thread, int32_t priority) {
    if (!thread) return;
    *(int16_t *)((char *)thread + NN_THREAD_PRIORITY_OFFSET) = (int16_t)priority;
    pthread_mutex_lock(&s_threads_mutex);
    nso_host_thread *th = find_thread_locked(thread);
    if (th) th->priority = priority;
    pthread_mutex_unlock(&s_threads_mutex);
}

uint64_t nso_host_get_thread_id(const void *thread) {
    if (!thread) {
        return current_native_tid();
    }
    uint64_t tid = *(const uint64_t *)((const char *)thread + NN_THREAD_ID_OFFSET);
    if (tid != 0) return tid;
    pthread_mutex_lock(&s_threads_mutex);
    nso_host_thread *th = find_thread_locked(thread);
    if (th && th->tid != 0) tid = th->tid;
    pthread_mutex_unlock(&s_threads_mutex);
    return tid;
}

void nso_host_sleep_thread(int64_t nanoseconds) {
    if (nanoseconds <= 0) return;
    struct timespec ts = {
        .tv_sec = nanoseconds / 1000000000LL,
        .tv_nsec = nanoseconds % 1000000000LL
    };
    nanosleep(&ts, NULL);
}

void nso_host_yield_thread(void) {
    sched_yield();
}

void nso_host_set_thread_core_mask(void *thread, int32_t ideal_core, uint64_t affinity_mask) {
    (void)thread; (void)ideal_core; (void)affinity_mask;
}

void nso_host_get_thread_core_mask(int32_t *out_ideal_core, uint64_t *out_affinity_mask, const void *thread) {
    (void)thread;
    if (out_ideal_core) *out_ideal_core = -1;
    if (out_affinity_mask) *out_affinity_mask = 0x7ULL;
}

uint64_t nso_host_get_thread_available_core_mask(void) {
    return 0x7ULL;
}

void *nso_host_get_current_thread(void) {
    pthread_t self = pthread_self();
    pthread_mutex_lock(&s_threads_mutex);
    for (int i = 0; i < MAX_HOST_THREADS; i++) {
        if (s_host_threads[i].active && pthread_equal(s_host_threads[i].host_thread, self)) {
            void *t = s_host_threads[i].guest_thread_type;
            pthread_mutex_unlock(&s_threads_mutex);
            return t;
        }
    }
    pthread_mutex_unlock(&s_threads_mutex);
    static char s_main_thread_storage[NN_THREAD_TYPE_SIZE] = {0};
    static bool s_main_inited = false;
    if (!s_main_inited) {
        s_main_inited = true;
        *(uint8_t *)(s_main_thread_storage + NN_THREAD_STATE_OFFSET) = 2; /* Started */
        *(int16_t *)(s_main_thread_storage + NN_THREAD_PRIORITY_OFFSET) = 16;
        *(const char **)(s_main_thread_storage + NN_THREAD_NAME_PTR_OFFSET) = "MainThread";
        *(uint64_t *)(s_main_thread_storage + NN_THREAD_ID_OFFSET) = nso_host_get_thread_id(NULL);
    }
    return s_main_thread_storage;
}

int32_t nso_host_movie_get_thread_priority(void) {
    return 16;
}

void nso_host_movie_set_thread_priority(int32_t p) {
    (void)p;
}

static nn_fs_alloc_fn s_fs_alloc = NULL;
static nn_fs_free_fn s_fs_free = NULL;

void nso_host_fs_set_allocator(nn_fs_alloc_fn alloc, nn_fs_free_fn free_fn) {
    s_fs_alloc = alloc;
    s_fs_free = free_fn;
    fprintf(stderr, "[hle] nn::fs::SetAllocator: alloc=%p free=%p\n", (void *)alloc, (void *)free_fn);
}

nn_fs_alloc_fn nso_host_fs_get_allocator(void) {
    return s_fs_alloc;
}

nn_fs_free_fn nso_host_fs_get_deallocator(void) {
    return s_fs_free;
}

int32_t nso_host_fs_query_mount_rom_cache_size(size_t *out_size) {
    if (!out_size) {
        fprintf(stderr, "[hle] nn::fs::QueryMountRomCacheSize: null out pointer\n");
        return 1;
    }
    *out_size = 0x10000; /* 64 KB cache size */
    fprintf(stderr, "[hle] nn::fs::QueryMountRomCacheSize: out_size=%p -> %zu (0x%zx)\n",
            (void *)out_size, *out_size, *out_size);
    return 0;
}

#define NSO_FS_MAX_MOUNTS 8
typedef struct {
    int in_use;
    char name[64];
    void *buffer;
    size_t size;
    char host_path[512];
} nso_fs_mount_entry;

static nso_fs_mount_entry s_fs_mounts[NSO_FS_MAX_MOUNTS];

int32_t nso_host_fs_mount_rom(const char *name, void *buffer, size_t size) {
    if (!name || !name[0]) {
        fprintf(stderr, "[hle] nn::fs::MountRom: invalid or null name\n");
        return 1;
    }
    if (!buffer) {
        fprintf(stderr, "[hle] nn::fs::MountRom: null buffer pointer\n");
        return 1;
    }
    if (size < 0x10000) {
        fprintf(stderr, "[hle] nn::fs::MountRom: cache size %zu too small (min 65536)\n", size);
        return 1;
    }

    const char *resolved_path = NULL;
    const char *env_path = getenv("NN_FS_ROM_PATH");
    if (!env_path) env_path = getenv("ROMFS_PATH");

    if (env_path && env_path[0] && access(env_path, F_OK) == 0) {
        resolved_path = env_path;
    } else {
        static const char *const candidates[] = {
            "romfs",
            "./romfs",
            ".local/romfs",
            ".local/control_romfs",
            "/data/local/tmp/ffrs-rg477m-efd1123620814764ae32529c18081259/romfs",
            "/data/local/tmp/romfs",
            "."
        };
        for (size_t i = 0; i < sizeof(candidates)/sizeof(candidates[0]); i++) {
            if (access(candidates[i], F_OK) == 0) {
                resolved_path = candidates[i];
                break;
            }
        }
    }

    if (!resolved_path) {
        resolved_path = ".";
    }

    int slot = -1;
    for (int i = 0; i < NSO_FS_MAX_MOUNTS; i++) {
        if (s_fs_mounts[i].in_use && !strcmp(s_fs_mounts[i].name, name)) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        for (int i = 0; i < NSO_FS_MAX_MOUNTS; i++) {
            if (!s_fs_mounts[i].in_use) {
                slot = i;
                break;
            }
        }
    }
    if (slot < 0) {
        fprintf(stderr, "[hle] nn::fs::MountRom: out of mount slots\n");
        return 1;
    }

    s_fs_mounts[slot].in_use = 1;
    strncpy(s_fs_mounts[slot].name, name, sizeof(s_fs_mounts[slot].name) - 1);
    s_fs_mounts[slot].name[sizeof(s_fs_mounts[slot].name) - 1] = '\0';
    s_fs_mounts[slot].buffer = buffer;
    s_fs_mounts[slot].size = size;
    strncpy(s_fs_mounts[slot].host_path, resolved_path, sizeof(s_fs_mounts[slot].host_path) - 1);
    s_fs_mounts[slot].host_path[sizeof(s_fs_mounts[slot].host_path) - 1] = '\0';

    fprintf(stderr, "[hle] nn::fs::MountRom: mounted '%s' buffer=%p size=%zu (0x%zx) host_path='%s'\n",
            name, buffer, size, size, resolved_path);
    return 0;
}

int nso_host_fs_is_rom_mounted(const char *name) {
    if (!name) {
        for (int i = 0; i < NSO_FS_MAX_MOUNTS; i++) {
            if (s_fs_mounts[i].in_use) return 1;
        }
        return 0;
    }
    for (int i = 0; i < NSO_FS_MAX_MOUNTS; i++) {
        if (s_fs_mounts[i].in_use && !strcmp(s_fs_mounts[i].name, name)) return 1;
    }
    return 0;
}

const char *nso_host_fs_get_rom_mount_path(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < NSO_FS_MAX_MOUNTS; i++) {
        if (s_fs_mounts[i].in_use && !strcmp(s_fs_mounts[i].name, name))
            return s_fs_mounts[i].host_path;
    }
    return NULL;
}

int32_t nso_host_fs_unmount(const char *name) {
    if (!name || !name[0]) {
        fprintf(stderr, "[hle] nn::fs::Unmount: invalid name\n");
        return 1;
    }
    for (int i = 0; i < NSO_FS_MAX_MOUNTS; i++) {
        if (s_fs_mounts[i].in_use && !strcmp(s_fs_mounts[i].name, name)) {
            s_fs_mounts[i].in_use = 0;
            fprintf(stderr, "[hle] nn::fs::Unmount: unmounted '%s'\n", name);
            return 0;
        }
    }
    fprintf(stderr, "[hle] nn::fs::Unmount: name '%s' not found\n", name);
    return 0;
}

#define NSO_FS_MAX_OPEN_FILES 128

typedef struct {
    int in_use;
    int fd;
    int mode;
    int64_t file_size;
    char nn_path[256];
    char host_path[512];
} nso_fs_file_entry;

static nso_fs_file_entry s_fs_open_files[NSO_FS_MAX_OPEN_FILES];
static pthread_mutex_t s_fs_file_mutex = PTHREAD_MUTEX_INITIALIZER;

static int resolve_fs_path(const char *nn_path, char *out_host_path, size_t out_size) {
    if (!nn_path || !out_host_path || out_size == 0) return -1;

    const char *colon = strchr(nn_path, ':');
    const char *subpath = NULL;
    const char *mount_host_path = NULL;

    if (colon) {
        char mount_name[64];
        size_t mlen = (size_t)(colon - nn_path);
        if (mlen >= sizeof(mount_name)) mlen = sizeof(mount_name) - 1;
        memcpy(mount_name, nn_path, mlen);
        mount_name[mlen] = '\0';

        subpath = colon + 1;
        while (*subpath == '/' || *subpath == '\\') subpath++;

        for (int i = 0; i < NSO_FS_MAX_MOUNTS; i++) {
            if (s_fs_mounts[i].in_use && !strcmp(s_fs_mounts[i].name, mount_name)) {
                mount_host_path = s_fs_mounts[i].host_path;
                break;
            }
        }
        if (!mount_host_path) {
            fprintf(stderr, "[hle] resolve_fs_path: mount '%s' not found for '%s'\n", mount_name, nn_path);
            return -1;
        }
    } else {
        subpath = nn_path;
        while (*subpath == '/' || *subpath == '\\') subpath++;
        for (int i = 0; i < NSO_FS_MAX_MOUNTS; i++) {
            if (s_fs_mounts[i].in_use && !strcmp(s_fs_mounts[i].name, "rom")) {
                mount_host_path = s_fs_mounts[i].host_path;
                break;
            }
        }
        if (!mount_host_path) {
            mount_host_path = ".";
        }
    }

    char norm_subpath[512];
    size_t j = 0;
    for (size_t i = 0; subpath[i] && j + 1 < sizeof(norm_subpath); i++) {
        norm_subpath[j++] = (subpath[i] == '\\') ? '/' : subpath[i];
    }
    norm_subpath[j] = '\0';

    if (norm_subpath[0] == '\0') {
        snprintf(out_host_path, out_size, "%s", mount_host_path);
    } else {
        snprintf(out_host_path, out_size, "%s/%s", mount_host_path, norm_subpath);
    }
    return 0;
}

static nso_fs_file_entry *allocate_file_slot(void) {
    for (int i = 0; i < NSO_FS_MAX_OPEN_FILES; i++) {
        if (!s_fs_open_files[i].in_use) {
            s_fs_open_files[i].in_use = 1;
            s_fs_open_files[i].fd = -1;
            return &s_fs_open_files[i];
        }
    }
    return NULL;
}

static nso_fs_file_entry *validate_file_handle(nn_fs_file_handle handle) {
    if (!handle.handle) return NULL;
    uintptr_t ptr = (uintptr_t)handle.handle;
    uintptr_t base = (uintptr_t)&s_fs_open_files[0];
    uintptr_t size = sizeof(nso_fs_file_entry);
    if (ptr < base || ptr >= base + NSO_FS_MAX_OPEN_FILES * size) return NULL;
    if ((ptr - base) % size != 0) return NULL;
    nso_fs_file_entry *entry = (nso_fs_file_entry *)handle.handle;
    if (!entry->in_use || entry->fd < 0) return NULL;
    return entry;
}

int32_t nso_host_fs_open_file(nn_fs_file_handle *out_handle, const char *path, int32_t mode) {
    if (!out_handle || !path) {
        fprintf(stderr, "[hle] nn::fs::OpenFile: null pointer argument (out_handle=%p, path=%p)\n",
                (void *)out_handle, (const void *)path);
        return 1;
    }
    if (path[0] == '\0') {
        fprintf(stderr, "[hle] nn::fs::OpenFile: empty path\n");
        return 0x202; /* ResultPathNotFound */
    }

    char host_path[512];
    if (resolve_fs_path(path, host_path, sizeof(host_path)) != 0) {
        fprintf(stderr, "[hle] nn::fs::OpenFile: path='%s' mode=%d -> resolve mount failed\n", path, mode);
        return 0x202; /* ResultPathNotFound */
    }

    pthread_mutex_lock(&s_fs_file_mutex);
    nso_fs_file_entry *entry = allocate_file_slot();
    if (!entry) {
        pthread_mutex_unlock(&s_fs_file_mutex);
        fprintf(stderr, "[hle] nn::fs::OpenFile: path='%s' -> out of file slots\n", path);
        return 1;
    }

    int flags = 0;
    if ((mode & 1) && (mode & 2)) {
        flags = O_RDWR;
    } else if (mode & 2) {
        flags = O_WRONLY;
    } else {
        flags = O_RDONLY;
    }
    if (mode & 4) {
        flags |= O_APPEND;
    }
#if defined(O_CLOEXEC)
    flags |= O_CLOEXEC;
#endif

    int fd = open(host_path, flags);
    if (fd < 0) {
        entry->in_use = 0;
        pthread_mutex_unlock(&s_fs_file_mutex);
        fprintf(stderr, "[hle] nn::fs::OpenFile: path='%s' mode=%d (host_path='%s') -> open failed errno=%d (%s)\n",
                path, mode, host_path, errno, strerror(errno));
        return 0x202; /* ResultPathNotFound */
    }

    struct stat st;
    int64_t fsz = 0;
    if (fstat(fd, &st) == 0) {
        fsz = (int64_t)st.st_size;
    }
    entry->fd = fd;
    entry->mode = mode;
    entry->file_size = fsz;
    strncpy(entry->nn_path, path, sizeof(entry->nn_path) - 1);
    entry->nn_path[sizeof(entry->nn_path) - 1] = '\0';
    strncpy(entry->host_path, host_path, sizeof(entry->host_path) - 1);
    entry->host_path[sizeof(entry->host_path) - 1] = '\0';

    out_handle->handle = (void *)entry;
    pthread_mutex_unlock(&s_fs_file_mutex);

    fprintf(stderr, "[hle] nn::fs::OpenFile: path='%s' mode=%d (host_path='%s') size=%lld -> handle=%p (fd=%d)\n",
            path, mode, host_path, (long long)fsz, (void *)entry, fd);
    return 0;
}

int32_t nso_host_fs_get_file_size(int64_t *out_size, nn_fs_file_handle handle) {
    if (!out_size) {
        fprintf(stderr, "[hle] nn::fs::GetFileSize: null out pointer\n");
        return 1;
    }
    pthread_mutex_lock(&s_fs_file_mutex);
    nso_fs_file_entry *entry = validate_file_handle(handle);
    if (!entry) {
        pthread_mutex_unlock(&s_fs_file_mutex);
        fprintf(stderr, "[hle] nn::fs::GetFileSize: invalid file handle %p\n", handle.handle);
        return 1;
    }
    struct stat st;
    if (fstat(entry->fd, &st) == 0) {
        entry->file_size = (int64_t)st.st_size;
    }
    *out_size = entry->file_size;
    pthread_mutex_unlock(&s_fs_file_mutex);
    fprintf(stderr, "[hle] nn::fs::GetFileSize: handle=%p size=%lld\n", handle.handle, (long long)*out_size);
    return 0;
}

int32_t nso_host_fs_read_file(size_t *out_bytes_read, nn_fs_file_handle handle, int64_t offset, void *buffer, size_t size) {
    if (!buffer) {
        fprintf(stderr, "[hle] nn::fs::ReadFile: null buffer pointer\n");
        return 1;
    }
    if (offset < 0) {
        fprintf(stderr, "[hle] nn::fs::ReadFile: negative offset %lld\n", (long long)offset);
        return 1;
    }
    pthread_mutex_lock(&s_fs_file_mutex);
    nso_fs_file_entry *entry = validate_file_handle(handle);
    if (!entry) {
        pthread_mutex_unlock(&s_fs_file_mutex);
        fprintf(stderr, "[hle] nn::fs::ReadFile: invalid file handle %p\n", handle.handle);
        return 1;
    }
    int fd = entry->fd;
    pthread_mutex_unlock(&s_fs_file_mutex);

    ssize_t n = pread(fd, buffer, size, (off_t)offset);
    if (n < 0) {
        fprintf(stderr, "[hle] nn::fs::ReadFile: pread failed fd=%d offset=%lld size=%zu errno=%d (%s)\n",
                fd, (long long)offset, size, errno, strerror(errno));
        return 1;
    }
    if (out_bytes_read) {
        *out_bytes_read = (size_t)n;
    }
    fprintf(stderr, "[hle] nn::fs::ReadFile: handle=%p offset=%lld req_size=%zu read_bytes=%zd\n",
            handle.handle, (long long)offset, size, n);
    return 0;
}

int32_t nso_host_fs_read_file_no_out(nn_fs_file_handle handle, int64_t offset, void *buffer, size_t size) {
    return nso_host_fs_read_file(NULL, handle, offset, buffer, size);
}

void nso_host_fs_close_file(nn_fs_file_handle handle) {
    pthread_mutex_lock(&s_fs_file_mutex);
    nso_fs_file_entry *entry = validate_file_handle(handle);
    if (!entry) {
        pthread_mutex_unlock(&s_fs_file_mutex);
        fprintf(stderr, "[hle] nn::fs::CloseFile: invalid file handle %p\n", handle.handle);
        return;
    }
    int fd = entry->fd;
    char path[256];
    strncpy(path, entry->nn_path, sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';

    entry->in_use = 0;
    entry->fd = -1;
    pthread_mutex_unlock(&s_fs_file_mutex);

    if (fd >= 0) {
        close(fd);
    }
    fprintf(stderr, "[hle] nn::fs::CloseFile: closed handle=%p (path='%s')\n", handle.handle, path);
}

int32_t nso_host_fs_flush_file(nn_fs_file_handle handle) {
    pthread_mutex_lock(&s_fs_file_mutex);
    nso_fs_file_entry *entry = validate_file_handle(handle);
    if (!entry) {
        pthread_mutex_unlock(&s_fs_file_mutex);
        fprintf(stderr, "[hle] nn::fs::FlushFile: invalid file handle %p\n", handle.handle);
        return 1;
    }
    int fd = entry->fd;
    pthread_mutex_unlock(&s_fs_file_mutex);

    fsync(fd);
    fprintf(stderr, "[hle] nn::fs::FlushFile: flushed handle=%p\n", handle.handle);
    return 0;
}

int32_t nso_host_fs_set_file_size(nn_fs_file_handle handle, int64_t size) {
    if (size < 0) return 1;
    pthread_mutex_lock(&s_fs_file_mutex);
    nso_fs_file_entry *entry = validate_file_handle(handle);
    if (!entry) {
        pthread_mutex_unlock(&s_fs_file_mutex);
        fprintf(stderr, "[hle] nn::fs::SetFileSize: invalid file handle %p\n", handle.handle);
        return 1;
    }
    int fd = entry->fd;
    int res = ftruncate(fd, (off_t)size);
    if (res == 0) {
        entry->file_size = size;
    }
    pthread_mutex_unlock(&s_fs_file_mutex);
    fprintf(stderr, "[hle] nn::fs::SetFileSize: handle=%p size=%lld res=%d\n", handle.handle, (long long)size, res);
    return res == 0 ? 0 : 1;
}

int32_t nso_host_fs_write_file(nn_fs_file_handle handle, int64_t offset, const void *data, size_t size, const void *option) {
    (void)option;
    if (!data || offset < 0) return 1;
    pthread_mutex_lock(&s_fs_file_mutex);
    nso_fs_file_entry *entry = validate_file_handle(handle);
    if (!entry) {
        pthread_mutex_unlock(&s_fs_file_mutex);
        fprintf(stderr, "[hle] nn::fs::WriteFile: invalid file handle %p\n", handle.handle);
        return 1;
    }
    int fd = entry->fd;
    pthread_mutex_unlock(&s_fs_file_mutex);

    ssize_t n = pwrite(fd, data, size, (off_t)offset);
    if (n < 0 || (size_t)n != size) {
        fprintf(stderr, "[hle] nn::fs::WriteFile: pwrite failed fd=%d size=%zu written=%zd\n", fd, size, n);
        return 1;
    }
    fprintf(stderr, "[hle] nn::fs::WriteFile: handle=%p offset=%lld size=%zu\n", handle.handle, (long long)offset, size);
    return 0;
}

int32_t nso_host_fs_get_entry_type(int32_t *out_entry_type, const char *path) {
    if (!out_entry_type || !path) {
        fprintf(stderr, "[hle] nn::fs::GetEntryType: null pointer argument\n");
        return 1;
    }
    char host_path[512];
    if (resolve_fs_path(path, host_path, sizeof(host_path)) != 0) {
        fprintf(stderr, "[hle] nn::fs::GetEntryType: resolve failed for '%s'\n", path);
        return 0x202; /* ResultPathNotFound */
    }
    struct stat st;
    if (stat(host_path, &st) != 0) {
        fprintf(stderr, "[hle] nn::fs::GetEntryType: path='%s' (host_path='%s') stat failed errno=%d\n",
                path, host_path, errno);
        return 0x202; /* ResultPathNotFound */
    }
    if (S_ISDIR(st.st_mode)) {
        *out_entry_type = 0; /* Directory */
    } else {
        *out_entry_type = 1; /* File */
    }
    fprintf(stderr, "[hle] nn::fs::GetEntryType: path='%s' (host_path='%s') -> %s (%d)\n",
            path, host_path, *out_entry_type == 0 ? "Directory" : "File", *out_entry_type);
    return 0;
}

void nso_host_fs_disable_global_file_data_cache(void) {
    fprintf(stderr, "[hle] nn::fs::DisableGlobalFileDataCache\n");
}

void nso_host_fs_enable_global_file_data_cache(void *buffer, size_t size) {
    fprintf(stderr, "[hle] nn::fs::EnableGlobalFileDataCache: buffer=%p size=%zu\n", buffer, size);
}

void nso_host_set_extension_resolver(nso_import_fn resolver) {extension_resolver=resolver;}
uint64_t nso_host_resolve(uint32_t index,const char *name) {
    if(!name)return 0;
#if defined(__ANDROID__) || (defined(NSO_WITH_CXX_GUARDS) && NSO_WITH_CXX_GUARDS)
    if(!strcmp(name,"__cxa_guard_acquire"))return (uint64_t)(uintptr_t)&__cxa_guard_acquire;
    if(!strcmp(name,"__cxa_guard_release"))return (uint64_t)(uintptr_t)&__cxa_guard_release;
    if(!strcmp(name,"__cxa_guard_abort"))return (uint64_t)(uintptr_t)&__cxa_guard_abort;
#endif
    if(!strcmp(name,"_ZN2nn2fs12SetAllocatorEPFPvmEPFvS1_mE") || !strcmp(name,"nnfsSetAllocator"))
        return (uint64_t)(uintptr_t)&nso_host_fs_set_allocator;
    if(!strcmp(name,"_ZN2nn2fs22QueryMountRomCacheSizeEPm") || !strcmp(name,"nnfsQueryMountRomCacheSize"))
        return (uint64_t)(uintptr_t)&nso_host_fs_query_mount_rom_cache_size;
    if(!strcmp(name,"_ZN2nn2fs8MountRomEPKcPvm") || !strcmp(name,"nnfsMountRom"))
        return (uint64_t)(uintptr_t)&nso_host_fs_mount_rom;
    if(!strcmp(name,"_ZN2nn2fs7UnmountEPKc") || !strcmp(name,"nnfsUnmount"))
        return (uint64_t)(uintptr_t)&nso_host_fs_unmount;
    if(!strcmp(name,"_ZN2nn2fs8OpenFileEPNS0_10FileHandleEPKci") || !strcmp(name,"nnfsOpenFile"))
        return (uint64_t)(uintptr_t)&nso_host_fs_open_file;
    if(!strcmp(name,"_ZN2nn2fs11GetFileSizeEPlNS0_10FileHandleE") || !strcmp(name,"nnfsGetFileSize"))
        return (uint64_t)(uintptr_t)&nso_host_fs_get_file_size;
    if(!strcmp(name,"_ZN2nn2fs8ReadFileEPmNS0_10FileHandleElPvm") || !strcmp(name,"nnfsReadFile"))
        return (uint64_t)(uintptr_t)&nso_host_fs_read_file;
    if(!strcmp(name,"_ZN2nn2fs8ReadFileENS0_10FileHandleElPvm"))
        return (uint64_t)(uintptr_t)&nso_host_fs_read_file_no_out;
    if(!strcmp(name,"_ZN2nn2fs9CloseFileENS0_10FileHandleE") || !strcmp(name,"nnfsCloseFile"))
        return (uint64_t)(uintptr_t)&nso_host_fs_close_file;
    if(!strcmp(name,"_ZN2nn2fs9FlushFileENS0_10FileHandleE") || !strcmp(name,"nnfsFlushFile"))
        return (uint64_t)(uintptr_t)&nso_host_fs_flush_file;
    if(!strcmp(name,"_ZN2nn2fs11SetFileSizeENS0_10FileHandleEl") || !strcmp(name,"nnfsSetFileSize"))
        return (uint64_t)(uintptr_t)&nso_host_fs_set_file_size;
    if(!strcmp(name,"_ZN2nn2fs9WriteFileENS0_10FileHandleElPKvmRKNS0_11WriteOptionE") || !strcmp(name,"nnfsWriteFile"))
        return (uint64_t)(uintptr_t)&nso_host_fs_write_file;
    if(!strcmp(name,"_ZN2nn2fs12GetEntryTypeEPNS0_18DirectoryEntryTypeEPKc") || !strcmp(name,"nnfsGetEntryType"))
        return (uint64_t)(uintptr_t)&nso_host_fs_get_entry_type;
    if(!strcmp(name,"_ZN2nn2fs26DisableGlobalFileDataCacheEv"))
        return (uint64_t)(uintptr_t)&nso_host_fs_disable_global_file_data_cache;
    if(!strcmp(name,"_ZN2nn2fs25EnableGlobalFileDataCacheEPvm"))
        return (uint64_t)(uintptr_t)&nso_host_fs_enable_global_file_data_cache;
    if(!strcmp(name,"_ZN2nn4util11ReferSymbolEPKv") || !strcmp(name,"nnutilReferSymbol"))
        return (uint64_t)(uintptr_t)&refer_symbol;
    if(!strcmp(name,"_ZN2nn4util12GenerateUuidEv"))
        return (uint64_t)(uintptr_t)&nso_host_generate_uuid;
    if(!strcmp(name,"_ZN2nn4time10InitializeEv"))
        return (uint64_t)(uintptr_t)&nso_host_time_initialize;
    if(!strcmp(name,"_ZN2nn4time13IsInitializedEv"))
        return (uint64_t)(uintptr_t)&nso_host_time_is_initialized;
    if(!strcmp(name,"_ZN2nn4time23StandardUserSystemClock14GetCurrentTimeEPNS0_9PosixTimeE"))
        return (uint64_t)(uintptr_t)&nso_host_time_get_current_time;
    if(!strcmp(name,"_ZN2nn4time21GetDeviceLocationNameEPNS0_12LocationNameE"))
        return (uint64_t)(uintptr_t)&nso_host_time_get_device_location_name;
    if(!strcmp(name,"_ZN2nn4time16LoadTimeZoneRuleEPNS0_12TimeZoneRuleERKNS0_12LocationNameE"))
        return (uint64_t)(uintptr_t)&nso_host_time_load_timezone_rule;
    if(!strcmp(name,"_ZN2nn4time14ToCalendarTimeEPNS0_12CalendarTimeEPNS0_22CalendarAdditionalInfoERKNS0_9PosixTimeERKNS0_12TimeZoneRuleE"))
        return (uint64_t)(uintptr_t)&nso_host_time_to_calendar_time;
    if(!strcmp(name,"_ZN2nn4time19ToCalendarTimeInUtcERKNS0_9PosixTimeE"))
        return (uint64_t)(uintptr_t)&nso_host_time_to_calendar_time_in_utc;
    if(!strcmp(name,"_ZN2nn4time12GetDayOfWeekEiii"))
        return (uint64_t)(uintptr_t)&nso_host_time_get_day_of_week;
    if(!strcmp(name,"_ZN2nn2os22GetSystemTickFrequencyEv") || !strcmp(name,"nnosGetSystemTickFrequency"))
        return (uint64_t)(uintptr_t)&nso_host_os_get_system_tick_frequency;
    if(!strcmp(name,"_ZN2nn2os13GetSystemTickEv") || !strcmp(name,"nnosGetSystemTick"))
        return (uint64_t)(uintptr_t)&nso_host_os_get_system_tick;
    if(!strcmp(name,"_ZN2nn2os21AllocateAddressRegionEPmm"))
        return (uint64_t)(uintptr_t)&nso_host_allocate_address_region;
    if(!strcmp(name,"_ZN2nn2os17FreeAddressRegionEm"))
        return (uint64_t)(uintptr_t)&nso_host_free_address_region;
    if(!strcmp(name,"_ZN2nn2os15InitializeMutexEPNS0_9MutexTypeEbi") || !strcmp(name,"nnosInitializeMutex"))
        return (uint64_t)(uintptr_t)&nso_host_initialize_mutex;
    if(!strcmp(name,"_ZN2nn2os13FinalizeMutexEPNS0_9MutexTypeE") || !strcmp(name,"nnosFinalizeMutex"))
        return (uint64_t)(uintptr_t)&nso_host_finalize_mutex;
    if(!strcmp(name,"_ZN2nn2os9LockMutexEPNS0_9MutexTypeE") || !strcmp(name,"nnosLockMutex"))
        return (uint64_t)(uintptr_t)&nso_host_lock_mutex;
    if(!strcmp(name,"_ZN2nn2os12TryLockMutexEPNS0_9MutexTypeE") || !strcmp(name,"nnosTryLockMutex"))
        return (uint64_t)(uintptr_t)&nso_host_try_lock_mutex;
    if(!strcmp(name,"_ZN2nn2os11UnlockMutexEPNS0_9MutexTypeE") || !strcmp(name,"nnosUnlockMutex"))
        return (uint64_t)(uintptr_t)&nso_host_unlock_mutex;
    if(!strcmp(name,"_ZN2nn2os28IsMutexLockedByCurrentThreadEPKNS0_9MutexTypeE"))
        return (uint64_t)(uintptr_t)&nso_host_is_mutex_locked_by_current_thread;
    if(!strcmp(name,"_ZN2nn2os26InitializeReaderWriterLockEPNS0_20ReaderWriterLockTypeE") || !strcmp(name,"nnosInitializeReaderWriterLock"))
        return (uint64_t)(uintptr_t)&nso_host_initialize_rwlock;
    if(!strcmp(name,"_ZN2nn2os24FinalizeReaderWriterLockEPNS0_20ReaderWriterLockTypeE") || !strcmp(name,"nnosFinalizeReaderWriterLock"))
        return (uint64_t)(uintptr_t)&nso_host_finalize_rwlock;
    if(!strcmp(name,"_ZN2nn2os15AcquireReadLockEPNS0_20ReaderWriterLockTypeE") || !strcmp(name,"nnosAcquireReadLock"))
        return (uint64_t)(uintptr_t)&nso_host_acquire_read_lock;
    if(!strcmp(name,"_ZN2nn2os18TryAcquireReadLockEPNS0_20ReaderWriterLockTypeE") || !strcmp(name,"nnosTryAcquireReadLock"))
        return (uint64_t)(uintptr_t)&nso_host_try_acquire_read_lock;
    if(!strcmp(name,"_ZN2nn2os15ReleaseReadLockEPNS0_20ReaderWriterLockTypeE") || !strcmp(name,"nnosReleaseReadLock"))
        return (uint64_t)(uintptr_t)&nso_host_release_read_lock;
    if(!strcmp(name,"_ZN2nn2os16AcquireWriteLockEPNS0_20ReaderWriterLockTypeE") || !strcmp(name,"nnosAcquireWriteLock"))
        return (uint64_t)(uintptr_t)&nso_host_acquire_write_lock;
    if(!strcmp(name,"_ZN2nn2os19TryAcquireWriteLockEPNS0_20ReaderWriterLockTypeE") || !strcmp(name,"nnosTryAcquireWriteLock"))
        return (uint64_t)(uintptr_t)&nso_host_try_acquire_write_lock;
    if(!strcmp(name,"_ZN2nn2os16ReleaseWriteLockEPNS0_20ReaderWriterLockTypeE") || !strcmp(name,"nnosReleaseWriteLock"))
        return (uint64_t)(uintptr_t)&nso_host_release_write_lock;
    if(!strcmp(name,"_ZN2nn2os30IsWriteLockHeldByCurrentThreadEPKNS0_20ReaderWriterLockTypeE") ||
       !strcmp(name,"_ZN2nn2os35IsWriteLockHeldByCurrentThreadEPKNS0_20ReaderWriterLockTypeE") ||
       !strcmp(name,"_ZN2nn2os29IsReaderWriterLockOwnerThreadEPKNS0_20ReaderWriterLockTypeE"))
        return (uint64_t)(uintptr_t)&nso_host_is_write_lock_held_by_current_thread;
    if(!strcmp(name,"_ZN2nn2os14IsReadLockHeldEPKNS0_20ReaderWriterLockTypeE"))
        return (uint64_t)(uintptr_t)&nso_host_is_read_lock_held;
    if(!strcmp(name,"_ZN2nn2os12CreateThreadEPNS0_10ThreadTypeEPFvPvES3_S3_mi") || !strcmp(name,"nnosCreateThread"))
        return (uint64_t)(uintptr_t)&nso_host_create_thread;
    if(!strcmp(name,"_ZN2nn2os12CreateThreadEPNS0_10ThreadTypeEPFvPvES3_S3_mii"))
        return (uint64_t)(uintptr_t)&nso_host_create_thread_7;
    if(!strcmp(name,"_ZN2nn2os11StartThreadEPNS0_10ThreadTypeE") || !strcmp(name,"nnosStartThread"))
        return (uint64_t)(uintptr_t)&nso_host_start_thread;
    if(!strcmp(name,"_ZN2nn2os10WaitThreadEPNS0_10ThreadTypeE") || !strcmp(name,"nnosWaitThread"))
        return (uint64_t)(uintptr_t)&nso_host_wait_thread;
    if(!strcmp(name,"_ZN2nn2os13DestroyThreadEPNS0_10ThreadTypeE") || !strcmp(name,"nnosDestroyThread"))
        return (uint64_t)(uintptr_t)&nso_host_destroy_thread;
    if(!strcmp(name,"_ZN2nn2os13SetThreadNameEPNS0_10ThreadTypeEPKc"))
        return (uint64_t)(uintptr_t)&nso_host_set_thread_name;
    if(!strcmp(name,"_ZN2nn2os20SetThreadNamePointerEPNS0_10ThreadTypeEPKc") || !strcmp(name,"nnosSetThreadNamePointer"))
        return (uint64_t)(uintptr_t)&nso_host_set_thread_name_pointer;
    if(!strcmp(name,"_ZN2nn2os20GetThreadNamePointerEPKNS0_10ThreadTypeE"))
        return (uint64_t)(uintptr_t)&nso_host_get_thread_name_pointer;
    if(!strcmp(name,"_ZN2nn2os17GetThreadPriorityEPKNS0_10ThreadTypeE") || !strcmp(name,"nnosGetThreadPriority"))
        return (uint64_t)(uintptr_t)&nso_host_get_thread_priority;
    if(!strcmp(name,"_ZN2nn2os24GetThreadCurrentPriorityEPKNS0_10ThreadTypeE"))
        return (uint64_t)(uintptr_t)&nso_host_get_thread_current_priority;
    if(!strcmp(name,"_ZN2nn2os20ChangeThreadPriorityEPNS0_10ThreadTypeEi"))
        return (uint64_t)(uintptr_t)&nso_host_change_thread_priority;
    if(!strcmp(name,"_ZN2nn2os11GetThreadIdEPKNS0_10ThreadTypeE") || !strcmp(name,"nnosGetThreadId"))
        return (uint64_t)(uintptr_t)&nso_host_get_thread_id;
    if(!strcmp(name,"_ZN2nn2os16GetCurrentThreadEv") || !strcmp(name,"nnosGetCurrentThread"))
        return (uint64_t)(uintptr_t)&nso_host_get_current_thread;
    if(!strcmp(name,"_ZN2nn2os11SleepThreadENS_8TimeSpanE") || !strcmp(name,"nnosSleepThread"))
        return (uint64_t)(uintptr_t)&nso_host_sleep_thread;
    if(!strcmp(name,"_ZN2nn2os11YieldThreadEv"))
        return (uint64_t)(uintptr_t)&nso_host_yield_thread;
    if(!strcmp(name,"_ZN2nn2os17SetThreadCoreMaskEPNS0_10ThreadTypeEim") || !strcmp(name,"nnosSetThreadCoreMask"))
        return (uint64_t)(uintptr_t)&nso_host_set_thread_core_mask;
    if(!strcmp(name,"_ZN2nn2os17GetThreadCoreMaskEPiPmPKNS0_10ThreadTypeE") || !strcmp(name,"nnosGetThreadCoreMask"))
        return (uint64_t)(uintptr_t)&nso_host_get_thread_core_mask;
    if(!strcmp(name,"_ZN2nn2os26GetThreadAvailableCoreMaskEv"))
        return (uint64_t)(uintptr_t)&nso_host_get_thread_available_core_mask;
    if(!strcmp(name,"_ZN5movie17GetThreadPriorityEv"))
        return (uint64_t)(uintptr_t)&nso_host_movie_get_thread_priority;
    if(!strcmp(name,"_ZN5movie17SetThreadPriorityEi"))
        return (uint64_t)(uintptr_t)&nso_host_movie_set_thread_priority;
    if(!strcmp(name,"atexit"))return (uint64_t)(uintptr_t)&nso_host_atexit;
    if(!strcmp(name,"__cxa_atexit"))return (uint64_t)(uintptr_t)&nso_host_cxa_atexit;
    if(!strcmp(name,"__cxa_finalize"))return (uint64_t)(uintptr_t)&nso_host_cxa_finalize;
    if(!strcmp(name,"_ZTHN13LowLevelTasks5FTask10ActiveTaskE") ||
       !strcmp(name,"_ZTHN13LowLevelTasks7Private20FOversubscriptionTls26bIsOversubscriptionAllowedE") ||
       !strcmp(name,"_ZTHN30FOverridableSerializationLogic20OverriddenPropertiesE") ||
       !strcmp(name,"_ZTHN30FOverridableSerializationLogic28bUseOverridableSerializationE") ||
       !strcmp(name,"_ZTHN17FPropertyTagScope18CurrentPropertyTagE"))
        return (uint64_t)(uintptr_t)&nso_host_tls_init_noop;
    if(!strcmp(name,"exit"))return (uint64_t)(uintptr_t)&nso_host_exit;
    if(!strcmp(name,"_Exit") || !strcmp(name,"_exit"))return (uint64_t)(uintptr_t)&nso_host_immediate_exit;
    return extension_resolver?extension_resolver(index,name):0;
}
