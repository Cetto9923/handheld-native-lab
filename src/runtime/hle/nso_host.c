#include "nso_host.h"
#include "address_region.h"
#include "mutex.h"
#include "exit_registry.h"
#include "guest_process.h"
#include "guest_tp.h"
#include "../nso/native_boundary.h"
#include "../nso/nso_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <pthread.h>
#include <sched.h>
#include <inttypes.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
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

    /* Guest frames live in the guest stack this thread was handed, not in the
     * host thread stack. The guest stack pointer starts at its top and grows
     * down, so the walkable range runs from the base to that top. */
    if (th->guest_sp && th->guest_stack_size) {
        nso_fault_scope_set_thread_stack(th->guest_sp - th->guest_stack_size, th->guest_sp);
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

/* movie::SetAllocator: the guest registers its own allocator with the SDK
 * movie library. The retail implementation is a forwarder inside the SDK's
 * movie module (subsdk0); this runtime never loads that module, so no real
 * movie library exists here to read the registration back. The registration is
 * still recorded faithfully, because it is the guest's real state change and
 * a later movie:: stub that actually allocates has to use the allocator the
 * guest chose rather than a host malloc. The guest callbacks are only stored:
 * invoking them from a host stub needs the guest thread and guest TLS context
 * and would corrupt the guest's thread state. */
static movie_alloc_fn s_movie_alloc = NULL;
static movie_free_fn s_movie_free = NULL;
static movie_realloc_fn s_movie_realloc = NULL;
static void *s_movie_allocator_userdata = NULL;

void nso_host_movie_set_allocator(movie_alloc_fn alloc, movie_free_fn free_fn,
                                  movie_realloc_fn realloc_fn, void *userdata) {
    s_movie_alloc = alloc;
    s_movie_free = free_fn;
    s_movie_realloc = realloc_fn;
    s_movie_allocator_userdata = userdata;
    fprintf(stderr, "[hle] movie::SetAllocator: alloc=%p free=%p realloc=%p userdata=%p\n",
            (void *)(uintptr_t)alloc, (void *)(uintptr_t)free_fn,
            (void *)(uintptr_t)realloc_fn, userdata);
}

movie_alloc_fn nso_host_movie_get_allocator(void) {
    return s_movie_alloc;
}

movie_free_fn nso_host_movie_get_deallocator(void) {
    return s_movie_free;
}

movie_realloc_fn nso_host_movie_get_reallocator(void) {
    return s_movie_realloc;
}

void *nso_host_movie_get_allocator_userdata(void) {
    return s_movie_allocator_userdata;
}

/* nv::SetGraphicsAllocator and nv::SetGraphicsDevtoolsAllocator: the guest
 * registers the allocator that the NVN graphics library will use for its
 * heap, once for the graphics library and once for its devtools channel.
 *
 * Both signatures are exactly the shape of the already bound
 * movie::SetAllocator (import 74), and the guest calls the two of them back
 * to back in one straight-line block (0x1434c90 and 0x1434ca4, with only the
 * argument setup in between), so both are reached on every boot.
 *
 * The guest callbacks are only stored, exactly like the movie registration:
 * invoking them from a host stub needs the guest thread and the guest TLS
 * context and would corrupt the guest's thread state. The registration is
 * still recorded faithfully, because it is the guest's real state change and
 * a later NVN stub that actually allocates has to use the allocator the
 * guest chose rather than a host malloc. */
static nv_alloc_fn s_nv_alloc = NULL;
static nv_free_fn s_nv_free = NULL;
static nv_realloc_fn s_nv_realloc = NULL;
static void *s_nv_allocator_userdata = NULL;
static nv_alloc_fn s_nv_devtools_alloc = NULL;
static nv_free_fn s_nv_devtools_free = NULL;
static nv_realloc_fn s_nv_devtools_realloc = NULL;
static void *s_nv_devtools_userdata = NULL;

static void record_nv_allocator(const char *label,nv_alloc_fn alloc,nv_free_fn free_fn,
                                 nv_realloc_fn realloc_fn,void *userdata,
                                 nv_alloc_fn *slot_alloc,nv_free_fn *slot_free,
                                 nv_realloc_fn *slot_realloc,void **slot_userdata) {
    *slot_alloc=alloc;
    *slot_free=free_fn;
    *slot_realloc=realloc_fn;
    *slot_userdata=userdata;
    fprintf(stderr,"[hle] %s: alloc=%p free=%p realloc=%p userdata=%p\n",label,
            (void *)(uintptr_t)alloc,(void *)(uintptr_t)free_fn,
            (void *)(uintptr_t)realloc_fn,userdata);
}

void nso_host_nv_set_graphics_allocator(nv_alloc_fn alloc,nv_free_fn free_fn,
                                         nv_realloc_fn realloc_fn,void *userdata) {
    record_nv_allocator("nv::SetGraphicsAllocator",alloc,free_fn,realloc_fn,userdata,
                        &s_nv_alloc,&s_nv_free,&s_nv_realloc,&s_nv_allocator_userdata);
}

void nso_host_nv_set_graphics_devtools_allocator(nv_alloc_fn alloc,nv_free_fn free_fn,
                                                  nv_realloc_fn realloc_fn,void *userdata) {
    record_nv_allocator("nv::SetGraphicsDevtoolsAllocator",alloc,free_fn,realloc_fn,userdata,
                        &s_nv_devtools_alloc,&s_nv_devtools_free,&s_nv_devtools_realloc,
                        &s_nv_devtools_userdata);
}

nv_alloc_fn nso_host_nv_get_graphics_allocator(void) { return s_nv_alloc; }
nv_free_fn nso_host_nv_get_graphics_deallocator(void) { return s_nv_free; }
nv_realloc_fn nso_host_nv_get_graphics_reallocator(void) { return s_nv_realloc; }
void *nso_host_nv_get_graphics_allocator_userdata(void) { return s_nv_allocator_userdata; }
nv_alloc_fn nso_host_nv_get_graphics_devtools_allocator(void) { return s_nv_devtools_alloc; }
nv_free_fn nso_host_nv_get_graphics_devtools_deallocator(void) { return s_nv_devtools_free; }
nv_realloc_fn nso_host_nv_get_graphics_devtools_reallocator(void) { return s_nv_devtools_realloc; }
void *nso_host_nv_get_graphics_devtools_userdata(void) { return s_nv_devtools_userdata; }

/* nv::InitializeGraphics (main NSO import index 352).
 *
 * The guest hands the NVN library its initialisation parameter block and
 * the size of that block, and the call has a single site (0x1434cb4) whose
 * result is never consumed, so the ABI is two arguments and no return:
 * x0 is the parameter block (the guest passes its own data at module offset
 * 0x82d6000) and x1 is the size (the guest passes 0x800000).
 *
 * The stub records both and does not claim that a graphics device exists:
 * the real NVN device is the T04 Vulkan bridge, which is not in this runtime
 * yet. The recorded values are what the guest actually asked for, so the
 * evidence stays usable once that bridge lands. */
static void *s_nv_initialize_graphics_param = NULL;
static uint64_t s_nv_initialize_graphics_size = 0;

void nso_host_nv_initialize_graphics(void *param,uint64_t size) {
    s_nv_initialize_graphics_param=param;
    s_nv_initialize_graphics_size=size;
    fprintf(stderr,"[hle] nv::InitializeGraphics: param=%p size=0x%" PRIx64 " recorded (no NVN graphics device exists in this runtime yet)\n",param,size);
}

void *nso_host_nv_get_initialize_graphics_param(void) { return s_nv_initialize_graphics_param; }
uint64_t nso_host_nv_get_initialize_graphics_size(void) { return s_nv_initialize_graphics_size; }

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

_Static_assert(sizeof(nn_fs_directory_entry) == 0x310, "nn_fs_directory_entry size must be 0x310");
_Static_assert(offsetof(nn_fs_directory_entry, name) == 0, "name offset");
_Static_assert(offsetof(nn_fs_directory_entry, type) == 0x304, "type offset");
_Static_assert(offsetof(nn_fs_directory_entry, file_size) == 0x308, "file_size offset");

#define NSO_FS_MAX_OPEN_DIRS 64

typedef struct {
    int in_use;
    int mode;
    char nn_path[256];
    char host_path[512];
    nn_fs_directory_entry *entries;
    size_t total_entries;
    size_t cursor;
} nso_fs_dir_entry;

static nso_fs_dir_entry s_fs_open_dirs[NSO_FS_MAX_OPEN_DIRS];
static pthread_mutex_t s_fs_dir_mutex = PTHREAD_MUTEX_INITIALIZER;

static nso_fs_dir_entry *allocate_dir_slot(void) {
    for (int i = 0; i < NSO_FS_MAX_OPEN_DIRS; i++) {
        if (!s_fs_open_dirs[i].in_use) {
            s_fs_open_dirs[i].in_use = 1;
            s_fs_open_dirs[i].entries = NULL;
            s_fs_open_dirs[i].total_entries = 0;
            s_fs_open_dirs[i].cursor = 0;
            return &s_fs_open_dirs[i];
        }
    }
    return NULL;
}

static nso_fs_dir_entry *validate_dir_handle(nn_fs_directory_handle handle) {
    if (!handle.handle) return NULL;
    uintptr_t ptr = (uintptr_t)handle.handle;
    uintptr_t base = (uintptr_t)&s_fs_open_dirs[0];
    uintptr_t size = sizeof(nso_fs_dir_entry);
    if (ptr < base || ptr >= base + NSO_FS_MAX_OPEN_DIRS * size) return NULL;
    if ((ptr - base) % size != 0) return NULL;
    nso_fs_dir_entry *entry = (nso_fs_dir_entry *)handle.handle;
    if (!entry->in_use) return NULL;
    return entry;
}

int32_t nso_host_fs_open_directory(nn_fs_directory_handle *out_handle, const char *path, int32_t mode) {
    if (!out_handle || !path) {
        fprintf(stderr, "[hle] nn::fs::OpenDirectory: null pointer argument (out_handle=%p, path=%p)\n",
                (void *)out_handle, (const void *)path);
        return 1;
    }
    if (path[0] == '\0') {
        fprintf(stderr, "[hle] nn::fs::OpenDirectory: empty path\n");
        return 0x202; /* ResultPathNotFound */
    }

    char host_path[512];
    if (resolve_fs_path(path, host_path, sizeof(host_path)) != 0) {
        fprintf(stderr, "[hle] nn::fs::OpenDirectory: path='%s' mode=%d -> resolve mount failed\n", path, mode);
        return 0x202; /* ResultPathNotFound */
    }

    struct stat st;
    if (stat(host_path, &st) != 0) {
        fprintf(stderr, "[hle] nn::fs::OpenDirectory: path='%s' mode=%d (host_path='%s') stat failed errno=%d (%s)\n",
                path, mode, host_path, errno, strerror(errno));
        return 0x202; /* ResultPathNotFound */
    }

    if (!S_ISDIR(st.st_mode)) {
        fprintf(stderr, "[hle] nn::fs::OpenDirectory: path='%s' mode=%d (host_path='%s') is not a directory (mode=0%o)\n",
                path, mode, host_path, st.st_mode);
        return 0x202; /* ResultPathNotFound */
    }

    DIR *d = opendir(host_path);
    if (!d) {
        fprintf(stderr, "[hle] nn::fs::OpenDirectory: path='%s' mode=%d (host_path='%s') opendir failed errno=%d (%s)\n",
                path, mode, host_path, errno, strerror(errno));
        return 0x202;
    }

    /* Pass 1: count matching entries */
    size_t count = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;

        char child_path[1024];
        snprintf(child_path, sizeof(child_path), "%s/%s", host_path, de->d_name);
        struct stat cst;
        if (stat(child_path, &cst) != 0) continue;

        int is_dir = S_ISDIR(cst.st_mode);
        if (is_dir && !(mode & 1)) continue;
        if (!is_dir && !(mode & 2)) continue;
        count++;
    }

    nn_fs_directory_entry *entries_buf = NULL;
    if (count > 0) {
        entries_buf = (nn_fs_directory_entry *)calloc(count, sizeof(nn_fs_directory_entry));
        if (!entries_buf) {
            closedir(d);
            fprintf(stderr, "[hle] nn::fs::OpenDirectory: path='%s' calloc failed for %zu entries\n", path, count);
            return 1;
        }

        rewinddir(d);
        size_t idx = 0;
        while ((de = readdir(d)) != NULL && idx < count) {
            if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;

            char child_path[1024];
            snprintf(child_path, sizeof(child_path), "%s/%s", host_path, de->d_name);
            struct stat cst;
            if (stat(child_path, &cst) != 0) continue;

            int is_dir = S_ISDIR(cst.st_mode);
            if (is_dir && !(mode & 1)) continue;
            if (!is_dir && !(mode & 2)) continue;

            memset(&entries_buf[idx], 0, sizeof(nn_fs_directory_entry));
            strncpy(entries_buf[idx].name, de->d_name, sizeof(entries_buf[idx].name) - 1);
            entries_buf[idx].name[sizeof(entries_buf[idx].name) - 1] = '\0';
            entries_buf[idx].type = is_dir ? 0 : 1;
            entries_buf[idx].file_size = is_dir ? 0 : (int64_t)cst.st_size;
            idx++;
        }
        count = idx;
    }
    closedir(d);

    pthread_mutex_lock(&s_fs_dir_mutex);
    nso_fs_dir_entry *entry = allocate_dir_slot();
    if (!entry) {
        pthread_mutex_unlock(&s_fs_dir_mutex);
        if (entries_buf) free(entries_buf);
        fprintf(stderr, "[hle] nn::fs::OpenDirectory: path='%s' -> out of directory slots\n", path);
        return 1;
    }

    entry->mode = mode;
    strncpy(entry->nn_path, path, sizeof(entry->nn_path) - 1);
    entry->nn_path[sizeof(entry->nn_path) - 1] = '\0';
    strncpy(entry->host_path, host_path, sizeof(entry->host_path) - 1);
    entry->host_path[sizeof(entry->host_path) - 1] = '\0';
    entry->entries = entries_buf;
    entry->total_entries = count;
    entry->cursor = 0;

    out_handle->handle = (void *)entry;
    pthread_mutex_unlock(&s_fs_dir_mutex);

    fprintf(stderr, "[hle] nn::fs::OpenDirectory: path='%s' mode=%d (host_path='%s') total_entries=%zu -> handle=%p\n",
            path, mode, host_path, count, (void *)entry);
    return 0;
}

