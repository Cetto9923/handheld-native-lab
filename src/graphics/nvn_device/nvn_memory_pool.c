/* The NVN memory pool family implementation.
 *
 * See nvn_memory_pool.h for why the state lives on the host.
 *
 * A memory pool is the guest's own block of memory, handed to NVN so that the
 * graphics device can use it. The guest allocates that memory itself and
 * passes the range to nvnMemoryPoolBuilderSetStorage, so this layer records
 * the range and uses it as the CPU mapping. It never allocates a second copy:
 * a shadow allocation would hand the guest a different address than the one
 * it writes to, and the failure would then show up somewhere far away.
 *
 * The pool may only exist when a real device exists. A pool without a device
 * would be memory that the graphics device cannot use, and the guest would
 * find that out much later, at a place where the cause is no longer visible.
 */
#include "nvn_memory_pool.h"
#include "nvn_engine.h"
extern void nvn_command_buffer_retire_source_memory(void *,size_t) __attribute__((weak));
#include "nvn_obj_ffrs.h"

#include "nvn_device.h"

#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    void *builder;
    void *device;
    void *storage;
    uint64_t size;
    uint32_t flags;
    int in_use;
} nvn_memory_pool_builder_state;

typedef struct {
    void *pool;
    void *device;
    void *storage;
    uint64_t size;
    uint32_t flags;
    uint64_t buffer_address;
    uint64_t generation;
    int initialized;
    int mapped;
    char debug_label[64];
    int in_use;
} nvn_memory_pool_state;

static pthread_mutex_t s_pool_lock = PTHREAD_MUTEX_INITIALIZER;
static int s_synthetic_address;
static nvn_memory_pool_builder_state s_builders[NVN_MEMORY_POOL_MAX_BUILDERS];
/* This counter belongs to the process, not a reusable table slot. No reset:
 * old recorded commands can still hold a bridge snapshot after Finalize. */
static uint64_t s_generation;
static nvn_memory_pool_state s_pools[NVN_MEMORY_POOL_MAX_POOLS];
static nvn_memory_pool_stats s_stats;

/* The builder state for one guest builder address. A builder that was never
 * prepared has no state, and the functions that need one say so instead of
 * inventing a default. */
static nvn_memory_pool_builder_state *builder_find(void *builder, int create) {
    size_t i;
    nvn_memory_pool_builder_state *free_slot = NULL;
    if (!builder) return NULL;
    for (i = 0; i < NVN_MEMORY_POOL_MAX_BUILDERS; i++) {
        if (s_builders[i].in_use) {
            if (s_builders[i].builder == builder) return &s_builders[i];
        } else if (!free_slot) {
            free_slot = &s_builders[i];
        }
    }
    if (!create || !free_slot) return NULL;
    memset(free_slot, 0, sizeof(*free_slot));
    free_slot->builder = builder;
    free_slot->in_use = 1;
    return free_slot;
}

