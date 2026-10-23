#ifndef NSO_GUEST_PAGES_H
#define NSO_GUEST_PAGES_H
#include "guest_context.h"
#include "address_region.h"
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct guest_pages guest_pages;
enum guest_pages_status {
    GUEST_PAGES_OK, GUEST_PAGES_INVALID, GUEST_PAGES_UNSUPPORTED,
    GUEST_PAGES_NOT_OWNED, GUEST_PAGES_OVERLAP, GUEST_PAGES_NO_MEMORY,
    GUEST_PAGES_QUOTA, GUEST_PAGES_CONTEXT_ERROR, GUEST_PAGES_VM_ERROR,
    GUEST_PAGES_QUARANTINED, GUEST_PAGES_BUSY, GUEST_PAGES_INCONSISTENT
};
struct guest_pages_result {
    enum guest_pages_status status;
    struct guest_context_result context, ticket_cleanup;
    struct nso_owned_page_result vm, rollback_vm;
    /* Existing address_region raw result, diagnostic only, NOT a translation
     * to an original SDK page Result. Inspect only if region_attempted!=0. */
    uint32_t region_result;
    int native_error;
    int vm_attempted, rollback_attempted, region_attempted;
};
struct guest_pages_usage {
    size_t region_count, reserved_bytes, allocation_count, ticket_count;
    size_t pending_bytes, charged_bytes;
    /* Accessible guest spans, not physical/resident RAM. Zero means unknown
     * when backing_valid==0; retained pending/charged tickets remain counted. */
    size_t backing_bytes, quarantined_regions;
    uint64_t generation;
    int backing_valid, ledger_valid;
    struct guest_context_usage context;
    struct nso_owned_page_usage vm;
};

/* One active page owner exclusively owns the address_region ledger in this
 * process. Creation requires that lower ledger to be empty; bypassing this
 * component with the raw region/page APIs is unsupported. A Context may also
 * own module/stack tickets; it must outlive this owner. No implicit quota.
 * APIs serialize owner mutations. Callers must quiesce guest memory users
 * before free/release/destroy and must exclude concurrent destroy/API entry.
 * Lock order: owner -> Context OR owner -> region, never region -> Context.
 * No resolver binding and no SDK Result/errno mapping is supplied here. */
struct guest_pages_result guest_pages_create(guest_context *, guest_pages **out);
struct guest_pages_result guest_pages_reserve(guest_pages *, size_t bytes, uintptr_t *out);
struct guest_pages_result guest_pages_map(guest_pages *, uintptr_t address, size_t bytes);
/* Only a single exact allocation identity may be freed. Partial, combined,
 * mixed, unknown or repeated frees are rejected without a VM call/refund. */
struct guest_pages_result guest_pages_free_exact(guest_pages *, uintptr_t address, size_t bytes);
/* Whole reserve release first really unmaps, then drops ALL its live tickets,
 * including retained pending tickets. The existing lower whole-unmap failure
 * aborts; this component does not invent recoverability for that fatal path. */
struct guest_pages_result guest_pages_release_region(guest_pages *, uintptr_t base);
/* Validates owner identities/disjointness/ticket sums against the exclusively
 * owned lower VA/backing totals and Context capacity. For a coherent snapshot
 * including other Context providers, callers serialize those providers too.
 * Quarantine keeps accounting but invalidates accessible backing/Query data. */
struct guest_pages_result guest_pages_usage(guest_pages *, struct guest_pages_usage *out);
/* Ordinary quiescent teardown releases whole reserves (even with live pages),
 * then frees this owner only after every real release/refund succeeded. */
struct guest_pages_result guest_pages_destroy(guest_pages *);

#ifdef __cplusplus
}
#endif
#endif