int32_t nso_host_fs_get_directory_entry_count(int64_t *out_count, nn_fs_directory_handle handle) {
    if (!out_count) {
        fprintf(stderr, "[hle] nn::fs::GetDirectoryEntryCount: null out_count pointer\n");
        return 1;
    }
    pthread_mutex_lock(&s_fs_dir_mutex);
    nso_fs_dir_entry *entry = validate_dir_handle(handle);
    if (!entry) {
        pthread_mutex_unlock(&s_fs_dir_mutex);
        fprintf(stderr, "[hle] nn::fs::GetDirectoryEntryCount: invalid directory handle %p\n", handle.handle);
        return 1;
    }
    *out_count = (int64_t)entry->total_entries;
    pthread_mutex_unlock(&s_fs_dir_mutex);

    fprintf(stderr, "[hle] nn::fs::GetDirectoryEntryCount: handle=%p -> count=%lld\n",
            handle.handle, (long long)*out_count);
    return 0;
}

int32_t nso_host_fs_read_directory(int64_t *out_entries_read, nn_fs_directory_entry *entries,
                                   nn_fs_directory_handle handle, int64_t max_entries) {
    if (!entries) {
        fprintf(stderr, "[hle] nn::fs::ReadDirectory: null entries buffer\n");
        return 1;
    }
    if (max_entries <= 0) {
        if (out_entries_read) *out_entries_read = 0;
        return 0;
    }
    pthread_mutex_lock(&s_fs_dir_mutex);
    nso_fs_dir_entry *entry = validate_dir_handle(handle);
    if (!entry) {
        pthread_mutex_unlock(&s_fs_dir_mutex);
        fprintf(stderr, "[hle] nn::fs::ReadDirectory: invalid directory handle %p\n", handle.handle);
        return 1;
    }

    size_t available = 0;
    if (entry->cursor < entry->total_entries) {
        available = entry->total_entries - entry->cursor;
    }
    size_t to_read = (size_t)max_entries;
    if (to_read > available) {
        to_read = available;
    }

    for (size_t i = 0; i < to_read; i++) {
        memcpy(&entries[i], &entry->entries[entry->cursor + i], sizeof(nn_fs_directory_entry));
    }
    entry->cursor += to_read;
    size_t cur = entry->cursor;
    size_t tot = entry->total_entries;
    pthread_mutex_unlock(&s_fs_dir_mutex);

    if (out_entries_read) {
        *out_entries_read = (int64_t)to_read;
    }
    fprintf(stderr, "[hle] nn::fs::ReadDirectory: handle=%p req_max=%lld read_entries=%zu (cursor=%zu/%zu)\n",
            handle.handle, (long long)max_entries, to_read, cur, tot);
    return 0;
}

