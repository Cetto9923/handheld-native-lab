/* The NVN sampler pool family implementation.
 *
 * See nvn_sampler_pool.h for why the state lives on the host.
 *
 * The pool is a slice of a memory pool. The guest hands over the memory pool,
 * the offset inside it and the number of samplers the slice can hold, so the
 * pool records those three and answers them back. The samplers themselves are
 * registered by index; the runtime keeps the index to sampler handle mapping
 * on the host, because the guest's sampler objects are never read here.
 */
#include "nvn_sampler_pool.h"

#include "nvn_device.h"
#include "nvn_memory_pool.h"

#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    void *pool;
    void *memory_pool;
    ptrdiff_t memory_offset;
    int size;
    int initialized;
    /* The registered samplers, by index. A NULL entry means the index has
     * not been registered yet. */
    void *samplers[NVN_SAMPLER_POOL_MAX_SAMPLERS];
    char debug_label[64];
    int in_use;
} nvn_sampler_pool_state;

static pthread_mutex_t s_sampler_pool_lock = PTHREAD_MUTEX_INITIALIZER;
static nvn_sampler_pool_state s_pools[8];
static nvn_sampler_pool_stats s_stats;

static nvn_sampler_pool_state *pool_find(void *pool, int create) {
    size_t i;
    nvn_sampler_pool_state *free_slot = NULL;
    if (!pool) return NULL;
    for (i = 0; i < sizeof(s_pools) / sizeof(s_pools[0]); i++) {
        if (s_pools[i].in_use) {
            if (s_pools[i].pool == pool) return &s_pools[i];
        } else if (!free_slot) {
            free_slot = &s_pools[i];
        }
    }
    if (!create || !free_slot) return NULL;
    memset(free_slot, 0, sizeof(*free_slot));
    free_slot->pool = pool;
    free_slot->in_use = 1;
    return free_slot;
}

/* nvnSamplerPoolInitialize(pool, memoryPool, offset, size).
 *
 * The pool needs a memory pool it can take its storage from. The offset and
 * the size are recorded exactly as the guest gave them and answered back
 * unchanged, because the guest uses them to address the samplers inside the
 * memory pool.
 *
 * A size of zero is not an error: it is a pool that holds no sampler yet, and
 * the guest is free to register into it later. The device run showed the guest
 * passing zero, so refusing that would have turned a value it chose into a
 * failure it never asked for. Only a missing memory pool is refused, because
 * a sampler pool without memory behind it addresses nothing. */
int nvn_sampler_pool_initialize(void *pool, const void *memory_pool, ptrdiff_t offset, int size) {
    nvn_sampler_pool_state *p;
    int ready = 0;

    pthread_mutex_lock(&s_sampler_pool_lock);
    s_stats.builder_calls++;
    if (!memory_pool) {
        s_stats.refused++;
        pthread_mutex_unlock(&s_sampler_pool_lock);
        fprintf(stderr, "[nvn] nvnSamplerPoolInitialize: no memory pool for %p; a sampler pool is a slice of a "
                        "memory pool, so it cannot exist without one\n",
                pool);
        return 0;
    }
    p = pool_find(pool, 1);
    if (!p) {
        s_stats.refused++;
        pthread_mutex_unlock(&s_sampler_pool_lock);
        fprintf(stderr, "[nvn] nvnSamplerPoolInitialize: no room for another sampler pool\n");
        return 0;
    }
    p->memory_pool = (void *)(uintptr_t)memory_pool;
    p->memory_offset = offset;
    p->size = size;
    p->initialized = 1;
    memset(p->samplers, 0, sizeof(p->samplers));
    s_stats.initialized++;
    ready = 1;
    pthread_mutex_unlock(&s_sampler_pool_lock);

    fprintf(stderr, "[nvn] nvnSamplerPoolInitialize: pool=%p memory_pool=%p offset=%" PRIdPTR " size=%d -> %s\n",
            pool, memory_pool, offset, size, ready ? "true" : "false");
    return 1;
}

void nvn_sampler_pool_set_debug_label(void *pool, const char *label) {
    nvn_sampler_pool_state *p;
    pthread_mutex_lock(&s_sampler_pool_lock);
    p = pool_find(pool, 1);
    if (p) snprintf(p->debug_label, sizeof(p->debug_label), "%s", label ? label : "");
    pthread_mutex_unlock(&s_sampler_pool_lock);
    NVN_VOID_RETURN();
}

void nvn_sampler_pool_finalize(void *pool) {
    nvn_sampler_pool_state *p;
    pthread_mutex_lock(&s_sampler_pool_lock);
    p = pool_find(pool, 0);
    if (p) {
        p->initialized = 0;
        p->memory_pool = NULL;
        p->memory_offset = 0;
        p->size = 0;
        memset(p->samplers, 0, sizeof(p->samplers));
        p->in_use = 0;
        s_stats.finalized++;
    }
    pthread_mutex_unlock(&s_sampler_pool_lock);
    fprintf(stderr, "[nvn] nvnSamplerPoolFinalize: pool=%p\n", pool);
    NVN_VOID_RETURN();
}

/* nvnSamplerPoolRegisterSampler(pool, index, sampler).
 *
 * The sampler's own state is not read here: it belongs to the sampler
 * family, which stores it against the sampler's address. What the pool has to
 * remember is which index holds which sampler, because the guest refers to
 * the samplers by index when it draws. */
