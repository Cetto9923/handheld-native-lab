/* The NVN memory pool family of the FFRS RG477M native runtime.
 *
 * The guest creates a memory pool from a builder and then uses that pool as
 * the backing store of its buffers and textures. As with the queue and the
 * window family, the whole state lives on the host and is keyed by the
 * guest's addresses: nothing is ever written into the guest's builder or
 * pool object, because the layout of an NVN memory pool is a property of the
 * Nintendo SDK, which this runtime does not have.
 *
 * The storage a pool is built from is not allocated by this runtime. The
 * guest (Unreal Engine) allocates the memory itself and hands the range to
 * nvnMemoryPoolBuilderSetStorage, so the pool records that range and uses it
 * as the CPU mapping. Shadowing it with a second allocation would give the
 * guest a different address than the one it wrote to, which is worse than
 * having no pool at all.
 */
#ifndef FFRS_NVN_MEMORY_POOL_H
#define FFRS_NVN_MEMORY_POOL_H

#include <stddef.h>
#include <stdint.h>

/* The guest creates a few pools: one for the frame buffers, one for the
 * textures, maybe one for the upload staging. The table is small and fixed on
 * purpose; a guest that wants more is told so instead of being served
 * silently. */
#define NVN_MEMORY_POOL_MAX_BUILDERS 64
#define NVN_MEMORY_POOL_MAX_POOLS 64

/* The memory pool flags, as the vendored NVN header documents them. The
 * runtime uses them to pick the CPU and GPU access it offers. */
#define NVN_MEMORY_POOL_FLAGS_CPU_NO_ACCESS 0x1u
#define NVN_MEMORY_POOL_FLAGS_CPU_UNCACHED 0x2u
#define NVN_MEMORY_POOL_FLAGS_CPU_CACHED 0x4u
#define NVN_MEMORY_POOL_FLAGS_GPU_NO_ACCESS 0x8u
#define NVN_MEMORY_POOL_FLAGS_GPU_UNCACHED 0x10u
#define NVN_MEMORY_POOL_FLAGS_GPU_CACHED 0x20u
#define NVN_MEMORY_POOL_FLAGS_SHADER_CODE 0x40u
#define NVN_MEMORY_POOL_FLAGS_COMPRESSIBLE 0x80u
#define NVN_MEMORY_POOL_FLAGS_PHYSICAL 0x100u
#define NVN_MEMORY_POOL_FLAGS_VIRTUAL 0x200u

typedef struct nvn_memory_pool_family_entry {
    const char *name;
    void *fn;
} nvn_memory_pool_family_entry;

size_t nvn_memory_pool_family_count(void);
const nvn_memory_pool_family_entry *nvn_memory_pool_family_entry_at(size_t index);

/* Statistics for the contract. */
typedef struct nvn_memory_pool_stats {
    uint64_t builder_calls;
    uint64_t initialized;
    uint64_t finalized;
    uint64_t refused;
    uint64_t storage_recorded;
    uint64_t storage_bytes;
    uint64_t maps;
    uint64_t flushes;
    uint64_t invalidates;
    uint64_t map_virtual_refused;
    int live_pools;
    int mapped_pools;
} nvn_memory_pool_stats;

void nvn_memory_pool_get_stats(nvn_memory_pool_stats *out);

/* The host address behind a GPU address, and how many bytes are readable from
 * it. The address belongs to a pool the guest created; an address that belongs
 * to no pool has no host address and the call says so by returning NULL. */
void *nvn_memory_pool_resolve_address(uint64_t address, size_t *available);

#endif /* FFRS_NVN_MEMORY_POOL_H */