void nso_host_fs_close_directory(nn_fs_directory_handle handle) {
    pthread_mutex_lock(&s_fs_dir_mutex);
    nso_fs_dir_entry *entry = validate_dir_handle(handle);
    if (!entry) {
        pthread_mutex_unlock(&s_fs_dir_mutex);
        fprintf(stderr, "[hle] nn::fs::CloseDirectory: invalid directory handle %p\n", handle.handle);
        return;
    }
    char path[256];
    strncpy(path, entry->nn_path, sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';

    if (entry->entries) {
        free(entry->entries);
        entry->entries = NULL;
    }
    entry->in_use = 0;
    entry->total_entries = 0;
    entry->cursor = 0;
    pthread_mutex_unlock(&s_fs_dir_mutex);

    fprintf(stderr, "[hle] nn::fs::CloseDirectory: closed handle=%p (path='%s')\n", handle.handle, path);
}

int32_t nso_host_fs_create_directory(const char *path) {
    if (!path) {
        fprintf(stderr, "[hle] nn::fs::CreateDirectory: null path argument\n");
        return 1;
    }
    char host_path[512];
    if (resolve_fs_path(path, host_path, sizeof(host_path)) != 0) {
        fprintf(stderr, "[hle] nn::fs::CreateDirectory: resolve failed for '%s'\n", path);
        return 0x202;
    }
    struct stat st;
    if (stat(host_path, &st) == 0) {
        fprintf(stderr, "[hle] nn::fs::CreateDirectory: path='%s' already exists (host_path='%s')\n", path, host_path);
        return 0x402; /* ResultPathAlreadyExists */
    }
    if (mkdir(host_path, 0755) != 0) {
        fprintf(stderr, "[hle] nn::fs::CreateDirectory: path='%s' (host_path='%s') mkdir failed errno=%d (%s)\n",
                path, host_path, errno, strerror(errno));
        return 0x202;
    }
    fprintf(stderr, "[hle] nn::fs::CreateDirectory: path='%s' (host_path='%s') created\n", path, host_path);
    return 0;
}

void nso_host_fs_disable_global_file_data_cache(void) {
    fprintf(stderr, "[hle] nn::fs::DisableGlobalFileDataCache\n");
}

void nso_host_fs_enable_global_file_data_cache(void *buffer, size_t size) {
    fprintf(stderr, "[hle] nn::fs::EnableGlobalFileDataCache: buffer=%p size=%zu\n", buffer, size);
}

int32_t nso_host_aoc_count_add_on_content(void) {
    fprintf(stderr, "[hle] nn::aoc::CountAddOnContent: returning 0 (no add-on content installed)\n");
    return 0;
}

int32_t nso_host_aoc_list_add_on_content(int32_t *out_indices, int32_t offset, int32_t count) {
    if (count > 0 && !out_indices) {
        fprintf(stderr, "[hle] nn::aoc::ListAddOnContent: null out_indices with count=%d\n", count);
        return 0;
    }
    fprintf(stderr, "[hle] nn::aoc::ListAddOnContent: out_indices=%p offset=%d count=%d -> returning 0\n",
            (void *)out_indices, offset, count);
    return 0;
}

int32_t nso_host_fs_query_mount_add_on_content_cache_size(size_t *out_size, int32_t target_index) {
    if (!out_size) {
        fprintf(stderr, "[hle] nn::fs::QueryMountAddOnContentCacheSize: null out_size\n");
        return 1;
    }
    *out_size = 0;
    fprintf(stderr, "[hle] nn::fs::QueryMountAddOnContentCacheSize: target_index=%d -> out_size=0\n", target_index);
    return 0;
}

int32_t nso_host_fs_mount_add_on_content(const char *name, int32_t target_index, void *buffer, size_t size) {
    fprintf(stderr, "[hle] nn::fs::MountAddOnContent: name='%s' target_index=%d buffer=%p size=%zu -> no aoc installed\n",
            name ? name : "(null)", target_index, buffer, size);
    return 0x202; /* ResultPathNotFound */
}

/* nn::os::SystemEvent is a separate SDK object from nn::os::Event: the guest
 * owns the object and hands its address to the SDK calls, while the host owns
 * the signalling state. The host never rewrites the guest object, so the real
 * nn::os::SystemEvent layout stays exactly as the game shipped it. */
typedef struct nso_system_event_entry {
    void *object;
    bool signaled;
    bool initialized;
    struct nso_system_event_entry *next;
} nso_system_event_entry;

static pthread_mutex_t s_system_event_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_system_event_cond = PTHREAD_COND_INITIALIZER;
static nso_system_event_entry *s_system_events = NULL;

static nso_system_event_entry *find_system_event_locked(void *object) {
    for (nso_system_event_entry *e = s_system_events; e; e = e->next) {
        if (e->object == object) return e;
    }
    return NULL;
}

/* Register the object if the SDK handed us one we have not seen. An event the
 * host has not been told about starts unsignaled, exactly like a system event
 * that has never been raised. */
static nso_system_event_entry *borrow_system_event(void *object) {
    if (!object) return NULL;
    pthread_mutex_lock(&s_system_event_mutex);
    nso_system_event_entry *e = find_system_event_locked(object);
    if (!e) {
        e = (nso_system_event_entry *)calloc(1, sizeof(*e));
        if (e) {
            e->object = object;
            e->signaled = false;
            e->initialized = true;
            e->next = s_system_events;
            s_system_events = e;
        }
    }
    pthread_mutex_unlock(&s_system_event_mutex);
    return e;
}

void nso_host_aoc_get_add_on_content_list_changed_event(void *out_event) {
    if (!out_event) {
        fprintf(stderr, "[hle] nn::aoc::GetAddOnContentListChangedEvent: null out_event\n");
        return;
    }
    nso_system_event_entry *e = borrow_system_event(out_event);
    if (!e) {
        fprintf(stderr, "[hle] nn::aoc::GetAddOnContentListChangedEvent: out_event=%p -> out of memory\n",
                (void *)out_event);
        return;
    }
    pthread_mutex_lock(&s_system_event_mutex);
    /* No add-on content is installed and this host cannot install any, so the
     * add-on content list can never change. The event stays unsignaled and a
     * waiter blocks, which is the retail-card behaviour of the real SDK. */
    e->signaled = false;
    e->initialized = true;
    pthread_mutex_unlock(&s_system_event_mutex);

    fprintf(stderr, "[hle] nn::aoc::GetAddOnContentListChangedEvent: event=%p -> unsignaled (no add-on content can be installed)\n",
            (void *)out_event);
}

void nso_host_os_wait_system_event(void *event) {
    nso_system_event_entry *e = borrow_system_event(event);
    if (!e) {
        fprintf(stderr, "[hle] nn::os::WaitSystemEvent: null event\n");
        return;
    }
    /* Log before blocking: an event that is never raised parks this thread
     * forever, and the trace must show that the call was reached. */
    fprintf(stderr, "[hle] nn::os::WaitSystemEvent: event=%p waiting\n", event);
    pthread_mutex_lock(&s_system_event_mutex);
    while (!e->signaled && e->initialized) {
        pthread_cond_wait(&s_system_event_cond, &s_system_event_mutex);
    }
    bool satisfied = e->signaled;
    pthread_mutex_unlock(&s_system_event_mutex);

    fprintf(stderr, "[hle] nn::os::WaitSystemEvent: event=%p %s\n",
            event, satisfied ? "satisfied" : "destroyed while waiting");
}

void nso_host_os_signal_system_event(void *event) {
    nso_system_event_entry *e = borrow_system_event(event);
    if (!e) {
        fprintf(stderr, "[hle] nn::os::SignalSystemEvent: null event\n");
        return;
    }
    pthread_mutex_lock(&s_system_event_mutex);
    e->signaled = true;
    pthread_cond_broadcast(&s_system_event_cond);
    pthread_mutex_unlock(&s_system_event_mutex);

    fprintf(stderr, "[hle] nn::os::SignalSystemEvent: event=%p\n", event);
}

void nso_host_os_destroy_system_event(void *event) {
    if (!event) {
        fprintf(stderr, "[hle] nn::os::DestroySystemEvent: null event\n");
        return;
    }
    pthread_mutex_lock(&s_system_event_mutex);
    nso_system_event_entry *e = find_system_event_locked(event);
    if (e) {
        e->initialized = false;
        e->signaled = false;
        pthread_cond_broadcast(&s_system_event_cond);
    }
    pthread_mutex_unlock(&s_system_event_mutex);

    fprintf(stderr, "[hle] nn::os::DestroySystemEvent: event=%p\n", event);
}

static bool s_nifm_initialized = false;
int32_t nso_host_nifm_initialize(void) {
    s_nifm_initialized = true;
    fprintf(stderr, "[hle] nn::nifm::Initialize: initialized network interface manager -> returning 0\n");
    return 0;
}

bool nso_host_nifm_is_initialized(void) {
    return s_nifm_initialized;
}

static int32_t s_ldn_state = 0;
int32_t nso_host_ldn_get_state(void) {
    /* nn::ldn::GetState() (main NSO import index 88).
     *
     * The value is settled from the four places the guest reads it, and all
     * of them agree that 0 means "no Nintendo local wireless session":
     *
     *   0x15bd4  the refresh helper returns 0 straight away when the state
     *            is non-zero and only takes its mutex and continues with the
     *            internet path when the state is 0.
     *   0x51dca8 and 0x51ecb8  the guest turns the state into a three-valued
     *            network classification: internet available (3), a local
     *            wireless session (2, state non-zero) or neither (1, state 0).
     *   0x942edc the state is checked before the refresh helper runs.
     *   0x943198 the local wireless IPv4 address is only requested for the
     *            states 3 and 5; state 0 takes the internet path instead.
     *
     * The RG477M is a standalone handheld with no Nintendo local wireless
     * radio and no local wireless session, so 0 is the state it has to
     * report. No enumerator name is claimed for it, because the tree carries
     * no authoritative nn::ldn state list. */
    fprintf(stderr,"[hle] nn::ldn::GetState: state=%d recorded\n",s_ldn_state);
    return s_ldn_state;
}

static bool s_socket_initialized = false;
static void *s_socket_work_buffer = NULL;
static size_t s_socket_work_buffer_size = 0;
static size_t s_socket_allocator_size = 0;
static int32_t s_socket_concurrency = 0;

int32_t nso_host_socket_initialize(void *work_buffer, size_t work_buffer_size, size_t allocator_size, int32_t concurrency) {
    s_socket_initialized = true;
    s_socket_work_buffer = work_buffer;
    s_socket_work_buffer_size = work_buffer_size;
    s_socket_allocator_size = allocator_size;
    s_socket_concurrency = concurrency;
    fprintf(stderr, "[hle] nn::socket::Initialize: buffer=%p size=%zu (0x%zx) alloc_size=%zu (0x%zx) concurrency=%d -> returning 0\n",
            work_buffer, work_buffer_size, work_buffer_size, allocator_size, allocator_size, concurrency);
    return 0;
}

bool nso_host_socket_is_initialized(void) {
    return s_socket_initialized;
}

/* ==========================================================================
 * nn::socket event family: EventFd / EventFdWrite / EventFdRead / Close /
 * GetLastError / Poll.
 *
 * Retail ABI evidence (Nintendo SDK NSO, build_id
 * 0023a1b06130cd795f7bc4613772af0af71c8cfd000000000000000000000000, sha256
 * 9dfd1cc6d3209d857dcafb191736265ac812401ddcc7b49dabf3bc0b114655a9; read
 * offline with scripts/runtime/inspect_nso.py --slice):
 *   nn::socket::EventFd(unsigned long, EventFdFlags)   @ 0x159b70 (4-byte
 *       thunk) -> nn::socket::detail::EventFd @ 0x165df0, which returns the
 *       descriptor in w0, or -1 with errno set on failure.
 *   nn::socket::EventFdWrite(int, unsigned long)       @ 0x159be0:
 *       "sub sp,#0x20 ; str x1,[sp,#8] ; mov w2,#8 ; bl write ; cmp w0,#8 ;
 *       csetm w0,ne" -> exactly 8 bytes are written and the result is 0 on
 *       success, -1 otherwise.
 *   nn::socket::EventFdRead(int, unsigned long*)        @ 0x159b80:
 *       "cbz x1 -> -1 ; str xzr,[x29,#0x18] ; mov w2,#8 ; bl read ;
 *       cmp w0,#8 ; str x8,[x19]" -> exactly 8 bytes are read into the output
 *       word and the result is 0 on success, -1 otherwise.
 *   nn::socket::Close(int) and nn::socket::GetLastError() are the matching
 *       release and error-report entry points.
 *
 * Guest usage (main NSO build_id 736837afd390442ed7c74a7de730e71b, resolved
 * with scripts/runtime/resolve_import_callers.py):
 *   0x109125c  EventFd(0, 4)            -> stores the descriptor, returns fd != -1
 *   0x10912c4  EventFdWrite(fd, 1)      -> post a single signal
 *   0x10912f0  EventFdRead(fd, &local)  -> drain; the return value is discarded
 *   0x1091290  nn::socket::Close(fd)    -> release
 *   0x10912ec  nn::socket::GetLastError() on the write error path
 * The guest polls the descriptor before reading it, so the descriptor only has
 * to be pollable, not blocking.
 *
 * Host mapping and documented coverage:
 *   - The descriptor is a real host descriptor, so nn::socket::Close closes it
 *     and nn::socket::Poll polls it. nn::socket::PollFd is {int fd; short
 *     events; short revents;}, which is layout-identical to POSIX pollfd
 *     (the guest writes fd+events as one 8-byte store and reads revents as a
 *     short at +6), so the guest array is polled in place.
 *   - Linux and Android use the kernel eventfd(2) with EFD_SEMAPHORE and
 *     EFD_NONBLOCK. Other hosts use an equivalent pipe-backed event object
 *     with the same observable behaviour, so the host contract and the device
 *     contract assert identical facts.
 *   - Semaphore (auto-reset) semantics are used because the guest drains with
 *     a single read per posted signal: a plain counter eventfd would consume
 *     two posted signals in one wait and could lose a wake-up.
 *   - The read never blocks. Retail blocks until a signal arrives; the guest
 *     polls first and discards the read result, so an empty read reports
 *     EAGAIN instead of parking the runtime thread.
 *   - nn::socket::EventFdFlags is logged verbatim but not reinterpreted: the
 *     retail mapping lives in the socket-framework service (its no-service
 *     fallback at 0x165d10 fails with EOPNOTSUPP), so no local artifact
 *     states the bit meanings. The guest passes the single value 4.
 * ========================================================================== */

#if defined(__linux__) || defined(__ANDROID__)
#include <sys/eventfd.h>
#define NN_SOCKET_EVENT_BACKEND_EVENTFD 1
#else
#define NN_SOCKET_EVENT_BACKEND_PIPE 1
#endif
#include <poll.h>

enum { NN_SOCKET_EVENT_SLOTS = 64 };
struct nn_socket_event_slot {
    int event_fd;      /* descriptor handed to the guest */
    int aux_fd;        /* pipe write end of the portable backend, else -1 */
    uint64_t counter;  /* pending posts of the portable backend */
    bool token;        /* portable backend: a readability token is queued */
    bool in_use;
};
static struct nn_socket_event_slot s_socket_events[NN_SOCKET_EVENT_SLOTS];
static pthread_mutex_t s_socket_events_mutex = PTHREAD_MUTEX_INITIALIZER;
static _Thread_local int s_socket_last_error = 0;

static void socket_set_error(int error) {
    s_socket_last_error = error;
    errno = error;
}
static struct nn_socket_event_slot *socket_event_find(int fd) {
    for (size_t i = 0; i < NN_SOCKET_EVENT_SLOTS; i++)
        if (s_socket_events[i].in_use && s_socket_events[i].event_fd == fd) return &s_socket_events[i];
    return NULL;
}
static struct nn_socket_event_slot *socket_event_reserve(int event_fd, int aux_fd) {
    struct nn_socket_event_slot *slot = NULL;
    for (size_t i = 0; i < NN_SOCKET_EVENT_SLOTS; i++) {
        if (!s_socket_events[i].in_use) { slot = &s_socket_events[i]; break; }
    }
    if (!slot) return NULL;
    *slot = (struct nn_socket_event_slot){.event_fd = event_fd, .aux_fd = aux_fd, .in_use = true};
    return slot;
}
static void socket_event_release(struct nn_socket_event_slot *slot) {
    if (slot->aux_fd >= 0) close(slot->aux_fd);
    slot->in_use = false;
    slot->event_fd = -1;
    slot->aux_fd = -1;
    slot->counter = 0;
    slot->token = false;
}

int32_t nso_host_socket_event_fd(uint64_t initval, int32_t flags) {
    int event_fd = -1;
    int aux_fd = -1;
    uint64_t counter = 0;
    bool token = false;
#if defined(NN_SOCKET_EVENT_BACKEND_EVENTFD)
    event_fd = eventfd((unsigned int)initval, EFD_SEMAPHORE | EFD_NONBLOCK);
    if (event_fd < 0) {
        socket_set_error(errno);
        fprintf(stderr, "[hle] nn::socket::EventFd: eventfd(initval=%llu) failed errno=%d (%s) -> returning -1\n",
                (unsigned long long)initval, errno, strerror(errno));
        return -1;
    }
#else
    int fds[2];
    if (pipe(fds) != 0) {
        socket_set_error(errno);
        fprintf(stderr, "[hle] nn::socket::EventFd: pipe failed errno=%d (%s) -> returning -1\n", errno, strerror(errno));
        return -1;
    }
    int flags_now = fcntl(fds[0], F_GETFL, 0);
    if (flags_now >= 0) fcntl(fds[0], F_SETFL, flags_now | O_NONBLOCK);
    event_fd = fds[0];
    aux_fd = fds[1];
    counter = initval;
    if (counter) {
        uint64_t mark = 1;
        if (write(aux_fd, &mark, sizeof(mark)) == (ssize_t)sizeof(mark)) token = true;
    }
#endif
    pthread_mutex_lock(&s_socket_events_mutex);
    struct nn_socket_event_slot *slot = socket_event_reserve(event_fd, aux_fd);
    if (slot) {
        slot->counter = counter;
        slot->token = token;
    }
    pthread_mutex_unlock(&s_socket_events_mutex);
    if (!slot) {
        if (aux_fd >= 0) close(aux_fd);
        close(event_fd);
        socket_set_error(EMFILE);
        fprintf(stderr, "[hle] nn::socket::EventFd: no free event slot -> returning -1\n");
        return -1;
    }
    fprintf(stderr, "[hle] nn::socket::EventFd: initval=%llu flags=0x%x -> descriptor=%d (backend=%s, auto-reset, non-blocking)\n",
            (unsigned long long)initval, (unsigned)flags, event_fd,
#if defined(NN_SOCKET_EVENT_BACKEND_EVENTFD)
            "eventfd"
#else
            "pipe"
#endif
            );
    return event_fd;
}

/* Portable backend: queue one readability token while posts are pending so a
 * poll on the descriptor reports the same readiness as the kernel eventfd. The
 * token travels on the pipe write end; the guest sees the read end. */
#if defined(NN_SOCKET_EVENT_BACKEND_PIPE)
static bool socket_event_post(struct nn_socket_event_slot *slot, uint64_t value) {
    slot->counter += value;
    if (slot->token) return true;
    uint64_t mark = 1;
    if (write(slot->aux_fd, &mark, sizeof(mark)) != (ssize_t)sizeof(mark)) return false;
    slot->token = true;
    return true;
}
#endif

int32_t nso_host_socket_event_fd_write(int32_t fd, uint64_t value) {
    if (fd < 0) {
        socket_set_error(EBADF);
        fprintf(stderr, "[hle] nn::socket::EventFdWrite: invalid descriptor %d -> returning -1\n", fd);
        return -1;
    }
    pthread_mutex_lock(&s_socket_events_mutex);
    struct nn_socket_event_slot *slot = socket_event_find(fd);
    if (slot) {
#if defined(NN_SOCKET_EVENT_BACKEND_EVENTFD)
        uint64_t posted = value;
        ssize_t written = write(fd, &posted, sizeof(posted));
        bool ok = written == (ssize_t)sizeof(posted);
        int error = ok ? 0 : errno;
#else
        bool ok = socket_event_post(slot, value);
        int error = ok ? 0 : errno;
#endif
        pthread_mutex_unlock(&s_socket_events_mutex);
        if (!ok) {
            socket_set_error(error);
            fprintf(stderr, "[hle] nn::socket::EventFdWrite: descriptor=%d value=%llu failed errno=%d (%s) -> returning -1\n",
                    fd, (unsigned long long)value, error, strerror(error));
            return -1;
        }
        fprintf(stderr, "[hle] nn::socket::EventFdWrite: descriptor=%d value=%llu -> posted\n",
                fd, (unsigned long long)value);
        return 0;
    }
    pthread_mutex_unlock(&s_socket_events_mutex);
    /* Not one of ours: fall back to the documented 8-byte write so a foreign
     * descriptor still behaves like the retail entry point. */
    uint64_t posted = value;
    ssize_t written = write(fd, &posted, sizeof(posted));
    if (written != (ssize_t)sizeof(posted)) {
        int error = written < 0 ? errno : EIO;
        socket_set_error(error);
        fprintf(stderr, "[hle] nn::socket::EventFdWrite: descriptor=%d value=%llu foreign write failed errno=%d (%s) -> returning -1\n",
                fd, (unsigned long long)value, error, strerror(error));
        return -1;
    }
    fprintf(stderr, "[hle] nn::socket::EventFdWrite: descriptor=%d value=%llu -> posted (foreign descriptor)\n",
            fd, (unsigned long long)value);
    return 0;
}

int32_t nso_host_socket_event_fd_read(int32_t fd, uint64_t *out_value) {
    if (!out_value) {
        socket_set_error(EFAULT);
        fprintf(stderr, "[hle] nn::socket::EventFdRead: null output pointer -> returning -1\n");
        return -1;
    }
    if (fd < 0) {
        socket_set_error(EBADF);
        fprintf(stderr, "[hle] nn::socket::EventFdRead: invalid descriptor %d -> returning -1\n", fd);
        return -1;
    }
    pthread_mutex_lock(&s_socket_events_mutex);
    struct nn_socket_event_slot *slot = socket_event_find(fd);
    if (slot) {
#if defined(NN_SOCKET_EVENT_BACKEND_EVENTFD)
        uint64_t value = 0;
        ssize_t got = read(fd, &value, sizeof(value));
        bool ok = got == (ssize_t)sizeof(value);
        int error = ok ? 0 : (got < 0 ? errno : EIO);
#else
        bool ok = slot->counter > 0;
        int error = ok ? 0 : EAGAIN;
        uint64_t value = 1;
        if (ok) {
            slot->counter -= 1;
            if (!slot->counter) {
                uint64_t mark = 0;
                ssize_t got = read(slot->event_fd, &mark, sizeof(mark));
                if (got == (ssize_t)sizeof(mark)) slot->token = false;
            }
        }
#endif
        pthread_mutex_unlock(&s_socket_events_mutex);
        if (!ok) {
            socket_set_error(error);
            fprintf(stderr, "[hle] nn::socket::EventFdRead: descriptor=%d no pending signal errno=%d (%s) -> returning -1\n",
                    fd, error, strerror(error));
            return -1;
        }
        *out_value = value;
        fprintf(stderr, "[hle] nn::socket::EventFdRead: descriptor=%d -> value=%llu\n",
                fd, (unsigned long long)value);
        return 0;
    }
    pthread_mutex_unlock(&s_socket_events_mutex);
    uint64_t value = 0;
    ssize_t got = read(fd, &value, sizeof(value));
    if (got != (ssize_t)sizeof(value)) {
        int error = got < 0 ? errno : EIO;
        socket_set_error(error);
        fprintf(stderr, "[hle] nn::socket::EventFdRead: descriptor=%d foreign read failed errno=%d (%s) -> returning -1\n",
                fd, error, strerror(error));
        return -1;
    }
    *out_value = value;
    fprintf(stderr, "[hle] nn::socket::EventFdRead: descriptor=%d -> value=%llu (foreign descriptor)\n",
            fd, (unsigned long long)value);
    return 0;
}

int32_t nso_host_socket_close(int32_t fd) {
    if (fd < 0) {
        socket_set_error(EBADF);
        fprintf(stderr, "[hle] nn::socket::Close: invalid descriptor %d -> returning -1\n", fd);
        return -1;
    }
    /* The guest's nn::socket descriptors are real host descriptors, so a close
     * of 0/1/2 would take the runtime's own standard streams with it. The
     * retail socket namespace never contains them, so refuse instead. */
    if (fd < 3) {
        socket_set_error(EBADF);
        fprintf(stderr, "[hle] nn::socket::Close: descriptor %d is outside the socket namespace -> returning -1\n", fd);
        return -1;
    }
    pthread_mutex_lock(&s_socket_events_mutex);
    struct nn_socket_event_slot *slot = socket_event_find(fd);
    if (slot) socket_event_release(slot);
    pthread_mutex_unlock(&s_socket_events_mutex);
    if (close(fd) != 0) {
        int error = errno;
        socket_set_error(error);
        fprintf(stderr, "[hle] nn::socket::Close: descriptor=%d close failed errno=%d (%s) -> returning -1\n",
                fd, error, strerror(error));
        return -1;
    }
    fprintf(stderr, "[hle] nn::socket::Close: descriptor=%d -> released\n", fd);
    return 0;
}

int32_t nso_host_socket_get_last_error(void) {
    int error = s_socket_last_error;
    fprintf(stderr, "[hle] nn::socket::GetLastError: -> %d (%s)\n", error, error ? strerror(error) : "no error");
    return error;
}

int32_t nso_host_socket_poll(void *fds, uint64_t count, int32_t timeout_ms) {
    if (!fds && count) {
        socket_set_error(EFAULT);
        fprintf(stderr, "[hle] nn::socket::Poll: null descriptor array -> returning -1\n");
        return -1;
    }
    if (count > 65536) {
        socket_set_error(EINVAL);
        fprintf(stderr, "[hle] nn::socket::Poll: descriptor count %llu out of range -> returning -1\n",
                (unsigned long long)count);
        return -1;
    }
    /* Logged before the call: a wait that never completes is then attributable
     * to this entry point instead of looking like a silent stall. */
    fprintf(stderr, "[hle] nn::socket::Poll: waiting count=%llu timeout_ms=%d\n",
            (unsigned long long)count, timeout_ms);
    int ready = poll((struct pollfd *)fds, (nfds_t)count, timeout_ms);
    if (ready < 0) {
        int error = errno;
        socket_set_error(error);
        fprintf(stderr, "[hle] nn::socket::Poll: count=%llu timeout_ms=%d failed errno=%d (%s) -> returning -1\n",
                (unsigned long long)count, timeout_ms, error, strerror(error));
        return -1;
    }
    fprintf(stderr, "[hle] nn::socket::Poll: count=%llu timeout_ms=%d -> ready=%d\n",
            (unsigned long long)count, timeout_ms, ready);
    return ready;
}

/* nnsocketPoll is the C entry point of the same POSIX poll. The retail SDK
 * signature is nnsocketPoll(struct pollfd *, int nfds, int timeout), which
 * nn::socket::detail::Poll @ 0x164c80 forwards to the socket framework. */
int32_t nso_host_socket_poll_c(void *fds, int32_t count, int32_t timeout_ms) {
    if (count < 0) {
        socket_set_error(EINVAL);
        fprintf(stderr, "[hle] nnsocketPoll: negative descriptor count %d -> returning -1\n", count);
        return -1;
    }
    return nso_host_socket_poll(fds, (uint64_t)count, timeout_ms);
}

static bool s_account_initialized = false;

int32_t nso_host_account_initialize(void) {
    s_account_initialized = true;
    fprintf(stderr, "[hle] nn::account::Initialize: initialized account system -> returning 0\n");
    return 0;
}

bool nso_host_account_is_initialized(void) {
    return s_account_initialized;
}

static bool s_oe_initialized = false;

int32_t nso_host_oe_initialize(void) {
    s_oe_initialized = true;
    fprintf(stderr, "[hle] nn::oe::Initialize: initialized operation environment -> returning 0\n");
    return 0;
}

bool nso_host_oe_is_initialized(void) {
    return s_oe_initialized;
}

nn_settings_language_code nso_host_oe_get_desired_language(void) {
    nn_settings_language_code code = { .code = "en-US" };
    fprintf(stderr, "[hle] nn::oe::GetDesiredLanguage: returning 'en-US'\n");
    return code;
}

static nn_oe_cpu_boost_mode s_oe_cpu_boost_mode = 0;

void nso_host_oe_set_cpu_boost_mode(nn_oe_cpu_boost_mode mode) {
    s_oe_cpu_boost_mode = mode;
    /* On the Switch this selects the title's CPU boost profile. On RG477M the
     * frequency policy belongs to the Android governor (writing cpufreq sysfs
     * needs root), so the request is recorded faithfully and stays visible in
     * the log instead of being dropped silently or faked. */
    fprintf(stderr, "[hle] nn::oe::SetCpuBoostMode: mode=%d recorded (cpu frequency policy stays with the Android governor)\n", (int)mode);
}

nn_oe_cpu_boost_mode nso_host_oe_get_cpu_boost_mode(void) {
    return s_oe_cpu_boost_mode;
}

static bool s_hid_keyboard_initialized = false;
static bool s_hid_mouse_initialized = false;

void nso_host_hid_initialize_keyboard(void) {
    s_hid_keyboard_initialized = true;
    fprintf(stderr, "[hle] nn::hid::InitializeKeyboard: keyboard subsystem initialised\n");
}

bool nso_host_hid_keyboard_initialized(void) {
    return s_hid_keyboard_initialized;
}

void nso_host_hid_initialize_mouse(void) {
    s_hid_mouse_initialized = true;
    fprintf(stderr, "[hle] nn::hid::InitializeMouse: mouse subsystem initialised\n");
}

bool nso_host_hid_mouse_initialized(void) {
    return s_hid_mouse_initialized;
}

static uint32_t s_oe_focus_state = 0;
static bool s_oe_performance_mode_notification = false;
static bool s_oe_operation_mode_notification = false;
static int32_t s_oe_focus_handling_mode = 0;
static bool s_oe_resume_notification = false;

uint32_t nso_host_oe_get_current_focus_state(void) {
    /* Stable and never 3: the guest treats 3 as "something else holds the
     * foreground" and reacts to it, and a foreground Android app has neither
     * an overlay nor a background transition to report. */
    return s_oe_focus_state;
}

void nso_host_oe_set_performance_mode_changed_notification_enabled(bool enabled) {
    s_oe_performance_mode_notification=enabled;
    fprintf(stderr, "[hle] nn::oe::SetPerformanceModeChangedNotificationEnabled: enabled=%d recorded\n",enabled?1:0);
}

void nso_host_oe_set_operation_mode_changed_notification_enabled(bool enabled) {
    s_oe_operation_mode_notification=enabled;
    fprintf(stderr, "[hle] nn::oe::SetOperationModeChangedNotificationEnabled: enabled=%d recorded\n",enabled?1:0);
}

void nso_host_oe_set_focus_handling_mode(int32_t mode) {
    s_oe_focus_handling_mode=mode;
    fprintf(stderr, "[hle] nn::oe::SetFocusHandlingMode: mode=%d recorded\n",(int)mode);
}

void nso_host_oe_set_resume_notification_enabled(bool enabled) {
    s_oe_resume_notification=enabled;
    fprintf(stderr, "[hle] nn::oe::SetResumeNotificationEnabled: enabled=%d recorded\n",enabled?1:0);
}

bool nso_host_oe_performance_mode_changed_notification_enabled(void) {
    return s_oe_performance_mode_notification;
}

bool nso_host_oe_operation_mode_changed_notification_enabled(void) {
    return s_oe_operation_mode_notification;
}

int32_t nso_host_oe_focus_handling_mode(void) {
    return s_oe_focus_handling_mode;
}

bool nso_host_oe_resume_notification_enabled(void) {
    return s_oe_resume_notification;
}

/* nn::oe performance configuration (main NSO import indexes 239 and 240).
 *
 * nn::oe::SetPerformanceConfiguration(PerformanceMode mode, int clock) is
 * bound as a record-only stub: the guest asks for a performance mode and a
 * clock code, the stub records both, and the real CPU and GPU frequencies
 * stay with the Android governor. Changing them from here is not possible
 * anyway, because the cpufreq sysfs nodes need root, and the already bound
 * nn::oe::SetCpuBoostMode (import 75) follows the same rule.
 *
 * The guest calls SetPerformanceConfiguration from four sites (0x520518,
 * 0x520524, 0x5205bc and 0x5205c8). Each pair of sites loads w0 and w1
 * immediately before the call and never consumes the result, so the ABI is
 * two 32-bit arguments and no return value. The values the guest passes are
 * mode 0 with clock 0x20003 or 0x20004, and then mode 1 with clock 0x10001.
 *
 * nn::oe::GetPerformanceMode() has a single call site (0x5203c4) and its
 * result is compared against 1, so it returns the performance mode that was
 * requested last. Reporting the requested state is what the reference
 * platform does: the guest asks for a mode and then reads it back. The
 * initial value is 0, the mode that has been in effect before the guest
 * asked for anything. */
static int32_t s_oe_performance_mode = 0;
static int32_t s_oe_performance_clock = 0;

void nso_host_oe_set_performance_configuration(int32_t mode,int32_t clock) {
    s_oe_performance_mode=mode;
    s_oe_performance_clock=clock;
    fprintf(stderr,"[hle] nn::oe::SetPerformanceConfiguration: mode=%d clock=0x%x recorded (the real CPU and GPU frequencies stay with the Android governor)\n",(int)mode,(unsigned)clock);
}

int32_t nso_host_oe_get_performance_mode(void) {
    return s_oe_performance_mode;
}

int32_t nso_host_oe_performance_clock(void) {
    return s_oe_performance_clock;
}

static uint32_t s_oe_operation_mode = 1;

uint32_t nso_host_oe_get_operation_mode(void) {
    /* The guest calls this from two unconditional sites (0x51dc84 and
     * 0x51ee40) and uses the result in exactly one way: it compares the
     * value against 1 and caches the outcome as a byte flag at 0x7c90614
     * (cset / strb at 0x51dc90-0x51dc94 and 0x51ee50-0x51ee58). That flag is
     * the game's "operation mode is 1" state.
     *
     * The flag is read at four sites (0x51e98c, 0x51f06c, 0x523e98 and
     * 0x525104) and each one picks the screen-size scale factor the same
     * way: fcsel selects 0x3cf5c290 (0.026873588) when the flag is set and
     * the default 0.0 when it is not. The factor multiplies the runtime
     * screen width and height, so the flag-clear branch scales the screen
     * size to zero and leaves the scaled size fields of the screen-info
     * structure (offsets 0x40, 0x48, 0x60, 0x68, 0x70 and 0x78) degenerate.
     *
     * A released game cannot use that branch as its normal state, so the
     * working state of the game is the one where the flag is set, which
     * means the value returned here has to be 1. The enumerator name behind
     * the value is not available in-tree and is not claimed; only the value
     * the guest requires is implemented. */
    fprintf(stderr,"[hle] nn::oe::GetOperationMode: mode=%u recorded\n",s_oe_operation_mode);
    return s_oe_operation_mode;
}

/* ========================================================================= */
/* nn::os::EventType HLE Implementation                                      */
/* ========================================================================= */

typedef enum {
    NN_OS_EVENT_CLEAR_MODE_MANUAL = 0,
    NN_OS_EVENT_CLEAR_MODE_AUTO   = 1,
} nn_os_event_clear_mode;

struct nso_event_entry {
    void *object;                 /* Guest EventType pointer */
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool signaled;
    uint8_t clear_mode;          /* 0 = manual, 1 = auto */
    bool initialized;
    uint32_t waiters;
    uint32_t users;
    struct nso_event_entry *next;
};

static pthread_mutex_t s_event_registry_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct nso_event_entry *s_event_registry = NULL;

static struct nso_event_entry *find_event_locked(void *object) {
    struct nso_event_entry *e = s_event_registry;
    while (e && e->object != object) e = e->next;
    return e;
}

static struct nso_event_entry *borrow_event(void *object) {
    if (!object || ((uintptr_t)object & 7)) {
        fprintf(stderr, "[hle] nn::os::Event: unaligned or null object %p\n", object);
        return NULL;
    }
    pthread_mutex_lock(&s_event_registry_mutex);
    struct nso_event_entry *e = find_event_locked(object);
    if (!e) {
        /* Auto-register statically initialized or dynamically allocated event */
        uint8_t *b = (uint8_t *)object;
        bool signaled = b[0x10] != 0;
        uint8_t clear_mode = b[0x12];
        e = (struct nso_event_entry *)calloc(1, sizeof(*e));
        if (!e) {
            pthread_mutex_unlock(&s_event_registry_mutex);
            fprintf(stderr, "[hle] nn::os::Event: out of memory allocating event entry\n");
            return NULL;
        }
        pthread_mutex_init(&e->mutex, NULL);
        pthread_cond_init(&e->cond, NULL);
        e->object = object;
        e->signaled = signaled;
        e->clear_mode = clear_mode;
        e->initialized = true;
        e->next = s_event_registry;
        s_event_registry = e;
    }
    e->users++;
    pthread_mutex_unlock(&s_event_registry_mutex);
    return e;
}

static void release_event(struct nso_event_entry *e) {
    if (!e) return;
    pthread_mutex_lock(&s_event_registry_mutex);
    if (e->users > 0) e->users--;
    pthread_mutex_unlock(&s_event_registry_mutex);
}

void nso_host_os_initialize_event(void *event, bool initially_signaled, int32_t clear_mode) {
    if (!event) {
        fprintf(stderr, "[hle] nn::os::InitializeEvent: null event pointer\n");
        return;
    }
    if ((uintptr_t)event & 7) {
        fprintf(stderr, "[hle] nn::os::InitializeEvent: unaligned event pointer %p\n", event);
        return;
    }

    pthread_mutex_lock(&s_event_registry_mutex);
    struct nso_event_entry *e = find_event_locked(event);
    if (!e) {
        e = (struct nso_event_entry *)calloc(1, sizeof(*e));
        if (!e) {
            pthread_mutex_unlock(&s_event_registry_mutex);
            fprintf(stderr, "[hle] nn::os::InitializeEvent: out of memory for event %p\n", event);
            return;
        }
        pthread_mutex_init(&e->mutex, NULL);
        pthread_cond_init(&e->cond, NULL);
        e->object = event;
        e->next = s_event_registry;
        s_event_registry = e;
    } else {
        pthread_mutex_lock(&e->mutex);
        pthread_cond_broadcast(&e->cond);
        pthread_mutex_unlock(&e->mutex);
    }

    pthread_mutex_lock(&e->mutex);
    e->signaled = initially_signaled != 0;
    e->clear_mode = (uint8_t)clear_mode;
    e->initialized = true;
    e->waiters = 0;
    pthread_mutex_unlock(&e->mutex);

    /* Mirror native Horizon/SDK EventType layout into guest memory */
    uint8_t *bytes = (uint8_t *)event;
    *(uint64_t *)(bytes + 0x00) = (uintptr_t)event;
    *(uint64_t *)(bytes + 0x08) = (uintptr_t)event;
    bytes[0x10] = initially_signaled ? 1 : 0;
    bytes[0x11] = initially_signaled ? 1 : 0;
    bytes[0x12] = (uint8_t)clear_mode;
    bytes[0x13] = 1; /* initialized state */
    *(uint64_t *)(bytes + 0x14) = 0;
    *(uint32_t *)(bytes + 0x1c) = 0;
    *(uint32_t *)(bytes + 0x20) = 0;

    pthread_mutex_unlock(&s_event_registry_mutex);

    fprintf(stderr, "[hle] nn::os::InitializeEvent: event=%p initially_signaled=%d clear_mode=%d (%s)\n",
            event, initially_signaled ? 1 : 0, clear_mode,
            clear_mode == NN_OS_EVENT_CLEAR_MODE_AUTO ? "AutoClear" : "ManualClear");
}

void nso_host_os_finalize_event(void *event) {
    if (!event) {
        fprintf(stderr, "[hle] nn::os::FinalizeEvent: null event pointer\n");
        return;
    }
    pthread_mutex_lock(&s_event_registry_mutex);
    struct nso_event_entry **slot = &s_event_registry;
    while (*slot && (*slot)->object != event) {
        slot = &(*slot)->next;
    }
    if (*slot) {
        struct nso_event_entry *e = *slot;
        *slot = e->next;
        pthread_mutex_lock(&e->mutex);
        e->initialized = false;
        pthread_cond_broadcast(&e->cond);
        pthread_mutex_unlock(&e->mutex);
        pthread_cond_destroy(&e->cond);
        pthread_mutex_destroy(&e->mutex);
        free(e);
    }
    /* Clear guest initialized state */
    ((uint8_t *)event)[0x13] = 0;
    pthread_mutex_unlock(&s_event_registry_mutex);

    fprintf(stderr, "[hle] nn::os::FinalizeEvent: event=%p\n", event);
}

void nso_host_os_signal_event(void *event) {
    struct nso_event_entry *e = borrow_event(event);
    if (!e) return;

    pthread_mutex_lock(&e->mutex);
    if (!e->signaled) {
        e->signaled = true;
        ((uint8_t *)event)[0x10] = 1;
        if (e->clear_mode == NN_OS_EVENT_CLEAR_MODE_AUTO) {
            pthread_cond_signal(&e->cond);
        } else {
            pthread_cond_broadcast(&e->cond);
        }
    }
    pthread_mutex_unlock(&e->mutex);
    release_event(e);

    fprintf(stderr, "[hle] nn::os::SignalEvent: event=%p\n", event);
}

void nso_host_os_clear_event(void *event) {
    struct nso_event_entry *e = borrow_event(event);
    if (!e) return;

    pthread_mutex_lock(&e->mutex);
    e->signaled = false;
    ((uint8_t *)event)[0x10] = 0;
    pthread_mutex_unlock(&e->mutex);
    release_event(e);

    fprintf(stderr, "[hle] nn::os::ClearEvent: event=%p\n", event);
}

void nso_host_os_wait_event(void *event) {
    struct nso_event_entry *e = borrow_event(event);
    if (!e) return;

    pthread_mutex_lock(&e->mutex);
    while (!e->signaled && e->initialized) {
        e->waiters++;
        pthread_cond_wait(&e->cond, &e->mutex);
        e->waiters--;
    }
    if (e->clear_mode == NN_OS_EVENT_CLEAR_MODE_AUTO) {
        e->signaled = false;
        ((uint8_t *)event)[0x10] = 0;
    }
    pthread_mutex_unlock(&e->mutex);
    release_event(e);

    fprintf(stderr, "[hle] nn::os::WaitEvent: event=%p satisfied\n", event);
}

bool nso_host_os_try_wait_event(void *event) {
    struct nso_event_entry *e = borrow_event(event);
    if (!e) return false;

    pthread_mutex_lock(&e->mutex);
    bool signaled = e->signaled;
    if (signaled && e->clear_mode == NN_OS_EVENT_CLEAR_MODE_AUTO) {
        e->signaled = false;
        ((uint8_t *)event)[0x10] = 0;
    }
    pthread_mutex_unlock(&e->mutex);
    release_event(e);

    return signaled;
}

bool nso_host_os_timed_wait_event(void *event, int64_t timeout_ns) {
    if (timeout_ns <= 0) {
        return nso_host_os_try_wait_event(event);
    }
    struct nso_event_entry *e = borrow_event(event);
    if (!e) return false;

    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += timeout_ns / 1000000000LL;
    deadline.tv_nsec += timeout_ns % 1000000000LL;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec += 1;
        deadline.tv_nsec -= 1000000000L;
    }

    pthread_mutex_lock(&e->mutex);
    bool satisfied = false;
    while (!e->signaled && e->initialized) {
        e->waiters++;
        int rc = pthread_cond_timedwait(&e->cond, &e->mutex, &deadline);
        e->waiters--;
        if (rc == ETIMEDOUT) {
            break;
        }
    }
    if (e->signaled) {
        satisfied = true;
        if (e->clear_mode == NN_OS_EVENT_CLEAR_MODE_AUTO) {
            e->signaled = false;
            ((uint8_t *)event)[0x10] = 0;
        }
    }
    pthread_mutex_unlock(&e->mutex);
    release_event(e);

    return satisfied;
}

