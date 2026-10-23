#ifndef NSO_ADDRESS_REGION_H
#define NSO_ADDRESS_REGION_H
#include <stddef.h>
#include <stdint.h>
/* Observed SDK Result ABI: x0=output pointer, x1=size, w0=result.
 * Reserve virtual addresses only. No memory block/physical commit semantics.
 * Nonzero, 4-KiB-multiple sizes are the supported contract. */
uint32_t nso_host_allocate_address_region(uintptr_t *out,size_t size);
uint32_t nso_host_free_address_region(uintptr_t address);

/* Internal native page interface. These statuses are NOT SDK Results and none
 * of these functions is registered in the guest import resolver. Anonymous
 * host VM can be lazy/overcommitted: mapped_guest_bytes counts accessible guest
 * spans, not resident bytes or a Horizon physical-memory budget.
 *
 * A quarantined reservation permits only whole-region release. Its accessible
 * contents/protections are indeterminate; callers must quiesce all users before
 * release. Guest reads/writes themselves are not synchronized by this API. */
enum nso_owned_page_status {
    NSO_OWNED_PAGE_OK,
    NSO_OWNED_PAGE_UNSUPPORTED,
    NSO_OWNED_PAGE_NOT_OWNED,
    NSO_OWNED_PAGE_NO_MEMORY,
    NSO_OWNED_PAGE_SYSTEM_ERROR,
    NSO_OWNED_PAGE_QUARANTINED
};
enum nso_owned_page_detail {
    NSO_OWNED_PAGE_DETAIL_NONE,
    NSO_OWNED_PAGE_DETAIL_RANGE,
    NSO_OWNED_PAGE_DETAIL_ALIGNMENT,
    NSO_OWNED_PAGE_DETAIL_OVERLAP,
    NSO_OWNED_PAGE_DETAIL_HOLES,
    NSO_OWNED_PAGE_DETAIL_OVERFLOW
};
struct nso_owned_page_result {
    enum nso_owned_page_status status;
    enum nso_owned_page_detail detail;
    int native_errno;
    int restore_errno;
};
struct nso_owned_page_usage {
    size_t region_count;
    size_t reserved_guest_bytes;
    size_t mapped_guest_bytes;
    size_t quarantined_regions;
    /* False => mapped_guest_bytes is zero/unknown, not the old complete sum. */
    int mapped_guest_bytes_valid;
};
struct nso_owned_page_result nso_host_owned_pages_map(uintptr_t address,size_t size);
struct nso_owned_page_result nso_host_owned_pages_free(uintptr_t address,size_t size);
struct nso_owned_page_result nso_host_owned_pages_usage(struct nso_owned_page_usage *out);

#if defined(NSO_OWNED_PAGES_TESTING)
/* Test binaries only: install while guest users are quiescent. NULL restores the
 * real syscall. Production builds have neither hooks nor this setter symbol. */
void nso_host_owned_pages_test_vm(int (*protect)(void *,size_t,int),
                                void *(*replace_none)(void *,size_t));
#endif
#endif
