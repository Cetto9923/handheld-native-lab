/* Ownership ledger for pointers that the guest can see.
 *
 * The guest runs on its own nn::os page heap: its blocks are host mmap
 * addresses, not host allocator chunks. The host has its own heap as well.
 * A block created on one heap and released on the other corrupts the host
 * heap: Scudo aborts with "corrupted chunk header" when a guest page
 * address is handed to the host free(), and the page allocator aborts when
 * a host chunk address is handed to nn::os::FreeMemoryPages.
 *
 * Every pointer that leaves this runtime towards the guest is recorded here
 * together with the heap that owns it, so that each release path can return
 * the block to the heap it came from. */
#ifndef NSO_HEAP_OWNER_H
#define NSO_HEAP_OWNER_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

enum heap_owner_kind {
    HEAP_OWNER_UNKNOWN = 0,
    /* A block of the guest's own nn::os page heap. It is a host address,
     * but the host allocator never produced it. */
    HEAP_OWNER_GUEST_PAGE = 1,
    /* A block of the host allocator. The guest must never release it. */
    HEAP_OWNER_HOST_SCUdo = 2
};

/* Record which heap owns a block that the guest can see. */
void heap_owner_record(void *ptr, size_t bytes, enum heap_owner_kind kind);
/* Look the owner up. HEAP_OWNER_UNKNOWN means the block was never handed
 * out by this runtime, for example a block of the guest's own allocator. */
enum heap_owner_kind heap_owner_lookup(const void *ptr, size_t *bytes_out);
/* Drop a block once it was released. */
void heap_owner_forget(const void *ptr);

#ifdef __cplusplus
}
#endif
#endif