bool nso_host_os_is_event_signaled(void *event) {
    if (!event) return false;
    pthread_mutex_lock(&s_event_registry_mutex);
    struct nso_event_entry *e = find_event_locked(event);
    bool s = false;
    if (e) {
        pthread_mutex_lock(&e->mutex);
        s = e->signaled;
        pthread_mutex_unlock(&e->mutex);
    } else {
        s = ((uint8_t *)event)[0x10] != 0;
    }
    pthread_mutex_unlock(&s_event_registry_mutex);
    return s;
}

/* ========================================================================= */
/* posix_memalign / memalign HLE Implementation                              */
/* ========================================================================= */

int nso_host_posix_memalign(void **memptr, size_t alignment, size_t size) {
    if (!memptr) {
        fprintf(stderr, "[hle] posix_memalign: null memptr pointer\n");
        return EINVAL;
    }
    if (alignment < sizeof(void *) || (alignment & (alignment - 1)) != 0) {
        fprintf(stderr, "[hle] posix_memalign: invalid alignment %zu (must be power of two >= %zu)\n",
                alignment, sizeof(void *));
        return EINVAL;
    }
    size_t alloc_size = (size == 0) ? sizeof(void *) : size;
    void *ptr = NULL;
    int rc = posix_memalign(&ptr, alignment, alloc_size);
    if (rc != 0) {
        fprintf(stderr, "[hle] posix_memalign: allocation failed alignment=%zu size=%zu rc=%d\n",
                alignment, size, rc);
        return rc;
    }
    memset(ptr, 0, alloc_size);
    *memptr = ptr;
    fprintf(stderr, "[hle] posix_memalign: memptr=%p alignment=%zu (0x%zx) size=%zu (0x%zx) -> *memptr=%p rc=0\n",
            (void *)memptr, alignment, alignment, size, size, ptr);
    return 0;
}

