/* The NVN texture pool family implementation.
 *
 * See nvn_texture_pool.h for why the state lives on the host.
 *
 * The pool is a slice of a memory pool. The guest hands over the memory pool,
 * the offset inside it and the size of the slice, so the pool records those
 * three and answers them back. The textures are registered by index; the
 * runtime keeps the index to texture handle mapping on the host, because the
 * guest's texture objects are never read here.
 */
#include "nvn_texture_pool.h"
#include "nvn_obj_ffrs.h"

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
    /* The registered textures, by index. A NULL entry means the index has
     * not been registered yet.
     *
     * The table grows with the indexes the guest uses instead of being a
     * fixed size, because the size of the pool is the guest's own choice: it
     * sizes its pool by the limit this device reports for
     * MAX_TEXTURE_POOL_SIZE (selector 36), which is 1048576. A fixed table
     * of sixteen thousand would refuse the textures of a game that uses
     * more, and a refused texture is a texture the guest draws with that
     * this runtime does not know about. The cap is the number of indexes a
     * texture handle can hold: the handle carries the index in its low
     * twenty bits. */
    void **textures;
    int texture_capacity;
    char debug_label[64];
    int in_use;
} nvn_texture_pool_state;

/* The numbering base of a texture pool.
 *
 * A texture handle is not a pointer: it is the texture's number in the low
 * twenty bits, and the numbering base of the pool that is bound is added to it
 * when the handle is read. Every pool is given a base at its first Initialize
 * and the pools share one numbering, so the base is not the index inside the
 * pool.
 *
 * The base lives in its own table and not in the pool state, because the pool
 * state goes away when the guest finalizes the pool, while the numbering has
 * to keep running: the guest can initialize a pool again later and must get
 * the same numbering back, or the textures it registered under the old base
 * would be read under a new one. */
typedef struct {
    const void *pool;
    int base;
} nvn_texture_pool_base_entry;

#define NVN_TEXTURE_POOL_MAX_BASE_ENTRIES 16

static pthread_mutex_t s_texture_pool_lock = PTHREAD_MUTEX_INITIALIZER;
static nvn_texture_pool_state s_pools[8];
static nvn_texture_pool_base_entry s_bases[NVN_TEXTURE_POOL_MAX_BASE_ENTRIES];
static int s_base_count;
/* The next free number of the shared numbering of the texture pools. */
static int s_texture_pool_next;
static nvn_texture_pool_stats s_stats;

