/* The NVN texture pool family of the FFRS RG477M native runtime.
 *
 * A texture pool is a slice of a memory pool that holds the textures the
 * guest draws with. It sits directly on top of the memory pool family, the
 * same way the sampler pool does: the pool is given the memory pool, the
 * offset inside it and the size of the slice, and the textures are registered
 * into it by index.
 *
 * As with the queue, the window, the memory pool and the sampler pool
 * family, the state lives on the host and is keyed by the guest's addresses.
 * Nothing is written into the guest's pool object, because the layout of an
 * NVN texture pool is a property of the Nintendo SDK, which this runtime does
 * not have.
 */
#ifndef FFRS_NVN_TEXTURE_POOL_H
#define FFRS_NVN_TEXTURE_POOL_H

#include <stddef.h>
#include <stdint.h>

/* The guest registers its textures by index. A pool holds a handful; anything
 * beyond this is reported instead of silently dropped. */
#define NVN_TEXTURE_POOL_MAX_TEXTURES 256

typedef struct nvn_texture_pool_family_entry {
    const char *name;
    void *fn;
} nvn_texture_pool_family_entry;

size_t nvn_texture_pool_family_count(void);
const nvn_texture_pool_family_entry *nvn_texture_pool_family_entry_at(size_t index);

/* Statistics for the contract. */
typedef struct nvn_texture_pool_stats {
    uint64_t builder_calls;
    uint64_t initialized;
    uint64_t finalized;
    uint64_t refused;
    uint64_t textures_registered;
    uint64_t images_registered;
    int live_pools;
} nvn_texture_pool_stats;

void nvn_texture_pool_get_stats(nvn_texture_pool_stats *out);

#endif /* FFRS_NVN_TEXTURE_POOL_H */