static nvn_memory_pool_state *pool_find(void *pool, int create) {
    size_t i;
    nvn_memory_pool_state *free_slot = NULL;
    if (!pool) return NULL;
    for (i = 0; i < NVN_MEMORY_POOL_MAX_POOLS; i++) {
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

/* ------------------------------------------------------------------ */
/* The builder                                                         */
/* ------------------------------------------------------------------ */

void nvn_memory_pool_builder_set_defaults(void *builder) {
    nvn_memory_pool_builder_state *b;
    pthread_mutex_lock(&s_pool_lock);
    s_stats.builder_calls++;
    b = builder_find(builder, 1);
    if (b) {
        /* The defaults are this runtime's own: no device, no storage and no
         * flags until the guest asks for them. */
        b->device = NULL;
        b->storage = NULL;
        b->size = 0;
        b->flags = 0;
    }
    pthread_mutex_unlock(&s_pool_lock);
    if (!b) fprintf(stderr, "[nvn] nvnMemoryPoolBuilderSetDefaults: no room for another builder (%p)\n", builder);
    NVN_VOID_RETURN();
}

void nvn_memory_pool_builder_set_device(void *builder, void *device) {
    nvn_memory_pool_builder_state *b;
    pthread_mutex_lock(&s_pool_lock);
    s_stats.builder_calls++;
    b = builder_find(builder, 1);
    if (b) b->device = device;
    pthread_mutex_unlock(&s_pool_lock);
    NVN_VOID_RETURN();
}

/* The storage is memory the guest already owns. It is recorded, not copied:
 * the guest writes into this range, so this range has to stay the one it
 * handed over. */
void nvn_memory_pool_builder_set_storage(void *builder, void *storage, uint64_t size) {
    nvn_memory_pool_builder_state *b;
    pthread_mutex_lock(&s_pool_lock);
    s_stats.builder_calls++;
    b = builder_find(builder, 1);
    if (b) {
        b->storage = storage;
        b->size = size;
        s_stats.storage_recorded++;
        s_stats.storage_bytes += size;
    }
    pthread_mutex_unlock(&s_pool_lock);
    NVN_VOID_RETURN();
}

void nvn_memory_pool_builder_set_flags(void *builder, int flags) {
    nvn_memory_pool_builder_state *b;
    pthread_mutex_lock(&s_pool_lock);
    s_stats.builder_calls++;
    b = builder_find(builder, 1);
    if (b) b->flags = (uint32_t)flags;
    pthread_mutex_unlock(&s_pool_lock);
    NVN_VOID_RETURN();
}

void *nvn_memory_pool_builder_get_device(const void *builder) {
    nvn_memory_pool_builder_state *b;
    void *device = NULL;
    pthread_mutex_lock(&s_pool_lock);
    s_stats.builder_calls++;
    b = builder_find((void *)(uintptr_t)builder, 0);
    if (b) device = b->device;
    pthread_mutex_unlock(&s_pool_lock);
    return device;
}

/* The vendored header writes this one as returning void, which cannot be
 * right: the guest asks the builder for the memory it stored, and the only
 * value worth returning is that pointer. It is implemented as returning the
 * pointer, and the runtime log records what it returned. */
void *nvn_memory_pool_builder_get_memory(const void *builder) {
    nvn_memory_pool_builder_state *b;
    void *storage = NULL;
    pthread_mutex_lock(&s_pool_lock);
    s_stats.builder_calls++;
    b = builder_find((void *)(uintptr_t)builder, 0);
    if (b) storage = b->storage;
    pthread_mutex_unlock(&s_pool_lock);
    return storage;
}

uint64_t nvn_memory_pool_builder_get_size(const void *builder) {
    nvn_memory_pool_builder_state *b;
    uint64_t size = 0;
    pthread_mutex_lock(&s_pool_lock);
    s_stats.builder_calls++;
    b = builder_find((void *)(uintptr_t)builder, 0);
    if (b) size = b->size;
    pthread_mutex_unlock(&s_pool_lock);
    return size;
}

uint32_t nvn_memory_pool_builder_get_flags(const void *builder) {
    nvn_memory_pool_builder_state *b;
    uint32_t flags = 0;
    pthread_mutex_lock(&s_pool_lock);
    s_stats.builder_calls++;
    b = builder_find((void *)(uintptr_t)builder, 0);
    if (b) flags = b->flags;
    pthread_mutex_unlock(&s_pool_lock);
    return flags;
}

/* ------------------------------------------------------------------ */
/* The pool                                                            */
/* ------------------------------------------------------------------ */

int nvn_memory_pool_initialize(void *pool, const void *builder) {
    nvn_memory_pool_builder_state *b;
    nvn_memory_pool_state *p;
    uint64_t buffer_address = 0;
    int ready = 0;
    void *storage = NULL, *device = NULL;
    uint64_t bytes = 0;
    uint32_t flags = 0;

    pthread_mutex_lock(&s_pool_lock);
    s_stats.builder_calls++;
    b = builder_find((void *)(uintptr_t)builder, 0);
    if (!b) {
        s_stats.refused++;
        pthread_mutex_unlock(&s_pool_lock);
        fprintf(stderr, "[nvn] nvnMemoryPoolInitialize: no builder state for %p; the guest did not prepare one\n",
                builder);
        return 0;
    }
    /* A pool without a device is memory that no graphics device can use. */
    if (!nvn_device_is_ready(b->device)) {
        s_stats.refused++;
        pthread_mutex_unlock(&s_pool_lock);
        fprintf(stderr, "[nvn] nvnMemoryPoolInitialize: the device in the builder (%p) is not an initialized device\n",
                b->device);
        return 0;
    }
    /* A pool without storage has no memory behind it. The guest is expected
     * to hand over the range it allocated; a runtime that invented one here
     * would give the guest a buffer it never asked for. */
    if (!b->storage || !b->size) {
        s_stats.refused++;
        pthread_mutex_unlock(&s_pool_lock);
        fprintf(stderr, "[nvn] nvnMemoryPoolInitialize: the builder has no storage (storage=%p size=%" PRIu64
                        "); this runtime does not allocate a pool behind the guest's back\n",
                b->storage, b->size);
        return 0;
    }
    if (s_generation == UINT64_MAX) {
        s_stats.refused++;
        pthread_mutex_unlock(&s_pool_lock);
        fputs("[nvn] nvnMemoryPoolInitialize: generation exhausted; refusing identity reuse\n", stderr);
        return 0;
    }
    p = pool_find(pool, 1);
    if (!p) {
        s_stats.refused++;
        pthread_mutex_unlock(&s_pool_lock);
        fprintf(stderr, "[nvn] nvnMemoryPoolInitialize: no room for another pool\n");
        return 0;
    }
    p->device = b->device;
    p->storage = b->storage;
    p->size = b->size;
    p->flags = b->flags;
    p->generation = ++s_generation;
    p->initialized = 1;
    p->mapped = 0;
    /* The buffer address is the address of the memory the guest gave the
     * pool. The reference host answers the same (hle_nvn.c:222), and the
     * convention behind it is that a GPU address is the CPU address of the
     * same memory: the guest hands this address back for every access to
     * the memory, and the graphics device has to be able to read and write
     * it. An address of this runtime's own making would name memory that
     * does not exist. */
    if (s_synthetic_address) {
        buffer_address = 0x6d656d0000000000ull | ((uint64_t)(uintptr_t)pool & 0xffffffffull);
    } else {
        buffer_address = (uint64_t)(uintptr_t)b->storage;
    }
    if (!buffer_address) buffer_address = 1;
    p->buffer_address = buffer_address;
    s_stats.initialized++;
    ready = 1;
    storage = p->storage; bytes = p->size; flags = p->flags; device = p->device;
    pthread_mutex_unlock(&s_pool_lock);

    /* The engine resolves the addresses of the guest through the pools it
     * knows, so the memory of this pool has to be told to it. */
    if (ready) nvn_engine_pool_add(storage, bytes);

    static unsigned s_pool_init_log_count = 0;
    if (s_pool_init_log_count++ < 20) {
        fprintf(stderr, "[nvn] nvnMemoryPoolInitialize: pool=%p builder=%p device=%p storage=%p size=%" PRIu64
                        " flags=0x%" PRIx32 " -> %s\n",
                pool, builder, device, storage, bytes, flags, ready ? "true" : "false");
    }
    return 1;
}

void nvn_memory_pool_set_debug_label(void *pool, const char *label) {
    nvn_memory_pool_state *p;
    pthread_mutex_lock(&s_pool_lock);
    p = pool_find(pool, 1);
    if (p) snprintf(p->debug_label, sizeof(p->debug_label), "%s", label ? label : "");
    pthread_mutex_unlock(&s_pool_lock);
    NVN_VOID_RETURN();
}

void nvn_memory_pool_finalize(void *pool) {
    nvn_memory_pool_state *p;
    void *storage=NULL;size_t storage_bytes=0;
    pthread_mutex_lock(&s_pool_lock);
    p = pool_find(pool, 0);
    if (p) {
        storage=p->storage;storage_bytes=(size_t)p->size;
        p->initialized = 0;
        p->mapped = 0;
        p->storage = NULL;
        p->size = 0;
        p->buffer_address = 0;
        p->in_use = 0;
        s_stats.finalized++;
    }
    pthread_mutex_unlock(&s_pool_lock);
    /* Storage remains guest-owned until this API returns. Retire source
     * handles and engine registrations outside the pool-table lock. Never
     * retain a table interior pointer across either callback. */
    if(storage&&storage_bytes){
        if(nvn_command_buffer_retire_source_memory)nvn_command_buffer_retire_source_memory(storage,storage_bytes);
        else if(nvn_engine_ready()){fputs("[nvn] live engine missing command-source retirement provider\n",stderr);abort();}
        nvn_engine_pool_del(storage);
    }
    static unsigned s_pool_fin_log_count = 0;
    if (s_pool_fin_log_count++ < 20) {
        fprintf(stderr, "[nvn] nvnMemoryPoolFinalize: pool=%p\n", pool);
    }
    NVN_VOID_RETURN();
}

/* The CPU mapping. The storage the guest handed over is already host
 * memory, so the mapping is that address; there is nothing to map again. */
void *nvn_memory_pool_map(const void *pool) {
    nvn_memory_pool_state *p;
    void *mapped = NULL;
    pthread_mutex_lock(&s_pool_lock);
    p = pool_find((void *)(uintptr_t)pool, 0);
    if (p && p->initialized && p->storage) {
        mapped = p->storage;
        if (!p->mapped) {
            p->mapped = 1;
            s_stats.maps++;
        }
    }
    pthread_mutex_unlock(&s_pool_lock);
    return mapped;
}

/* The flush and the invalidate.
 *
 * The storage belongs to the guest and the guest writes into it with its own
 * CPU instructions, so there is no host side cache to push out or to drop:
 * the memory is already coherent. The calls are answered as done and the log
 * says why, because pretending that a cache operation happened when none was
 * needed would hide a real problem later. */
void nvn_memory_pool_flush_mapped_range(const void *pool, ptrdiff_t offset, size_t size) {
    nvn_memory_pool_state *p;
    int known = 0;
    pthread_mutex_lock(&s_pool_lock);
    p = pool_find((void *)(uintptr_t)pool, 0);
    known = p != NULL && p->initialized;
    pthread_mutex_unlock(&s_pool_lock);
    s_stats.flushes++;
    fprintf(stderr, "[nvn] nvnMemoryPoolFlushMappedRange: pool=%p offset=%" PRIdPTR " size=%zu initialized=%d; "
                    "the storage is host memory the guest writes itself, so it is already coherent\n",
            pool, offset, size, known);
    NVN_VOID_RETURN();
}

void nvn_memory_pool_invalidate_mapped_range(const void *pool, ptrdiff_t offset, size_t size) {
    nvn_memory_pool_state *p;
    int known = 0;
    pthread_mutex_lock(&s_pool_lock);
    p = pool_find((void *)(uintptr_t)pool, 0);
    known = p != NULL && p->initialized;
    pthread_mutex_unlock(&s_pool_lock);
    s_stats.invalidates++;
    fprintf(stderr, "[nvn] nvnMemoryPoolInvalidateMappedRange: pool=%p offset=%" PRIdPTR " size=%zu initialized=%d; "
                    "the storage is host memory the guest writes itself, so there is no stale cache to drop\n",
            pool, offset, size, known);
    NVN_VOID_RETURN();
}

/* The GPU visible address of the pool. It is stable for the lifetime of the
 * pool, which is what the guest needs when it hands the address back to the
 * device for an access. */
void nvn_memory_pool_set_synthetic_address(int synthetic) {
    pthread_mutex_lock(&s_pool_lock);
    s_synthetic_address = synthetic ? 1 : 0;
    pthread_mutex_unlock(&s_pool_lock);
}

uint64_t nvn_memory_pool_get_buffer_address(const void *pool) {
    nvn_memory_pool_state *p;
    uint64_t address = 0;
    pthread_mutex_lock(&s_pool_lock);
    p = pool_find((void *)(uintptr_t)pool, 0);
    if (p && p->initialized) address = p->buffer_address;
    pthread_mutex_unlock(&s_pool_lock);
    return address;
}

/* The virtual mapping. This runtime does not build a page table for the
 * guest, so the call is refused and named instead of being answered with an
 * address that does not exist. */
int nvn_memory_pool_map_virtual(void *pool, int flags, const void *request) {
    (void)flags;
    (void)request;
    pthread_mutex_lock(&s_pool_lock);
    s_stats.map_virtual_refused++;
    pthread_mutex_unlock(&s_pool_lock);
    fprintf(stderr, "[nvn] nvnMemoryPoolMapVirtual: pool=%p; this runtime does not build a virtual page table for "
                    "the guest, so the mapping is refused rather than faked\n",
            pool);
    return 0;
}

uint64_t nvn_memory_pool_get_size(const void *pool) {
    nvn_memory_pool_state *p;
    uint64_t size = 0;
    pthread_mutex_lock(&s_pool_lock);
    p = pool_find((void *)(uintptr_t)pool, 0);
    if (p && p->initialized) size = p->size;
    pthread_mutex_unlock(&s_pool_lock);
    return size;
}

uint32_t nvn_memory_pool_get_flags(const void *pool) {
    nvn_memory_pool_state *p;
    uint32_t flags = 0;
    pthread_mutex_lock(&s_pool_lock);
    p = pool_find((void *)(uintptr_t)pool, 0);
    if (p && p->initialized) flags = p->flags;
    pthread_mutex_unlock(&s_pool_lock);
    return flags;
}

/* The host address behind a GPU address.
 *
 * The guest hands the device buffer addresses, never host pointers. To read
 * the code area of a shader the runtime has to find the pool the address
 * belongs to and turn it back into the host address of the same bytes. The
 * pools are few and their ranges do not overlap, so the search is a walk over
 * the table.
 *
 * The number of bytes that are readable from the returned address is reported
 * as well, because the caller has to know how much of the area it may touch. */
void *nvn_memory_pool_resolve_address(uint64_t address, size_t *available) {
    size_t i;
    void *resolved = NULL;
    if (available) *available = 0;
    pthread_mutex_lock(&s_pool_lock);
    for (i = 0; i < NVN_MEMORY_POOL_MAX_POOLS; i++) {
        nvn_memory_pool_state *p = &s_pools[i];
        uint64_t base;
        uint64_t offset;
        if (!p->in_use || !p->initialized || !p->storage) continue;
        /* The address the pool hands out. It is derived from the pool's own
         * identity, so it is a place the guest can keep its own bookkeeping
         * in; it is not the address of the bytes. */
        base = p->buffer_address;
        if (address >= base && address - base < p->size) {
            offset = address - base;
            resolved = (void *)(uintptr_t)((unsigned char *)p->storage + offset);
            if (available) *available = (size_t)(p->size - offset);
            break;
        }
        /* The address the guest really uses. The guest allocated the storage
         * itself and addresses the pool with the host pointer, so the address
         * that reaches the device is inside the storage. The guest and the
         * runtime share the address space, so that pointer names the same
         * bytes the guest wrote. */
        base = (uint64_t)(uintptr_t)p->storage;
        if (address >= base && address - base < p->size) {
            offset = address - base;
            resolved = (void *)(uintptr_t)((unsigned char *)p->storage + offset);
            if (available) *available = (size_t)(p->size - offset);
            break;
        }
    }
    pthread_mutex_unlock(&s_pool_lock);
    return resolved;
}

/* ------------------------------------------------------------------ */
/* The names                                                           */
/* ------------------------------------------------------------------ */

static const nvn_memory_pool_family_entry s_memory_pool_family[] = {
    { "nvnMemoryPoolBuilderSetDevice", (void *)(uintptr_t)nvn_memory_pool_builder_set_device },
    { "nvnMemoryPoolBuilderSetDefaults", (void *)(uintptr_t)nvn_memory_pool_builder_set_defaults },
    { "nvnMemoryPoolBuilderSetStorage", (void *)(uintptr_t)nvn_memory_pool_builder_set_storage },
    { "nvnMemoryPoolBuilderSetFlags", (void *)(uintptr_t)nvn_memory_pool_builder_set_flags },
    { "nvnMemoryPoolBuilderGetDevice", (void *)(uintptr_t)nvn_memory_pool_builder_get_device },
    { "nvnMemoryPoolBuilderGetMemory", (void *)(uintptr_t)nvn_memory_pool_builder_get_memory },
    { "nvnMemoryPoolBuilderGetSize", (void *)(uintptr_t)nvn_memory_pool_builder_get_size },
    { "nvnMemoryPoolBuilderGetFlags", (void *)(uintptr_t)nvn_memory_pool_builder_get_flags },
    { "nvnMemoryPoolInitialize", (void *)(uintptr_t)nvn_memory_pool_initialize },
    { "nvnMemoryPoolSetDebugLabel", (void *)(uintptr_t)nvn_memory_pool_set_debug_label },
    { "nvnMemoryPoolFinalize", (void *)(uintptr_t)nvn_memory_pool_finalize },
    { "nvnMemoryPoolMap", (void *)(uintptr_t)nvn_memory_pool_map },
    { "nvnMemoryPoolFlushMappedRange", (void *)(uintptr_t)nvn_memory_pool_flush_mapped_range },
    { "nvnMemoryPoolInvalidateMappedRange", (void *)(uintptr_t)nvn_memory_pool_invalidate_mapped_range },
    { "nvnMemoryPoolGetBufferAddress", (void *)(uintptr_t)nvn_memory_pool_get_buffer_address },
    { "nvnMemoryPoolMapVirtual", (void *)(uintptr_t)nvn_memory_pool_map_virtual },
    { "nvnMemoryPoolGetSize", (void *)(uintptr_t)nvn_memory_pool_get_size },
    { "nvnMemoryPoolGetFlags", (void *)(uintptr_t)nvn_memory_pool_get_flags },
};

size_t nvn_memory_pool_family_count(void) {
    return sizeof(s_memory_pool_family) / sizeof(s_memory_pool_family[0]);
}

const nvn_memory_pool_family_entry *nvn_memory_pool_family_entry_at(size_t index) {
    if (index >= nvn_memory_pool_family_count()) return NULL;
    return &s_memory_pool_family[index];
}

void nvn_memory_pool_get_stats(nvn_memory_pool_stats *out) {
    size_t i;
    if (!out) return;
    pthread_mutex_lock(&s_pool_lock);
    *out = s_stats;
    out->live_pools = 0;
    out->mapped_pools = 0;
    for (i = 0; i < NVN_MEMORY_POOL_MAX_POOLS; i++) {
        if (s_pools[i].in_use && s_pools[i].initialized) {
            out->live_pools++;
            if (s_pools[i].mapped) out->mapped_pools++;
        }
    }
    pthread_mutex_unlock(&s_pool_lock);
}

/* What this family knows about one memory pool: the memory the guest gave
 * it, its size and its flags.
 *
 * The GPU address of a range of a pool is the address of that range in the
 * guest's own memory (the reference calls this the convention "GPU address =
 * CPU address", dm/gfx/nvn/nvn_metal_mapping.md:9), so the storage address
 * is what the pool holds. */
int nvn_memory_pool_info_of(const void *pool, nvn_memory_pool_info *out) {
    nvn_memory_pool_state *slot;
    if (!pool || !out) return 0;
    pthread_mutex_lock(&s_pool_lock);
    slot = pool_find((void *)(uintptr_t)pool, 0);
    if (!slot || !slot->initialized) {
        pthread_mutex_unlock(&s_pool_lock);
        return 0;
    }
    out->generation = slot->generation;
    out->mem = (uint8_t *)slot->storage;
    out->size = slot->size;
    out->flags = slot->flags;
    pthread_mutex_unlock(&s_pool_lock);
    return 1;
}