void *nso_host_memalign(size_t alignment, size_t size) {
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        fprintf(stderr, "[hle] memalign: invalid alignment %zu\n", alignment);
        return NULL;
    }
    size_t alloc_align = alignment < sizeof(void *) ? sizeof(void *) : alignment;
    size_t alloc_size = (size == 0) ? sizeof(void *) : size;
    void *ptr = NULL;
    int rc = posix_memalign(&ptr, alloc_align, alloc_size);
    if (rc != 0) {
        fprintf(stderr, "[hle] memalign: allocation failed alignment=%zu size=%zu rc=%d\n",
                alignment, size, rc);
        return NULL;
    }
    memset(ptr, 0, alloc_size);
    fprintf(stderr, "[hle] memalign: alignment=%zu (0x%zx) size=%zu (0x%zx) -> %p\n",
            alignment, alignment, size, size, ptr);
    return ptr;
}

void *nso_host_aligned_alloc(size_t alignment, size_t size) {
    return nso_host_memalign(alignment, size);
}

void *nso_host_valloc(size_t size) {
    return nso_host_memalign(4096, size);
}

void nso_host_free(void *ptr) {
    if (ptr) {
        free(ptr);
    }
}

static int is_readable(const void *ptr, size_t len) {
    if (!ptr || (uintptr_t)ptr < 4096) return 0;
    int pfd[2];
    if (pipe(pfd) != 0) return 0;
    ssize_t ret = write(pfd[1], ptr, len);
    close(pfd[0]);
    close(pfd[1]);
    return ret == (ssize_t)len;
}

