/* The NVN sampler pool family of the FFRS RG477M native runtime.
 *
 * A sampler pool is a slice of a memory pool that holds the samplers the
 * guest draws with. The guest creates one right after the memory pool it
 * belongs to, so this family sits directly on top of the memory pool family:
 * the pool is given the memory pool, the offset inside it and the size of the
 * slice, and the samplers are registered into it by index.
 *
 * As with the queue, the window and the memory pool family, the state lives
 * on the host and is keyed by the guest's addresses. Nothing is written into
 * the guest's pool object, because the layout of an NVN sampler pool is a
 * property of the Nintendo SDK, which this runtime does not have.
 */
#ifndef FFRS_NVN_SAMPLER_POOL_H
#define FFRS_NVN_SAMPLER_POOL_H

#include <stddef.h>
#include <stdint.h>

/* The guest registers its samplers by index. A pool holds a handful; anything
 * beyond this is reported instead of silently dropped. */
#define NVN_SAMPLER_POOL_MAX_SAMPLERS 64

typedef struct nvn_sampler_pool_family_entry {
    const char *name;
    void *fn;
} nvn_sampler_pool_family_entry;

size_t nvn_sampler_pool_family_count(void);
const nvn_sampler_pool_family_entry *nvn_sampler_pool_family_entry_at(size_t index);

/* Statistics for the contract. */
typedef struct nvn_sampler_pool_stats {
    uint64_t builder_calls;
    uint64_t initialized;
    uint64_t finalized;
    uint64_t refused;
    uint64_t samplers_registered;
    uint64_t sampler_builders_registered;
    int live_pools;
} nvn_sampler_pool_stats;

void nvn_sampler_pool_get_stats(nvn_sampler_pool_stats *out);

/* The numbering base of a pool.
 *
 * A sampler handle holds the index of the sampler inside the pool in the bits
 * above the texture number, and the numbering base of the pool that is bound
 * is added to it when the handle is read. The base is given to a pool at its
 * first Initialize and does not change afterwards; the samplers have a
 * numbering of their own, apart from the numbering of the textures. */
int nvn_sampler_pool_base(const void *pool);

/* Whether the pool has been given a numbering base of its own. The base of
 * the first pool is zero, so the base alone cannot tell a first pool and a
 * pool that was never initialized apart. */
int nvn_sampler_pool_known(const void *pool);

/* The sampler that is registered at an index of the pool, or NULL when the
 * index holds no sampler. */
const void *nvn_sampler_pool_sampler_at(const void *pool, int index);

#endif /* FFRS_NVN_SAMPLER_POOL_H */
