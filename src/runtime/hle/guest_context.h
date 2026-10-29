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
    GUEST_CONTEXT_SYSTEM_ERROR, GUEST_CONTEXT_QUARANTINED, GUEST_CONTEXT_UNSUPPORTED
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
    /* Exact logical guest window, contained in verified native usable memory. */
    uintptr_t address;
    size_t bytes, guard_bytes;
    /* Outer guards bracket this raw RW allocation, not the logical window. */
    uintptr_t allocation_address;
    size_t allocation_bytes;
    uintptr_t native_address;
    size_t native_bytes;
    size_t administrative_bytes;
    unsigned capacity_attempts;
};
typedef void *(*guest_primary_callback)(guest_context *,void *);

/* Explicit port-policy memory-management arena. Separate from application
 * backing quota, not kernel page tables/RSS. A 256-byte slab includes its header
 * and padding; only real live ticket/page/VA nodes occupy slabs. Other native
 * runtime control/loader/pthread metadata is outside this policy. */
enum guest_metadata_kind {GUEST_METADATA_TICKET=1,GUEST_METADATA_PAGE_REGION,
    GUEST_METADATA_PAGE_ALLOCATION,GUEST_METADATA_VA_REGION,GUEST_METADATA_VA_INTERVAL,
    GUEST_METADATA_KIND_END};
struct guest_resource_usage {
    size_t total_bytes,used_bytes,blocks,counts[GUEST_METADATA_KIND_END];
    int valid;
};
struct guest_context_result guest_context_create_with_resources(
        const struct guest_context_config *,size_t resource_bytes,guest_context **out);
struct guest_context_result guest_metadata_allocate(guest_context *,enum guest_metadata_kind,size_t,void **out);
struct guest_context_result guest_metadata_release(guest_context *,enum guest_metadata_kind,void *);
int guest_metadata_is_owned(guest_context *,enum guest_metadata_kind,const void *,size_t);
struct guest_context_result guest_context_resource_usage(guest_context *,struct guest_resource_usage *out);
#if defined(GUEST_RESOURCE_TESTING)
/* Test-only normal mmap/munmap failure provider; absent from production. */
void guest_context_resource_test_vm(void *(*map)(size_t),int (*unmap)(void *,size_t));
#endif

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
/* Split one live ticket into two same-state tickets. Total charge unchanged;
 * metadata failure leaves the original unchanged. Caller owns both outputs. */
struct guest_context_result guest_charge_split(guest_charge_ticket *,size_t left_bytes,guest_charge_ticket **right);
struct guest_context_result guest_charge_commit(guest_charge_ticket *);
struct guest_context_result guest_charge_cancel(guest_charge_ticket *);
struct guest_context_result guest_charge_release(guest_charge_ticket *);
struct guest_context_result guest_context_usage(guest_context *,struct guest_context_usage *out);

/* Synchronous ordinary native callback on a separately owned pthread stack.
 * Guard/padding VA and host pthread/TLS/bookkeeping overhead are not application
 * quota charges. Quota charges the exact requested logical guest window. Public
 * pthread stack queries verify capacity BEFORE callback. Short capacity probes
 * are joined/unmapped/refunded before retry; at most three attempts and at most
 * four host pages of extra RW administrative space, with checked arithmetic.
 * No private pthread layout/constant deficit or default host stack is assumed.
 * Native usable capacity may exceed the logical window; SP must remain inside
 * the exact logical window. Guards are inspected using allocation_* above.
 * No SDK thread/TCB,
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
/* Synthetic capacity-query outcomes, absent from production. A short report
 * suppresses callback and exercises join/free/precharge, never guest execution. */
void guest_context_test_capacity(guest_context *,size_t shortfall,unsigned reports,int query_error);
unsigned guest_context_test_capacity_attempts(guest_context *);
#endif
#ifdef __cplusplus
}
#endif
#endif