uint64_t nso_host_diag_abort_impl(void) {
    uint64_t cur_fp = 0, cur_lr = 0;
    uint64_t cur_x0 = 0, cur_x19 = 0, cur_x20 = 0, cur_x21 = 0, cur_x22 = 0, cur_x8 = 0;
#if defined(__aarch64__)
    __asm__ volatile("mov %0, x29" : "=r"(cur_fp));
    __asm__ volatile("mov %0, x30" : "=r"(cur_lr));
    __asm__ volatile("mov %0, x0"  : "=r"(cur_x0));
    __asm__ volatile("mov %0, x19" : "=r"(cur_x19));
    __asm__ volatile("mov %0, x20" : "=r"(cur_x20));
    __asm__ volatile("mov %0, x21" : "=r"(cur_x21));
    __asm__ volatile("mov %0, x22" : "=r"(cur_x22));
    __asm__ volatile("mov %0, x8"  : "=r"(cur_x8));
#endif
    fprintf(stderr, "[hle] nn::diag::detail::AbortImpl: lr=0x%" PRIx64 " fp=0x%" PRIx64 " x0=0x%" PRIx64 " x19=%p x20=%p x21=0x%" PRIx64 " x22=%p x8=0x%" PRIx64 "\n",
            cur_lr, cur_fp, cur_x0, (void *)cur_x19, (void *)cur_x20, cur_x21, (void *)cur_x22, cur_x8);

    /* Frame chain walk */
    uint64_t fp = cur_fp;
    for (int depth = 0; depth < 16; depth++) {
        if (!fp || (fp & 15) || fp < 0x1000) break;
        if (!is_readable((const void *)(uintptr_t)fp, 16)) break;
        const uint64_t *frame = (const uint64_t *)(uintptr_t)fp;
        uint64_t next_fp = frame[0];
        uint64_t ret_addr = frame[1];
        fprintf(stderr, "[hle]   frame[%d]: fp=0x%" PRIx64 " ret=0x%" PRIx64 "\n", depth, next_fp, ret_addr);
        if (next_fp <= fp) break;
        fp = next_fp;
    }

    /* Decode UTF-16 message string in x22 if valid readable pointer */
    if (cur_x22 && !(cur_x22 & 1) && cur_x22 >= 0x1000 && is_readable((const void *)(uintptr_t)cur_x22, 2)) {
        const uint16_t *u16 = (const uint16_t *)(uintptr_t)cur_x22;
        char utf8[1024];
        size_t out = 0;
        for (size_t in = 0; in < 512 && out + 4 < sizeof(utf8); in++) {
            if (!is_readable(&u16[in], 2) || u16[in] == 0) break;
            uint32_t cp = u16[in];
            if (cp >= 0xd800 && cp <= 0xdbff && in + 1 < 512 && is_readable(&u16[in + 1], 2)) {
                uint32_t low = u16[in + 1];
                if (low >= 0xdc00 && low <= 0xdfff) {
                    cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
                    in++;
                }
            }
            if (cp < 0x80) {
                utf8[out++] = (char)cp;
            } else if (cp < 0x800) {
                utf8[out++] = (char)(0xc0 | (cp >> 6));
                utf8[out++] = (char)(0x80 | (cp & 0x3f));
            } else if (cp < 0x10000) {
                utf8[out++] = (char)(0xe0 | (cp >> 12));
                utf8[out++] = (char)(0x80 | ((cp >> 6) & 0x3f));
                utf8[out++] = (char)(0x80 | (cp & 0x3f));
            } else {
                utf8[out++] = (char)(0xf0 | (cp >> 18));
                utf8[out++] = (char)(0x80 | ((cp >> 12) & 0x3f));
                utf8[out++] = (char)(0x80 | ((cp >> 6) & 0x3f));
                utf8[out++] = (char)(0x80 | (cp & 0x3f));
            }
        }
        utf8[out] = '\0';
        if (out > 0) {
            fprintf(stderr, "[hle] nn::diag::detail::AbortImpl message: \"%s\"\n", utf8);
        }
    }
    return 0;
}

int32_t nso_host_diag_get_backtrace(uintptr_t *out_backtrace, int32_t count) {
    if (!out_backtrace || count <= 0) return 0;
    int32_t depth = 0;
#if defined(__aarch64__)
    uint64_t fp = 0;
    __asm__ volatile("mov %0, x29" : "=r"(fp));
    while (depth < count && fp && !(fp & 15) && fp >= 0x1000) {
        if (!is_readable((const void *)(uintptr_t)fp, 16)) break;
        const uint64_t *frame = (const uint64_t *)(uintptr_t)fp;
        uint64_t next_fp = frame[0];
        uint64_t ret_addr = frame[1];
        if (depth > 0) {
            out_backtrace[depth - 1] = ret_addr;
        }
        depth++;
        if (next_fp <= fp) break;
        fp = next_fp;
    }
    if (depth > 0) depth--;
#endif
    fprintf(stderr, "[hle] nn::diag::GetBacktrace: out=%p count=%d -> captured=%d\n",
            (void *)out_backtrace, count, depth);
    return depth;
}

typedef struct std_mutex_entry {
    void *guest_ptr;
    pthread_mutex_t mutex;
    struct std_mutex_entry *next;
} std_mutex_entry;

static pthread_mutex_t s_std_mutex_lock = PTHREAD_MUTEX_INITIALIZER;
static std_mutex_entry *s_std_mutex_head = NULL;

static pthread_mutex_t *get_std_mutex(void *ptr) {
    if (!ptr) return NULL;
    pthread_mutex_lock(&s_std_mutex_lock);
    std_mutex_entry *e = s_std_mutex_head;
    while (e) {
        if (e->guest_ptr == ptr) {
            pthread_mutex_unlock(&s_std_mutex_lock);
            return &e->mutex;
        }
        e = e->next;
    }
    e = calloc(1, sizeof(*e));
    e->guest_ptr = ptr;
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_NORMAL);
    pthread_mutex_init(&e->mutex, &attr);
    pthread_mutexattr_destroy(&attr);
    e->next = s_std_mutex_head;
    s_std_mutex_head = e;
    pthread_mutex_unlock(&s_std_mutex_lock);
    return &e->mutex;
}

void nso_host_std_mutex_lock(void *m) {
    pthread_mutex_t *pm = get_std_mutex(m);
    if (pm) pthread_mutex_lock(pm);
}

void nso_host_std_mutex_unlock(void *m) {
    pthread_mutex_t *pm = get_std_mutex(m);
    if (pm) pthread_mutex_unlock(pm);
}

typedef struct std_cv_entry {
    void *guest_ptr;
    pthread_cond_t cond;
    struct std_cv_entry *next;
} std_cv_entry;

static pthread_mutex_t s_std_cv_lock = PTHREAD_MUTEX_INITIALIZER;
static std_cv_entry *s_std_cv_head = NULL;

static pthread_cond_t *get_std_cv(void *ptr) {
    if (!ptr) return NULL;
    pthread_mutex_lock(&s_std_cv_lock);
    std_cv_entry *e = s_std_cv_head;
    while (e) {
        if (e->guest_ptr == ptr) {
            pthread_mutex_unlock(&s_std_cv_lock);
            return &e->cond;
        }
        e = e->next;
    }
    e = calloc(1, sizeof(*e));
    e->guest_ptr = ptr;
    pthread_cond_init(&e->cond, NULL);
    e->next = s_std_cv_head;
    s_std_cv_head = e;
    pthread_mutex_unlock(&s_std_cv_lock);
    return &e->cond;
}

void nso_host_std_cv_notify_one(void *cv) {
    pthread_cond_t *pc = get_std_cv(cv);
    if (pc) pthread_cond_signal(pc);
}

void nso_host_std_cv_notify_all(void *cv) {
    pthread_cond_t *pc = get_std_cv(cv);
    if (pc) pthread_cond_broadcast(pc);
}

void nso_host_std_cv_wait(void *cv, void *unique_lock_ptr) {
    pthread_cond_t *pc = get_std_cv(cv);
    if (!pc || !unique_lock_ptr) return;
    void *m = *(void **)unique_lock_ptr;
    pthread_mutex_t *pm = get_std_mutex(m);
    if (pm) pthread_cond_wait(pc, pm);
}

uint64_t nso_host_steady_clock_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

int nso_host_pthread_cond_clockwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                                    clockid_t clock_id, const struct timespec *abstime) {
    (void)clock_id;
    return pthread_cond_timedwait(cond, mutex, abstime);
}

void nso_host_cxa_pure_virtual(void) {
    fprintf(stderr, "[hle] __cxa_pure_virtual called!\n");
    abort();
}

char *nso_host_setlocale(int category, const char *locale) {
    (void)category; (void)locale;
    static char c_locale[] = "C";
    return c_locale;
}

char *nso_host_getenv(const char *name) {
    (void)name;
    return NULL;
}

char *nso_host_nl_langinfo(int item) {
    (void)item;
    static char utf8[] = "UTF-8";
    return utf8;
}

int nso_host_gettimeofday(struct timeval *tv, void *tz) {
    (void)tz;
    if (!tv) {
        return -1;
    }
    int rc = gettimeofday(tv, NULL);
    fprintf(stderr, "[hle] gettimeofday: tv=%p tz=%p -> sec=%ld usec=%ld rc=%d\n",
            (void *)tv, tz, (long)tv->tv_sec, (long)tv->tv_usec, rc);
    return rc;
}

pid_t nso_host_getpid(void) {
    pid_t pid = getpid();
    fprintf(stderr, "[hle] getpid -> %d\n", (int)pid);
    return pid;
}

uint32_t nso_host_get_current_process_handle(void) {
    pid_t pid = getpid();
    uint32_t handle = (uint32_t)pid;
    fprintf(stderr, "[hle] nn::os::GetCurrentProcessHandle -> handle=0x%x (pid=%d)\n", handle, (int)pid);
    return handle;
}

uint32_t nso_host_nn_result_is_failure(uint32_t result) {
    uint32_t is_failure = (result != 0) ? 1 : 0;
    fprintf(stderr, "[hle] nnResultIsFailure: res=0x%x -> %u\n", result, is_failure);
    return is_failure;
}

uint32_t nso_host_nn_result_is_success(uint32_t result) {
    uint32_t is_success = (result == 0) ? 1 : 0;
    fprintf(stderr, "[hle] nnResultIsSuccess: res=0x%x -> %u\n", result, is_success);
    return is_success;
}

uint32_t nso_host_nn_result_get_module(uint32_t result) {
    uint32_t module = result & 0x1ff;
    fprintf(stderr, "[hle] nnResultGetModule: res=0x%x -> %u\n", result, module);
    return module;
}

uint32_t nso_host_nn_result_get_description(uint32_t result) {
    uint32_t description = (result >> 9) & 0x1fff;
    fprintf(stderr, "[hle] nnResultGetDescription: res=0x%x -> %u\n", result, description);
    return description;
}

uint32_t nso_host_nn_result_success(void) {
    fprintf(stderr, "[hle] nnResultSuccess -> 0\n");
    return 0;
}

/* ========================================================================= */
/* nnsslInitialize / nnsslFinalize HLE Implementation                         */
/* Nintendo SDK SSL service lifecycle stubs.                                  */
/* nnsslInitialize: no parameters, returns nn::Result (0 = success).          */
/* nnsslFinalize:   no parameters, returns nn::Result (0 = success).          */
/* We do not perform real TLS handshakes or certificate validation.           */
/*                                                                          */
/* ABI evidence (retail Nintendo SDK NSO, build_id                            */
/* 0023a1b06130cd795f7bc4613772af0af71c8cfd000000000000000000000000,      */
/* sha256 9dfd1cc6d3209d857dcafb191736265ac812401ddcc7b49dabf3bc0b114655a9): */
/*   nnsslInitialize  @ sdk module_offset 0x4335e0 (size 24)                  */
/*     stp x29,x30,[sp,#-0x10]! ; mov x29,sp ; bl <internal> ;                */
/*     and w0,w0,#0x3fffff ; ldp x29,x30,[sp],#0x10 ; ret                     */
/*   nnsslFinalize    @ sdk module_offset 0x433620 (size 24)                  */
/*     stp x29,x30,[sp,#-0x10]! ; mov x29,sp ; bl <internal> ;                */
/*     and w0,w0,#0x3fffff ; ldp x29,x30,[sp],#0x10 ; ret                     */
/* No argument is staged before the call, so both take zero parameters, and   */
/* both return a masked nn::Result in w0 (bits 0..21 = module|description;   */
/* 0 = success). The import gateway (native_boundary.S) forwards w0 to the    */
/* guest unchanged, so nnsslFinalize must return 0 rather than void.          */
/* ========================================================================= */

static bool s_ssl_initialized = false;

int32_t nso_host_ssl_initialize(void) {
    s_ssl_initialized = true;
    fprintf(stderr, "[hle] nnsslInitialize: initialized Nintendo SSL service (stub) -> returning 0\n");
    return 0;
}

int32_t nso_host_ssl_finalize(void) {
    s_ssl_initialized = false;
    fprintf(stderr, "[hle] nnsslFinalize: finalized Nintendo SSL service (stub) -> returning 0\n");
    return 0;
}

bool nso_host_ssl_is_initialized(void) {
    return s_ssl_initialized;
}