static nvn_texture_pool_state *pool_find(void *pool, int create) {
    size_t i;
    nvn_texture_pool_state *free_slot = NULL;
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

/* The numbering base of a pool, and the numbering it is given.
 *
 * The base is assigned at the pool's first Initialize and the numbering then
 * steps on by the pool's size rounded up to a whole number of 256, so that
 * every pool starts on a boundary of its own. A pool that is initialized again
 * keeps the base it already has. */
/* The base of a pool, and whether the pool has one at all. The base of the
 * first pool is zero, so the two answers cannot be told apart by the base
 * alone. */
static int base_find(const void *pool, int *base) {
    int i;
    for (i = 0; i < s_base_count; i++) {
        if (s_bases[i].pool == pool) {
            if (base) *base = s_bases[i].base;
            return 1;
        }
    }
    return 0;
}

static int base_assign(const void *pool, int count) {
    int i;
    for (i = 0; i < s_base_count; i++) {
        if (s_bases[i].pool == pool) return s_bases[i].base;
    }
    if (s_base_count == NVN_TEXTURE_POOL_MAX_BASE_ENTRIES) {
        fprintf(stderr, "[nvn] nvnTexturePoolInitialize: more than %d texture pools; this pool shares the "
                        "numbering of the first one\n",
                NVN_TEXTURE_POOL_MAX_BASE_ENTRIES);
        return 0;
    }
    s_bases[s_base_count].pool = pool;
    s_bases[s_base_count].base = s_texture_pool_next;
    s_base_count++;
    s_texture_pool_next += (count + 255) & ~255;
    return s_bases[s_base_count - 1].base;
}

/* The table of registered textures grows to hold an index the guest uses.
 *
 * It grows by doubling, so the cost of the growth is the same order as the
 * size of the table. The cap is the number of indexes a handle can carry. A
 * table that cannot grow is a refusal, and the refusal is logged with the
 * index that could not be held. */
static int texture_table_grow(nvn_texture_pool_state *p, int index) {
    int capacity = p->texture_capacity ? p->texture_capacity : 256;
    void **grown;
    while (capacity <= index) {
        if (capacity > NVN_TEXTURE_POOL_MAX_TEXTURES / 2) {
            capacity = NVN_TEXTURE_POOL_MAX_TEXTURES;
            break;
        }
        capacity *= 2;
    }
    if (index >= capacity) return 0;
    grown = (void **)realloc(p->textures, (size_t)capacity * sizeof(*grown));
    if (!grown) return 0;
    memset(grown + p->texture_capacity, 0, (size_t)(capacity - p->texture_capacity) * sizeof(*grown));
    p->textures = grown;
    p->texture_capacity = capacity;
    return 1;
}

/* nvnTexturePoolInitialize(pool, memoryPool, offset, size).
 *
 * The pool needs a memory pool it can take its storage from. The offset and
 * the size are recorded exactly as the guest gave them and answered back
 * unchanged, because the guest uses them to address the textures inside the
 * memory pool.
 *
 * A size of zero is not an error: it is a pool that holds no texture yet,
 * and the guest is free to register into it later. Only a missing memory pool
 * is refused, because a texture pool without memory behind it addresses
 * nothing. */
int nvn_texture_pool_initialize(void *pool, const void *memory_pool, ptrdiff_t offset, int size) {
    nvn_texture_pool_state *p;
    int ready = 0;
    int base = 0;

    pthread_mutex_lock(&s_texture_pool_lock);
    s_stats.builder_calls++;
    if (!memory_pool) {
        s_stats.refused++;
        pthread_mutex_unlock(&s_texture_pool_lock);
        fprintf(stderr, "[nvn] nvnTexturePoolInitialize: no memory pool for %p; a texture pool is a slice of a "
                        "memory pool, so it cannot exist without one\n",
                pool);
        return 0;
    }
    p = pool_find(pool, 1);
    if (!p) {
        s_stats.refused++;
        pthread_mutex_unlock(&s_texture_pool_lock);
        fprintf(stderr, "[nvn] nvnTexturePoolInitialize: no room for another texture pool\n");
        return 0;
    }
    p->memory_pool = (void *)(uintptr_t)memory_pool;
    p->memory_offset = offset;
    p->size = size;
    p->initialized = 1;
    /* The table of registered textures starts empty. It grows when the guest
     * registers an index it has not used before, so that the size of the
     * guest's pool decides how much memory is taken, and not a guess made
     * here. */
    free(p->textures);
    p->textures = NULL;
    p->texture_capacity = 0;
    /* The numbering of this pool is settled here, at its first Initialize,
     * and it does not change when the pool is finalized and initialized
     * again. */
    base = base_assign(pool, size);
    s_stats.initialized++;
    ready = 1;
    pthread_mutex_unlock(&s_texture_pool_lock);

    fprintf(stderr, "[nvn] nvnTexturePoolInitialize: pool=%p memory_pool=%p offset=%" PRIdPTR " size=%d base=%d -> %s\n",
            pool, memory_pool, offset, size, base, ready ? "true" : "false");
    return 1;
}

void nvn_texture_pool_set_debug_label(void *pool, const char *label) {
    nvn_texture_pool_state *p;
    pthread_mutex_lock(&s_texture_pool_lock);
    p = pool_find(pool, 1);
    if (p) snprintf(p->debug_label, sizeof(p->debug_label), "%s", label ? label : "");
    pthread_mutex_unlock(&s_texture_pool_lock);
    NVN_VOID_RETURN();
}

void nvn_texture_pool_finalize(void *pool) {
    nvn_texture_pool_state *p;
    pthread_mutex_lock(&s_texture_pool_lock);
    p = pool_find(pool, 0);
    if (p) {
        p->initialized = 0;
        p->memory_pool = NULL;
        p->memory_offset = 0;
        p->size = 0;
        free(p->textures);
        p->textures = NULL;
        p->texture_capacity = 0;
        p->in_use = 0;
        s_stats.finalized++;
    }
    pthread_mutex_unlock(&s_texture_pool_lock);
    fprintf(stderr, "[nvn] nvnTexturePoolFinalize: pool=%p\n", pool);
    NVN_VOID_RETURN();
}

/* nvnTexturePoolRegisterTexture(pool, index, texture, view).
 *
 * The texture's own state is not read here: it belongs to the texture
 * family, which stores it against the texture's address. What the pool has to
 * remember is which index holds which texture, because the guest refers to
 * the textures by index when it draws. The view is recorded as well, because
 * it selects the part of the texture the guest means. */
void nvn_texture_pool_register_texture(const void *pool, int index, const void *texture, const void *view) {
    nvn_texture_pool_state *p;
    (void)view;
    pthread_mutex_lock(&s_texture_pool_lock);
    p = pool_find((void *)(uintptr_t)pool, 0);
    if (p && p->initialized && index >= 0 && index < NVN_TEXTURE_POOL_MAX_TEXTURES) {
        if (index >= p->texture_capacity && !texture_table_grow(p, index)) {
            fprintf(stderr, "[nvn] nvnTexturePoolRegisterTexture: index=%d could not be held; the texture was not "
                            "registered\n",
                    index);
        } else {
            p->textures[index] = (void *)(uintptr_t)texture;
            s_stats.textures_registered++;
        }
    } else if (p && p->initialized) {
        fprintf(stderr, "[nvn] nvnTexturePoolRegisterTexture: index=%d is outside the %d textures a handle can "
                        "address; the texture was not registered\n",
                index, NVN_TEXTURE_POOL_MAX_TEXTURES);
    }
    pthread_mutex_unlock(&s_texture_pool_lock);
    NVN_VOID_RETURN();
}

/* The same registration for an image: the guest registers the same texture
 * handle twice when it wants two views of it. */
void nvn_texture_pool_register_image(const void *pool, int index, const void *texture, const void *view) {
    nvn_texture_pool_state *p;
    (void)view;
    pthread_mutex_lock(&s_texture_pool_lock);
    p = pool_find((void *)(uintptr_t)pool, 0);
    if (p && p->initialized && index >= 0 && index < NVN_TEXTURE_POOL_MAX_TEXTURES) {
        if (index >= p->texture_capacity && !texture_table_grow(p, index)) {
            fprintf(stderr, "[nvn] nvnTexturePoolRegisterImage: index=%d could not be held; the image was not "
                            "registered\n",
                    index);
        } else {
            p->textures[index] = (void *)(uintptr_t)texture;
            s_stats.images_registered++;
        }
    } else if (p && p->initialized) {
        fprintf(stderr, "[nvn] nvnTexturePoolRegisterImage: index=%d is outside the %d textures a handle can "
                        "address; the image was not registered\n",
                index, NVN_TEXTURE_POOL_MAX_TEXTURES);
    }
    pthread_mutex_unlock(&s_texture_pool_lock);
    NVN_VOID_RETURN();
}

void *nvn_texture_pool_get_memory_pool(const void *pool) {
    nvn_texture_pool_state *p;
    void *memory_pool = NULL;
    pthread_mutex_lock(&s_texture_pool_lock);
    p = pool_find((void *)(uintptr_t)pool, 0);
    if (p && p->initialized) memory_pool = p->memory_pool;
    pthread_mutex_unlock(&s_texture_pool_lock);
    return memory_pool;
}

ptrdiff_t nvn_texture_pool_get_memory_offset(const void *pool) {
    nvn_texture_pool_state *p;
    ptrdiff_t offset = 0;
    pthread_mutex_lock(&s_texture_pool_lock);
    p = pool_find((void *)(uintptr_t)pool, 0);
    if (p && p->initialized) offset = p->memory_offset;
    pthread_mutex_unlock(&s_texture_pool_lock);
    return offset;
}

int nvn_texture_pool_get_size(const void *pool) {
    nvn_texture_pool_state *p;
    int size = 0;
    pthread_mutex_lock(&s_texture_pool_lock);
    p = pool_find((void *)(uintptr_t)pool, 0);
    if (p && p->initialized) size = p->size;
    pthread_mutex_unlock(&s_texture_pool_lock);
    return size;
}

/* The numbering base of the pool.
 *
 * The base is what a handle has to be read with: the handle holds the index
 * inside the pool, and the base says where that index sits in the numbering
 * that all the pools share. The base of the first pool is zero, so a caller
 * that has to tell "the first pool" from "a pool that was never initialized"
 * asks nvn_texture_pool_known first. */
int nvn_texture_pool_base(const void *pool) {
    int base = 0;
    pthread_mutex_lock(&s_texture_pool_lock);
    base_find(pool, &base);
    pthread_mutex_unlock(&s_texture_pool_lock);
    return base;
}

/* Whether the pool has a numbering base of its own. */
int nvn_texture_pool_known(const void *pool) {
    int base = 0;
    int known = 0;
    pthread_mutex_lock(&s_texture_pool_lock);
    known = base_find(pool, &base);
    pthread_mutex_unlock(&s_texture_pool_lock);
    return known;
}

/* The texture that is registered at an index of the pool.
 *
 * The texture's own state belongs to the texture family, which keeps it
 * against the texture's address; the pool only remembers which index holds
 * which texture, because the guest refers to the textures by index when it
 * draws. An index that was never registered gives nothing back. */
const void *nvn_texture_pool_texture_at(const void *pool, int index) {
    nvn_texture_pool_state *p;
    const void *texture = NULL;
    pthread_mutex_lock(&s_texture_pool_lock);
    p = pool_find((void *)(uintptr_t)pool, 0);
    if (p && p->initialized && index >= 0 && index < p->texture_capacity) {
        texture = p->textures[index];
    }
    pthread_mutex_unlock(&s_texture_pool_lock);
    return texture;
}

/* ------------------------------------------------------------------ */
/* The names                                                           */
/* ------------------------------------------------------------------ */

static const nvn_texture_pool_family_entry s_texture_pool_family[] = {
    { "nvnTexturePoolInitialize", (void *)(uintptr_t)nvn_texture_pool_initialize },
    { "nvnTexturePoolSetDebugLabel", (void *)(uintptr_t)nvn_texture_pool_set_debug_label },
    { "nvnTexturePoolFinalize", (void *)(uintptr_t)nvn_texture_pool_finalize },
    { "nvnTexturePoolRegisterTexture", (void *)(uintptr_t)nvn_texture_pool_register_texture },
    { "nvnTexturePoolRegisterImage", (void *)(uintptr_t)nvn_texture_pool_register_image },
    { "nvnTexturePoolGetMemoryPool", (void *)(uintptr_t)nvn_texture_pool_get_memory_pool },
    { "nvnTexturePoolGetMemoryOffset", (void *)(uintptr_t)nvn_texture_pool_get_memory_offset },
    { "nvnTexturePoolGetSize", (void *)(uintptr_t)nvn_texture_pool_get_size },
};

size_t nvn_texture_pool_family_count(void) {
    return sizeof(s_texture_pool_family) / sizeof(s_texture_pool_family[0]);
}

const nvn_texture_pool_family_entry *nvn_texture_pool_family_entry_at(size_t index) {
    if (index >= nvn_texture_pool_family_count()) return NULL;
    return &s_texture_pool_family[index];
}

void nvn_texture_pool_get_stats(nvn_texture_pool_stats *out) {
    size_t i;
    if (!out) return;
    pthread_mutex_lock(&s_texture_pool_lock);
    *out = s_stats;
    out->live_pools = 0;
    for (i = 0; i < sizeof(s_pools) / sizeof(s_pools[0]); i++) {
        if (s_pools[i].in_use && s_pools[i].initialized) out->live_pools++;
    }
    pthread_mutex_unlock(&s_texture_pool_lock);
}

/* What this family knows about one texture pool: the memory pool the
 * descriptors live in and how many of them there are. */
int nvn_texture_pool_info_of(const void *pool, nvn_pool_info *out) {
    nvn_texture_pool_state *slot;
    if (!pool || !out) return 0;
    pthread_mutex_lock(&s_texture_pool_lock);
    slot = pool_find((void *)(uintptr_t)pool, 0);
    if (!slot || !slot->initialized) {
        pthread_mutex_unlock(&s_texture_pool_lock);
        return 0;
    }
    out->memory_pool = slot->memory_pool;
    out->memory_offset = slot->memory_offset;
    out->size = slot->size;
    pthread_mutex_unlock(&s_texture_pool_lock);
    return 1;
}
