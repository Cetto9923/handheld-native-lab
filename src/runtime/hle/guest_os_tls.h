#ifndef NSO_GUEST_OS_TLS_H
#define NSO_GUEST_OS_TLS_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define GUEST_OS_TLS_SLOT_COUNT 32u
#define GUEST_OS_TLS_RESERVED_SLOT 16u
typedef struct guest_os_tls_manager guest_os_tls_manager;
typedef struct guest_os_tls_thread guest_os_tls_thread;
enum guest_os_tls_status {
    GUEST_OS_TLS_OK, GUEST_OS_TLS_INVALID, GUEST_OS_TLS_RESERVED,
    GUEST_OS_TLS_EXHAUSTED, GUEST_OS_TLS_NOT_ALLOCATED,
    GUEST_OS_TLS_NOT_REGISTERED, GUEST_OS_TLS_NO_MEMORY,
    GUEST_OS_TLS_BUSY, GUEST_OS_TLS_BAD_STATE,
    GUEST_OS_TLS_NEEDS_INVOKER, GUEST_OS_TLS_QUARANTINED,
    GUEST_OS_TLS_SYSTEM_ERROR
};
enum guest_os_tls_pool { GUEST_OS_TLS_APP, GUEST_OS_TLS_SDK };
enum guest_os_tls_thread_state {
    GUEST_OS_TLS_THREAD_LIVE, GUEST_OS_TLS_THREAD_CLEANING,
    GUEST_OS_TLS_THREAD_CLEANED, GUEST_OS_TLS_THREAD_QUARANTINED
};
struct guest_os_tls_result { enum guest_os_tls_status status; int native_error; };
enum guest_os_tls_invoke_status {
    GUEST_OS_TLS_INVOKE_OK, GUEST_OS_TLS_INVOKE_FAILED, GUEST_OS_TLS_INVOKE_UNKNOWN
};
struct guest_os_tls_invoke_result {
    enum guest_os_tls_invoke_status status;
    int provider_error;
};
/* Destructor is an opaque guest identity, never cast to a host function. The
 * future ABI/call-scope provider chooses how to invoke it. It may reenter
 * Get/Set/Allocate/Free/Register, since no manager lock spans this call. */
typedef struct guest_os_tls_invoke_result (*guest_os_tls_invoker)(
    guest_os_tls_manager *, guest_os_tls_thread *, uint64_t destructor,
    uint64_t value, void *opaque);
struct guest_os_tls_cleanup_report {
    unsigned rounds_scanned;
    size_t invocations, noop_visits;
    size_t remaining_nonzero;
    unsigned failed_round;
    uint32_t failed_slot;
    uint64_t failed_destructor, failed_value;
    struct guest_os_tls_invoke_result invocation;
    enum guest_os_tls_thread_state state;
};
struct guest_os_tls_counts {
    uint32_t app_used, sdk_used;
    size_t registered_threads, cleaning_threads, cleaned_threads, quarantined_threads;
    int quarantined;
};

/* Independent logical OS-TLS storage, not an SDK ThreadType/TP layout, host
 * identity, ELF TLS or ELF destructor list. Manager owns registered records and
 * their 32 u64 values. Thread handles expire at unregister; callers serialize
 * their lifetime against API entry, and quiesce value users for cleanup and
 * unregister. No process-global singleton or implicit current thread exists.
 * Ordinary operations serialize on one manager mutex. Reentry from the typed
 * invoker is supported; one cleanup may run per manager. Nested cleanup or
 * unregister of the cleaning thread is BUSY.
 * Unrelated concurrent writers during cleanup are outside the proven SDK scope.
 * Invokers must return normally; asynchronous cancellation/nonlocal exits are
 * unsupported and must not reclaim manager/thread/provider state.
 */
struct guest_os_tls_result guest_os_tls_create(guest_os_tls_manager **out);
struct guest_os_tls_result guest_os_tls_register(guest_os_tls_manager *, guest_os_tls_thread **out);
/* APP searches 0..15 ascending, SDK 31..17 descending. Slot 16 is unavailable.
 * Destructor identity 0 is an allocated noop, not a free slot. Allocation clears
 * this slot in ALL registered records before publishing the u32 output. On any
 * failure the output is unchanged. Exhaustion is typed, not an SDK Result/abort. */
struct guest_os_tls_result guest_os_tls_allocate(guest_os_tls_manager *, enum guest_os_tls_pool,
                                               uint64_t destructor, uint32_t *out);
/* Free does not invoke a destructor or clear any per-thread value. Get/Set may
 * access unallocated slots (stale values survive Free), but reject 16/out of range.
 * Set is allowed for LIVE/CLEANING records; CLEANED records remain readable. */
struct guest_os_tls_result guest_os_tls_free(guest_os_tls_manager *, uint32_t slot);
struct guest_os_tls_result guest_os_tls_get(guest_os_tls_manager *, guest_os_tls_thread *,
                                          uint32_t slot, uint64_t *out);
struct guest_os_tls_result guest_os_tls_set(guest_os_tls_manager *, guest_os_tls_thread *,
                                          uint32_t slot, uint64_t value);
struct guest_os_tls_result guest_os_tls_count(guest_os_tls_manager *, struct guest_os_tls_counts *out);
/* Ascending 0..31, at most 5 scans. Scan 1 visits every allocated destructor even
 * at value 0; scans 2..5 visit only nonzero values and reread the callback table.
 * Successful/noop return clears the CURRENT value after the callback. Reentry
 * may leave earlier slots nonzero after scan 5; CLEANED never means all-zero.
 * NULL invoker + any real destructor fails preflight with NEEDS_INVOKER before
 * any state/value change, and may be retried with a provider.
 * Non-OK provider result retains current values/event, quarantines the thread
 * AND manager, and prohibits any further mutation/cleanup invocation. Reads,
 * counts and report remain available. No forced discard/retry is provided. */
struct guest_os_tls_result guest_os_tls_cleanup(guest_os_tls_manager *, guest_os_tls_thread *,
                                              guest_os_tls_invoker, void *opaque,
                                              struct guest_os_tls_cleanup_report *out);
struct guest_os_tls_result guest_os_tls_report(guest_os_tls_manager *, guest_os_tls_thread *,
                                             struct guest_os_tls_cleanup_report *out);
/* Only CLEANED records can unregister (including a five-scan nonzero remainder).
 * This frees only our logical record, never a guest value/object. Quarantined
 * records remain owned and make manager destruction BUSY. Destroy requires no
 * registered records and exclusion of concurrent API calls. It drops slot
 * identities without invoking them; those identities never own guest objects. */
struct guest_os_tls_result guest_os_tls_unregister(guest_os_tls_manager *, guest_os_tls_thread *);
struct guest_os_tls_result guest_os_tls_destroy(guest_os_tls_manager *);

#ifdef __cplusplus
}
#endif
#endif