/* ========================================================================= */
/* stdio HLE: stream objects, print entry points and stream writes          */
/* ========================================================================= */
/* The guest imports stdout/stdin/stderr as STT_OBJECT data symbols, not as  */
/* callables. The loader binds such an import to the *address of the object* */
/* (R_GLOB_DAT, zero addend) and the guest then loads the object value:     */
/*   adrp x8, :got:stdout                                                  */
/*   ldr  x8, [x8, :got_lo12:stdout]   // address of the stdout object      */
/*   ldr  x0, [x8]                     // the FILE* the object holds         */
/* An unresolved data import instead receives a PROT_NONE sentinel slot, so  */
/* that second load faults with SIGSEGV. The binding must therefore be a     */
/* readable, writable host location holding a live host FILE*: never a       */
/* function entry, never a BRK sentinel, never a call target.               */
/*                                                                          */
/* The guest hands the same FILE* back to the host stdio functions below, so */
/* the guest stream and the host stream stay one object and guest output     */
/* reaches the host log.                                                     */
/* ========================================================================= */

/* These buffers live on the guest stack (the guest stack is the host thread
 * stack of the running guest), so the bounds stay small: the whole scratch
 * area of one call must stay a small fraction of the guest stack. */
#define NSO_STDIO_FORMAT_LIMIT 4096
#define NSO_STDIO_TEXT_LIMIT 16384
#define NSO_STDIO_OUTPUT_LIMIT 16384

struct nso_stdio_stream {
    const char *name;
    FILE *object;
    bool logged;
};
static struct nso_stdio_stream s_stdio_streams[3]={
    {"stdout",NULL,false},{"stdin",NULL,false},{"stderr",NULL,false}};

static struct nso_stdio_stream *stdio_stream_for(const char *name) {
    if(!name)return NULL;
    for(size_t i=0;i<sizeof(s_stdio_streams)/sizeof(s_stdio_streams[0]);i++)
        if(!strcmp(s_stdio_streams[i].name,name))return &s_stdio_streams[i];
    return NULL;
}
/* One host mirror per guest stream. The guest may write the object slot, so
 * the mirror keeps the guest write away from the host libc's own variable. */
static FILE *stdio_stream_object(struct nso_stdio_stream *stream) {
    if(!stream->object) {
        if(!strcmp(stream->name,"stdout"))stream->object=stdout;
        else if(!strcmp(stream->name,"stdin"))stream->object=stdin;
        else stream->object=stderr;
    }
    return stream->object;
}
uint64_t nso_host_stdio_object(const char *name) {
    struct nso_stdio_stream *stream=stdio_stream_for(name);
    if(!stream)return 0;
    FILE *object=stdio_stream_object(stream);
    if(!stream->logged) {
        stream->logged=true;
        fprintf(stderr,"[hle] stdio-object: %s -> host FILE*=%p (object address=%p, writable mirror)\n",
            stream->name,(void *)object,(void *)&stream->object);
    }
    return (uint64_t)(uintptr_t)&stream->object;
}

/* The guest owns the format string, its content and its termination. Copy it
 * into a bounded host buffer before the host libc parses it, so an unbounded
 * or unterminated guest string is never walked by the host. */
static const char *stdio_format_copy(const char *guest,char *buffer,size_t limit) {
    if(!guest)return NULL;
    for(size_t i=0;i<limit;i++) {
        char c=guest[i];
        buffer[i]=c;
        if(!c)return buffer;
    }
    buffer[limit-1]='\0';
    return NULL;
}
/* Same bound for a plain guest text argument (puts/fputs). */
static const char *stdio_text_copy(const char *guest,char *buffer,size_t limit) {
    if(!guest)return NULL;
    size_t i=0;
    for(;i+1<limit;i++) {
        char c=guest[i];
        buffer[i]=c;
        if(!c)return buffer;
    }
    buffer[i]='\0';
    if(guest[i])fprintf(stderr,"[hle] stdio: guest text truncated at %zu bytes\n",limit-1);
    return buffer;
}

int nso_host_vprintf(const char *fmt,va_list ap) {
    char format[NSO_STDIO_FORMAT_LIMIT];
    const char *parsed=stdio_format_copy(fmt,format,sizeof(format));
    if(!parsed) {
        fprintf(stderr,"[hle] printf: unusable guest format string %p\n",(const void *)fmt);
        return -1;
    }
    int result=vprintf(parsed,ap);
    fprintf(stderr,"[hle] printf: -> %d\n",result);
    return result;
}
int nso_host_vfprintf(FILE *stream,const char *fmt,va_list ap) {
    char format[NSO_STDIO_FORMAT_LIMIT];
    const char *parsed=stdio_format_copy(fmt,format,sizeof(format));
    if(!parsed) {
        fprintf(stderr,"[hle] fprintf: unusable guest format string %p\n",(const void *)fmt);
        return -1;
    }
    int result=vfprintf(stream,parsed,ap);
    fprintf(stderr,"[hle] fprintf: stream=%p -> %d\n",(void *)stream,result);
    return result;
}
/* sprintf has no guest-supplied bound, so the formatted result is produced in
 * a bounded host buffer and copied out. The guest sees the full length, and a
 * truncated copy is reported instead of overrunning guest memory. */
int nso_host_vsprintf(char *buf,const char *fmt,va_list ap) {
    char format[NSO_STDIO_FORMAT_LIMIT];
    const char *parsed=stdio_format_copy(fmt,format,sizeof(format));
    if(!parsed) {
        fprintf(stderr,"[hle] sprintf: unusable guest format string %p\n",(const void *)fmt);
        return -1;
    }
    char scratch[NSO_STDIO_OUTPUT_LIMIT];
    int result=vsnprintf(scratch,sizeof(scratch),parsed,ap);
    if(result<0) {
        fprintf(stderr,"[hle] sprintf: formatting failed\n");
        return result;
    }
    size_t length=(size_t)result;
    size_t copy=length<sizeof(scratch)?length:sizeof(scratch)-1;
    if(copy<length)fprintf(stderr,"[hle] sprintf: guest buffer truncated at %zu of %zu bytes\n",copy,length);
    if(buf)memcpy(buf,scratch,copy+1);
    fprintf(stderr,"[hle] sprintf: -> %d bytes\n",result);
    return result;
}
int nso_host_fflush(FILE *stream) {
    int result=fflush(stream);
    fprintf(stderr,"[hle] fflush: stream=%p -> %d\n",(void *)stream,result);
    return result;
}
size_t nso_host_fwrite(const void *ptr,size_t size,size_t count,FILE *stream) {
    size_t result=fwrite(ptr,size,count,stream);
    fprintf(stderr,"[hle] fwrite: size=%zu count=%zu -> %zu\n",size,count,result);
    return result;
}
int nso_host_fputs(const char *text,FILE *stream) {
    char buffer[NSO_STDIO_TEXT_LIMIT];
    const char *copy=stdio_text_copy(text,buffer,sizeof(buffer));
    if(!copy) {
        fprintf(stderr,"[hle] fputs: unusable guest text %p\n",(const void *)text);
        return -1;
    }
    int result=fputs(copy,stream);
    fprintf(stderr,"[hle] fputs: stream=%p -> %d\n",(void *)stream,result);
    return result;
}
int nso_host_fputc(int c,FILE *stream) {
    int result=fputc(c,stream);
    fprintf(stderr,"[hle] fputc: c=%d stream=%p -> %d\n",c,(void *)stream,result);
    return result;
}
int nso_host_puts(const char *text) {
    char buffer[NSO_STDIO_TEXT_LIMIT];
    const char *copy=stdio_text_copy(text,buffer,sizeof(buffer));
    if(!copy) {
        fprintf(stderr,"[hle] puts: unusable guest text %p\n",(const void *)text);
        return -1;
    }
    int result=puts(copy);
    fprintf(stderr,"[hle] puts: -> %d\n",result);
    return result;
}

void nso_host_throw_system_error(int ev, const char *what_arg) {
    fprintf(stderr, "[hle] std::__throw_system_error: ev=%d what=%s\n", ev, what_arg ? what_arg : "(null)");
    abort();
}