void nvn_sampler_pool_register_sampler(const void *pool, int index, const void *sampler) {
    nvn_sampler_pool_state *p;
    pthread_mutex_lock(&s_sampler_pool_lock);
    p = pool_find((void *)(uintptr_t)pool, 0);
    if (p && p->initialized && index >= 0 && index < NVN_SAMPLER_POOL_MAX_SAMPLERS) {
        p->samplers[index] = (void *)(uintptr_t)sampler;
        s_stats.samplers_registered++;
    } else if (p && p->initialized) {
        fprintf(stderr, "[nvn] nvnSamplerPoolRegisterSampler: index=%d is outside the %d samplers this pool "
                        "tracks; the sampler was not registered\n",
                index, NVN_SAMPLER_POOL_MAX_SAMPLERS);
    }
    pthread_mutex_unlock(&s_sampler_pool_lock);
    NVN_VOID_RETURN();
}

/* The same registration from a builder: the sampler does not exist yet, so
 * what is recorded is the builder the guest is going to initialize. */
void nvn_sampler_pool_register_sampler_builder(const void *pool, int index, const void *builder) {
    nvn_sampler_pool_state *p;
    pthread_mutex_lock(&s_sampler_pool_lock);
    p = pool_find((void *)(uintptr_t)pool, 0);
    if (p && p->initialized && index >= 0 && index < NVN_SAMPLER_POOL_MAX_SAMPLERS) {
        p->samplers[index] = (void *)(uintptr_t)builder;
        s_stats.sampler_builders_registered++;
    } else if (p && p->initialized) {
        fprintf(stderr, "[nvn] nvnSamplerPoolRegisterSamplerBuilder: index=%d is outside the %d samplers this "
                        "pool tracks; the sampler builder was not registered\n",
                index, NVN_SAMPLER_POOL_MAX_SAMPLERS);
    }
    pthread_mutex_unlock(&s_sampler_pool_lock);
    NVN_VOID_RETURN();
}

void *nvn_sampler_pool_get_memory_pool(const void *pool) {
    nvn_sampler_pool_state *p;
    void *memory_pool = NULL;
    pthread_mutex_lock(&s_sampler_pool_lock);
    p = pool_find((void *)(uintptr_t)pool, 0);
    if (p && p->initialized) memory_pool = p->memory_pool;
    pthread_mutex_unlock(&s_sampler_pool_lock);
    return memory_pool;
}

ptrdiff_t nvn_sampler_pool_get_memory_offset(const void *pool) {
    nvn_sampler_pool_state *p;
    ptrdiff_t offset = 0;
    pthread_mutex_lock(&s_sampler_pool_lock);
    p = pool_find((void *)(uintptr_t)pool, 0);
    if (p && p->initialized) offset = p->memory_offset;
    pthread_mutex_unlock(&s_sampler_pool_lock);
    return offset;
}

int nvn_sampler_pool_get_size(const void *pool) {
    nvn_sampler_pool_state *p;
    int size = 0;
    pthread_mutex_lock(&s_sampler_pool_lock);
    p = pool_find((void *)(uintptr_t)pool, 0);
    if (p && p->initialized) size = p->size;
    pthread_mutex_unlock(&s_sampler_pool_lock);
    return size;
}

/* ------------------------------------------------------------------ */
/* The names                                                           */
/* ------------------------------------------------------------------ */

static const nvn_sampler_pool_family_entry s_sampler_pool_family[] = {
    { "nvnSamplerPoolInitialize", (void *)(uintptr_t)nvn_sampler_pool_initialize },
    { "nvnSamplerPoolSetDebugLabel", (void *)(uintptr_t)nvn_sampler_pool_set_debug_label },
    { "nvnSamplerPoolFinalize", (void *)(uintptr_t)nvn_sampler_pool_finalize },
    { "nvnSamplerPoolRegisterSampler", (void *)(uintptr_t)nvn_sampler_pool_register_sampler },
    { "nvnSamplerPoolRegisterSamplerBuilder",
      (void *)(uintptr_t)nvn_sampler_pool_register_sampler_builder },
    { "nvnSamplerPoolGetMemoryPool", (void *)(uintptr_t)nvn_sampler_pool_get_memory_pool },
    { "nvnSamplerPoolGetMemoryOffset", (void *)(uintptr_t)nvn_sampler_pool_get_memory_offset },
    { "nvnSamplerPoolGetSize", (void *)(uintptr_t)nvn_sampler_pool_get_size },
};

size_t nvn_sampler_pool_family_count(void) {
    return sizeof(s_sampler_pool_family) / sizeof(s_sampler_pool_family[0]);
}

const nvn_sampler_pool_family_entry *nvn_sampler_pool_family_entry_at(size_t index) {
    if (index >= nvn_sampler_pool_family_count()) return NULL;
    return &s_sampler_pool_family[index];
}

void nvn_sampler_pool_get_stats(nvn_sampler_pool_stats *out) {
    size_t i;
    if (!out) return;
    pthread_mutex_lock(&s_sampler_pool_lock);
    *out = s_stats;
    out->live_pools = 0;
    for (i = 0; i < sizeof(s_pools) / sizeof(s_pools[0]); i++) {
        if (s_pools[i].in_use && s_pools[i].initialized) out->live_pools++;
    }
    pthread_mutex_unlock(&s_sampler_pool_lock);
}
