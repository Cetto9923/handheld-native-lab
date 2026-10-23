#ifndef NSO_GUEST_CONTEXT_H
#define NSO_GUEST_CONTEXT_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct guest_context guest_context;
typedef struct guest_charge_ticket guest_charge_ticket;
enum guest_context_status {
    GUEST_CONTEXT_OK, GUEST_CONTEXT_INVALID, GUEST_CONTEXT_LIMIT,
    GUEST_CONTEXT_NO_MEMORY, GUEST_CONTEXT_BUSY, GUEST_CONTEXT_BAD_STATE,
    GUEST_CONTEXT_SYSTEM_ERROR, GUEST_CONTEXT_QUARANTINED
};
struct guest_context_result {
    enum guest_context_status status;
    int native_error;
    int cleanup_error;
};
struct guest_context_config {
    /* Required exact port policy, not RAM/RSS or an original SVC measurement. */
    size_t quota_bytes;
    /* Required host-page multiple >= PTHREAD_STACK_MIN; no implicit default. */
    size_t primary_stack_bytes;
};
struct guest_context_usage {
    size_t quota_bytes, pending_bytes, charged_bytes, tickets;
    int primary_active, quarantined;
    /* No module/heap/SDK thread providers exist in this component. */
    unsigned memory_info_valid_fields;
};
struct guest_primary_stack {
    uintptr_t address;
    size_t bytes, guard_bytes;
};
typedef void *(*guest_primary_callback)(guest_context *,void *);

struct guest_context_result guest_context_create(const struct guest_context_config *,guest_context **out);
/* Users/APIs must be quiescent. Outstanding external tickets or an unjoined
 * native thread return BUSY. Failed cleanup retains the context/charge. */
struct guest_context_result guest_context_destroy(guest_context *);

/* Tickets are logical quota reservations. Reserve BEFORE allocating a resource,
 * commit only after real success, cancel pending tickets after failed creation,
 * release committed tickets only AFTER real resource release. They do not map
 * memory themselves and do not certify resident/physical bytes. The caller owns
 * each live ticket and must serialize operations on that ticket. */
struct guest_context_result guest_charge_reserve(guest_context *,size_t,guest_charge_ticket **out);
struct guest_context_result guest_charge_commit(guest_charge_ticket *);
struct guest_context_result guest_charge_cancel(guest_charge_ticket *);
struct guest_context_result guest_charge_release(guest_charge_ticket *);
struct guest_context_result guest_context_usage(guest_context *,struct guest_context_usage *out);

/* Synchronous ordinary native callback on a separately owned pthread stack.
 * Guard/padding VA and host pthread/TLS/bookkeeping overhead are not application
 * quota charges. Quota charges the exact RW stack payload. No SDK thread/TCB,
 * guest TLS, QueryMemoryInfo, game entry or TP-register adaptation is installed.
 * Return value must not point into the soon-to-be-released primary stack.
 * Cancellation/concurrent destruction of a running call is unsupported. */
/* A failed join returns QUARANTINED with ownership retained: the callback may
 * still run, so its arg/context must remain alive. No unsafe forced teardown. */
struct guest_context_result guest_context_run_primary(guest_context *,guest_primary_callback,void *arg,void **value);
/* Copy descriptor for use within the callback only; expires when run returns. */
struct guest_context_result guest_context_primary_stack(guest_context *,struct guest_primary_stack *out);

#if defined(GUEST_CONTEXT_TESTING)
void guest_context_test_fail_next_map(guest_context *,int native_error);
size_t guest_context_test_pending_before_map(guest_context *);
#endif
#ifdef __cplusplus
}
#endif
#endif