void nso_host_libcpp_verbose_abort(const char *format, ...) {
    va_list args;
    va_start(args, format);
    fprintf(stderr, "[hle] libcpp_verbose_abort: ");
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
    va_end(args);
    abort();
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
    if(!strcmp(name,"_ZN2nn2fs13OpenDirectoryEPNS0_15DirectoryHandleEPKci") || !strcmp(name,"nnfsOpenDirectory"))
        return (uint64_t)(uintptr_t)&nso_host_fs_open_directory;
    if(!strcmp(name,"_ZN2nn2fs22GetDirectoryEntryCountEPlNS0_15DirectoryHandleE") || !strcmp(name,"nnfsGetDirectoryEntryCount"))
        return (uint64_t)(uintptr_t)&nso_host_fs_get_directory_entry_count;
    if(!strcmp(name,"_ZN2nn2fs13ReadDirectoryEPlPNS0_14DirectoryEntryENS0_15DirectoryHandleEl") || !strcmp(name,"nnfsReadDirectory"))
        return (uint64_t)(uintptr_t)&nso_host_fs_read_directory;
    if(!strcmp(name,"_ZN2nn2fs14CloseDirectoryENS0_15DirectoryHandleE") || !strcmp(name,"nnfsCloseDirectory"))
        return (uint64_t)(uintptr_t)&nso_host_fs_close_directory;
    if(!strcmp(name,"_ZN2nn2fs15CreateDirectoryEPKc") || !strcmp(name,"nnfsCreateDirectory"))
        return (uint64_t)(uintptr_t)&nso_host_fs_create_directory;
    if(!strcmp(name,"_ZN2nn2fs26DisableGlobalFileDataCacheEv"))
        return (uint64_t)(uintptr_t)&nso_host_fs_disable_global_file_data_cache;
    if(!strcmp(name,"_ZN2nn2fs25EnableGlobalFileDataCacheEPvm"))
        return (uint64_t)(uintptr_t)&nso_host_fs_enable_global_file_data_cache;
    if(!strcmp(name,"_ZN2nn3aoc17CountAddOnContentEv") || !strcmp(name,"nnaocCountAddOnContent"))
        return (uint64_t)(uintptr_t)&nso_host_aoc_count_add_on_content;
    if(!strcmp(name,"_ZN2nn3aoc16ListAddOnContentEPiii") || !strcmp(name,"nnaocListAddOnContent"))
        return (uint64_t)(uintptr_t)&nso_host_aoc_list_add_on_content;
    if(!strcmp(name,"_ZN2nn3aoc31GetAddOnContentListChangedEventEPNS_2os11SystemEventE"))
        return (uint64_t)(uintptr_t)&nso_host_aoc_get_add_on_content_list_changed_event;
    if(!strcmp(name,"_ZN2nn2os15WaitSystemEventEPNS0_15SystemEventTypeE"))
        return (uint64_t)(uintptr_t)&nso_host_os_wait_system_event;
    if(!strcmp(name,"_ZN2nn2os17SignalSystemEventEPNS0_15SystemEventTypeE"))
        return (uint64_t)(uintptr_t)&nso_host_os_signal_system_event;
    if(!strcmp(name,"_ZN2nn2os18DestroySystemEventEPNS0_15SystemEventTypeE"))
        return (uint64_t)(uintptr_t)&nso_host_os_destroy_system_event;
    if(!strcmp(name,"_ZN2nn2fs31QueryMountAddOnContentCacheSizeEPmi") || !strcmp(name,"nnfsQueryMountAddOnContentCacheSize"))
        return (uint64_t)(uintptr_t)&nso_host_fs_query_mount_add_on_content_cache_size;
    if(!strcmp(name,"_ZN2nn2fs17MountAddOnContentEPKciPvm") || !strcmp(name,"nnfsMountAddOnContent"))
        return (uint64_t)(uintptr_t)&nso_host_fs_mount_add_on_content;
    if(!strcmp(name,"_ZN2nn4nifm10InitializeEv") || !strcmp(name,"nnnifmInitialize"))
        return (uint64_t)(uintptr_t)&nso_host_nifm_initialize;
    if(!strcmp(name,"_ZN2nn3ldn8GetStateEv"))
        return (uint64_t)(uintptr_t)&nso_host_ldn_get_state;
    if(!strcmp(name,"_ZN2nn6socket10InitializeEPvmmi") || !strcmp(name,"nnsocketInitialize"))
        return (uint64_t)(uintptr_t)&nso_host_socket_initialize;
    if(!strcmp(name,"_ZN2nn6socket7EventFdEmNS0_12EventFdFlagsE"))
        return (uint64_t)(uintptr_t)&nso_host_socket_event_fd;
    if(!strcmp(name,"_ZN2nn6socket12EventFdWriteEim"))
        return (uint64_t)(uintptr_t)&nso_host_socket_event_fd_write;
    if(!strcmp(name,"_ZN2nn6socket11EventFdReadEiPm"))
        return (uint64_t)(uintptr_t)&nso_host_socket_event_fd_read;
    if(!strcmp(name,"_ZN2nn6socket5CloseEi"))
        return (uint64_t)(uintptr_t)&nso_host_socket_close;
    if(!strcmp(name,"_ZN2nn6socket12GetLastErrorEv"))
        return (uint64_t)(uintptr_t)&nso_host_socket_get_last_error;
    if(!strcmp(name,"_ZN2nn6socket4PollEPNS0_6PollFdEmi"))
        return (uint64_t)(uintptr_t)&nso_host_socket_poll;
    if(!strcmp(name,"nnsocketPoll"))
        return (uint64_t)(uintptr_t)&nso_host_socket_poll_c;
    if(!strcmp(name,"_ZN2nn7account10InitializeEv") || !strcmp(name,"nnaccountInitialize"))
        return (uint64_t)(uintptr_t)&nso_host_account_initialize;
    if(!strcmp(name,"_ZN2nn2oe10InitializeEv") || !strcmp(name,"nnoeInitialize"))
        return (uint64_t)(uintptr_t)&nso_host_oe_initialize;
    if(!strcmp(name,"_ZN2nn2oe18GetDesiredLanguageEv") || !strcmp(name,"nnoeGetDesiredLanguage"))
        return (uint64_t)(uintptr_t)&nso_host_oe_get_desired_language;
    if(!strcmp(name,"_ZN2nn2oe15SetCpuBoostModeENS0_12CpuBoostModeE"))
        return (uint64_t)(uintptr_t)&nso_host_oe_set_cpu_boost_mode;
    if(!strcmp(name,"_ZN2nn3hid18InitializeKeyboardEv"))
        return (uint64_t)(uintptr_t)&nso_host_hid_initialize_keyboard;
    if(!strcmp(name,"_ZN2nn3hid15InitializeMouseEv"))
        return (uint64_t)(uintptr_t)&nso_host_hid_initialize_mouse;
    if(!strcmp(name,"_ZN2nn2oe20GetCurrentFocusStateEv"))
        return (uint64_t)(uintptr_t)&nso_host_oe_get_current_focus_state;
    if(!strcmp(name,"_ZN2nn2oe44SetPerformanceModeChangedNotificationEnabledEb"))
        return (uint64_t)(uintptr_t)&nso_host_oe_set_performance_mode_changed_notification_enabled;
    if(!strcmp(name,"_ZN2nn2oe42SetOperationModeChangedNotificationEnabledEb"))
        return (uint64_t)(uintptr_t)&nso_host_oe_set_operation_mode_changed_notification_enabled;
    if(!strcmp(name,"_ZN2nn2oe20SetFocusHandlingModeENS0_17FocusHandlingModeE"))
        return (uint64_t)(uintptr_t)&nso_host_oe_set_focus_handling_mode;
    if(!strcmp(name,"_ZN2nn2oe28SetResumeNotificationEnabledEb"))
        return (uint64_t)(uintptr_t)&nso_host_oe_set_resume_notification_enabled;
    if(!strcmp(name,"_ZN2nn2oe16GetOperationModeEv"))
        return (uint64_t)(uintptr_t)&nso_host_oe_get_operation_mode;
    if(!strcmp(name,"_ZN2nv20SetGraphicsAllocatorEPFPvmmS0_EPFvS0_S0_EPFS0_S0_mS0_ES0_"))
        return (uint64_t)(uintptr_t)&nso_host_nv_set_graphics_allocator;
    if(!strcmp(name,"_ZN2nv28SetGraphicsDevtoolsAllocatorEPFPvmmS0_EPFvS0_S0_EPFS0_S0_mS0_ES0_"))
        return (uint64_t)(uintptr_t)&nso_host_nv_set_graphics_devtools_allocator;
    if(!strcmp(name,"_ZN2nv18InitializeGraphicsEPvm"))
        return (uint64_t)(uintptr_t)&nso_host_nv_initialize_graphics;
    if(!strcmp(name,"_ZN2nn2oe27SetPerformanceConfigurationENS0_15PerformanceModeEi"))
        return (uint64_t)(uintptr_t)&nso_host_oe_set_performance_configuration;
    if(!strcmp(name,"_ZN2nn2oe18GetPerformanceModeEv"))
        return (uint64_t)(uintptr_t)&nso_host_oe_get_performance_mode;
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
    if(!strcmp(name,"_ZN2nn2os15InitializeEventEPNS0_9EventTypeEbNS0_14EventClearModeE") || !strcmp(name,"nnosInitializeEvent"))
        return (uint64_t)(uintptr_t)&nso_host_os_initialize_event;
    if(!strcmp(name,"_ZN2nn2os13FinalizeEventEPNS0_9EventTypeE") || !strcmp(name,"nnosFinalizeEvent"))
        return (uint64_t)(uintptr_t)&nso_host_os_finalize_event;
    if(!strcmp(name,"_ZN2nn2os11SignalEventEPNS0_9EventTypeE") || !strcmp(name,"nnosSignalEvent"))
        return (uint64_t)(uintptr_t)&nso_host_os_signal_event;
    if(!strcmp(name,"_ZN2nn2os10ClearEventEPNS0_9EventTypeE"))
        return (uint64_t)(uintptr_t)&nso_host_os_clear_event;
    if(!strcmp(name,"_ZN2nn2os9WaitEventEPNS0_9EventTypeE") || !strcmp(name,"nnosWaitEvent"))
        return (uint64_t)(uintptr_t)&nso_host_os_wait_event;
    if(!strcmp(name,"_ZN2nn2os12TryWaitEventEPNS0_9EventTypeE"))
        return (uint64_t)(uintptr_t)&nso_host_os_try_wait_event;
    if(!strcmp(name,"_ZN2nn2os14TimedWaitEventEPNS0_9EventTypeENS_8TimeSpanE"))
        return (uint64_t)(uintptr_t)&nso_host_os_timed_wait_event;
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
    if(!strcmp(name,"_ZN5movie12SetAllocatorEPFPvmmS0_EPFvS0_S0_EPFS0_S0_mS0_ES0_"))
        return (uint64_t)(uintptr_t)&nso_host_movie_set_allocator;
    if(!strcmp(name,"atexit"))return (uint64_t)(uintptr_t)&nso_host_atexit;
    if(!strcmp(name,"__cxa_atexit"))return (uint64_t)(uintptr_t)&nso_host_cxa_atexit;
    if(!strcmp(name,"__cxa_thread_atexit"))return (uint64_t)(uintptr_t)&nso_host_cxa_thread_atexit;
    if(!strcmp(name,"__cxa_thread_atexit_impl"))return (uint64_t)(uintptr_t)&nso_host_cxa_thread_atexit_impl;
    if(!strcmp(name,"__cxa_finalize"))return (uint64_t)(uintptr_t)&nso_host_cxa_finalize;
    if(!strcmp(name,"_ZTHN13LowLevelTasks5FTask10ActiveTaskE") ||
       !strcmp(name,"_ZTHN13LowLevelTasks7Private20FOversubscriptionTls26bIsOversubscriptionAllowedE") ||
       !strcmp(name,"_ZTHN30FOverridableSerializationLogic20OverriddenPropertiesE") ||
       !strcmp(name,"_ZTHN30FOverridableSerializationLogic28bUseOverridableSerializationE") ||
       !strcmp(name,"_ZTHN17FPropertyTagScope18CurrentPropertyTagE"))
        return (uint64_t)(uintptr_t)&nso_host_tls_init_noop;
    if(!strcmp(name,"exit"))return (uint64_t)(uintptr_t)&nso_host_exit;
    if(!strcmp(name,"_Exit") || !strcmp(name,"_exit"))return (uint64_t)(uintptr_t)&nso_host_immediate_exit;
    if(!strcmp(name,"posix_memalign"))return (uint64_t)(uintptr_t)&nso_host_posix_memalign;
    if(!strcmp(name,"memalign"))return (uint64_t)(uintptr_t)&nso_host_memalign;
    if(!strcmp(name,"aligned_alloc"))return (uint64_t)(uintptr_t)&nso_host_aligned_alloc;
    if(!strcmp(name,"valloc"))return (uint64_t)(uintptr_t)&nso_host_valloc;
    if(!strcmp(name,"free"))return (uint64_t)(uintptr_t)&nso_host_free;
    if(!strcmp(name,"_ZN2nn4diag6detail9AbortImplEv") || !strcmp(name,"nndetailDiagAbortImpl"))
        return (uint64_t)(uintptr_t)&nso_host_diag_abort_impl;
    if(!strcmp(name,"_ZN2nn4diag12GetBacktraceEPmi"))
        return (uint64_t)(uintptr_t)&nso_host_diag_get_backtrace;
    if(!strcmp(name,"_ZNSt3__15mutex4lockEv"))
        return (uint64_t)(uintptr_t)&nso_host_std_mutex_lock;
    if(!strcmp(name,"_ZNSt3__15mutex6unlockEv"))
        return (uint64_t)(uintptr_t)&nso_host_std_mutex_unlock;
    if(!strcmp(name,"_ZNSt3__118condition_variable10notify_oneEv"))
        return (uint64_t)(uintptr_t)&nso_host_std_cv_notify_one;
    if(!strcmp(name,"_ZNSt3__118condition_variable10notify_allEv"))
        return (uint64_t)(uintptr_t)&nso_host_std_cv_notify_all;
    if(!strcmp(name,"_ZNSt3__118condition_variable4waitERNS_11unique_lockINS_5mutexEEE"))
        return (uint64_t)(uintptr_t)&nso_host_std_cv_wait;
    if(!strcmp(name,"_ZNSt3__16chrono12steady_clock3nowEv"))
        return (uint64_t)(uintptr_t)&nso_host_steady_clock_now;
    if(!strcmp(name,"pthread_cond_clockwait"))
        return (uint64_t)(uintptr_t)&nso_host_pthread_cond_clockwait;
    if(!strcmp(name,"_ZNSt3__120__throw_system_errorEiPKc"))
        return (uint64_t)(uintptr_t)&nso_host_throw_system_error;
    if(!strcmp(name,"_ZNSt3__122__libcpp_verbose_abortEPKcz"))
        return (uint64_t)(uintptr_t)&nso_host_libcpp_verbose_abort;
    if(!strcmp(name,"setlocale"))
        return (uint64_t)(uintptr_t)&nso_host_setlocale;
    if(!strcmp(name,"getenv"))
        return (uint64_t)(uintptr_t)&nso_host_getenv;
    if(!strcmp(name,"nl_langinfo"))
        return (uint64_t)(uintptr_t)&nso_host_nl_langinfo;
    if(!strcmp(name,"gettimeofday"))
        return (uint64_t)(uintptr_t)&nso_host_gettimeofday;
    if(!strcmp(name,"getpid"))
        return (uint64_t)(uintptr_t)&nso_host_getpid;
    if(!strcmp(name,"_ZN2nn2os23GetCurrentProcessHandleEv") || !strcmp(name,"nnosGetCurrentProcessHandle"))
        return (uint64_t)(uintptr_t)&nso_host_get_current_process_handle;
    if(!strcmp(name,"nnResultIsFailure"))
        return (uint64_t)(uintptr_t)&nso_host_nn_result_is_failure;
    if(!strcmp(name,"nnResultIsSuccess"))
        return (uint64_t)(uintptr_t)&nso_host_nn_result_is_success;
    if(!strcmp(name,"nnResultGetModule"))
        return (uint64_t)(uintptr_t)&nso_host_nn_result_get_module;
    if(!strcmp(name,"nnResultGetDescription"))
        return (uint64_t)(uintptr_t)&nso_host_nn_result_get_description;
    if(!strcmp(name,"nnResultSuccess"))
        return (uint64_t)(uintptr_t)&nso_host_nn_result_success;
    if(!strcmp(name,"nnsslInitialize"))
        return (uint64_t)(uintptr_t)&nso_host_ssl_initialize;
    if(!strcmp(name,"nnsslFinalize"))
        return (uint64_t)(uintptr_t)&nso_host_ssl_finalize;
    /* Data imports: the resolved value is the address of the host object the
     * guest dereferences, never a callable entry. */
    if(!strcmp(name,"stdout") || !strcmp(name,"stdin") || !strcmp(name,"stderr"))
        return nso_host_stdio_object(name);
    /* Print family: the variadic gateway rebuilds an AAPCS64 va_list from the
     * guest registers and passes it as an explicit argument. */
    if(!strcmp(name,"printf"))
        return (uint64_t)(uintptr_t)&nso_host_vprintf;
    if(!strcmp(name,"fprintf"))
        return (uint64_t)(uintptr_t)&nso_host_vfprintf;
    if(!strcmp(name,"sprintf"))
        return (uint64_t)(uintptr_t)&nso_host_vsprintf;
    if(!strcmp(name,"fflush"))
        return (uint64_t)(uintptr_t)&nso_host_fflush;
    if(!strcmp(name,"fwrite"))
        return (uint64_t)(uintptr_t)&nso_host_fwrite;
    if(!strcmp(name,"fputs"))
        return (uint64_t)(uintptr_t)&nso_host_fputs;
    if(!strcmp(name,"fputc"))
        return (uint64_t)(uintptr_t)&nso_host_fputc;
    if(!strcmp(name,"puts"))
        return (uint64_t)(uintptr_t)&nso_host_puts;
    if(!strcmp(name,"__cxa_pure_virtual"))
        return (uint64_t)(uintptr_t)&nso_host_cxa_pure_virtual;
    return extension_resolver?extension_resolver(index,name):0;
}
