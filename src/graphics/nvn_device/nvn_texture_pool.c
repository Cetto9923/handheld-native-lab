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
     * not been registered yet. */
    void *textures[NVN_TEXTURE_POOL_MAX_TEXTURES];
    char debug_label[64];
    int in_use;
} nvn_texture_pool_state;

static pthread_mutex_t s_texture_pool_lock = PTHREAD_MUTEX_INITIALIZER;
static nvn_texture_pool_state s_pools[8];
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
    memset(p->textures, 0, sizeof(p->textures));
    s_stats.initialized++;
    ready = 1;
    pthread_mutex_unlock(&s_texture_pool_lock);

    fprintf(stderr, "[nvn] nvnTexturePoolInitialize: pool=%p memory_pool=%p offset=%" PRIdPTR " size=%d -> %s\n",
            pool, memory_pool, offset, size, ready ? "true" : "false");
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
        memset(p->textures, 0, sizeof(p->textures));
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
        p->textures[index] = (void *)(uintptr_t)texture;
        s_stats.textures_registered++;
    } else if (p && p->initialized) {
        fprintf(stderr, "[nvn] nvnTexturePoolRegisterTexture: index=%d is outside the %d textures this pool "
                        "tracks; the texture was not registered\n",
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
        p->textures[index] = (void *)(uintptr_t)texture;
        s_stats.images_registered++;
    } else if (p && p->initialized) {
        fprintf(stderr, "[nvn] nvnTexturePoolRegisterImage: index=%d is outside the %d textures this pool "
                        "tracks; the image was not registered\n",
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
