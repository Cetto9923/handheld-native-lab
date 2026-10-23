#include "nso_host.h"
#include "address_region.h"
#include "mutex.h"
#include "exit_registry.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
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
void nso_host_set_extension_resolver(nso_import_fn resolver) {extension_resolver=resolver;}
uint64_t nso_host_resolve(uint32_t index,const char *name) {
    if(!name)return 0;
#if defined(__ANDROID__) || (defined(NSO_WITH_CXX_GUARDS) && NSO_WITH_CXX_GUARDS)
    if(!strcmp(name,"__cxa_guard_acquire"))return (uint64_t)(uintptr_t)&__cxa_guard_acquire;
    if(!strcmp(name,"__cxa_guard_release"))return (uint64_t)(uintptr_t)&__cxa_guard_release;
    if(!strcmp(name,"__cxa_guard_abort"))return (uint64_t)(uintptr_t)&__cxa_guard_abort;
#endif
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
