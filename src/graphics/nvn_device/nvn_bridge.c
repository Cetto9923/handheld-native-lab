/* The bridge between the state of this runtime and the objects the replay
 * layer reads. See nvn_bridge.h for what it is for.
 *
 * The bridge keeps one table of objects, keyed by the guest's address. An
 * entry is made once and kept: the commands that were recorded hold the
 * pointer to the object, and the replay layer reads it when the command is
 * submitted. An entry is made again when the guest initializes the same
 * address again, because then it is a different object.
 */
#include "nvn_bridge.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* One kind of object. The bridge keeps one table per kind, because the
 * guest's addresses of the different kinds are addresses of different
 * objects. */
typedef struct bridge_entry {
    const void *guest;   /* the address the guest named */
    uint64_t serial;     /* the generation of the object there */
    void *object;        /* the copy the replay layer reads */
    size_t size;         /* how many bytes the copy is */
} bridge_entry;

typedef struct bridge_table {
    bridge_entry *entries;
    size_t count;
    size_t capacity;
} bridge_table;

static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static bridge_table s_textures;
static bridge_table s_programs;
static bridge_table s_pools;
static bridge_table s_texture_pools;
static bridge_table s_sampler_pools;
static bridge_table s_samplers;
static bridge_table s_views;
static uint64_t s_objects;
static uint64_t s_regenerated;

/* Find the entry of an address, or make one. */
static bridge_entry *entry_find(bridge_table *table, const void *guest, int create) {
    size_t i;
    bridge_entry *entry = NULL;
    for (i = 0; i < table->count; i++) {
        if (table->entries[i].guest == guest) return &table->entries[i];
    }
    if (!create) return NULL;
    if (table->count == table->capacity) {
        size_t capacity = table->capacity ? table->capacity * 2 : 16;
        bridge_entry *grown = (bridge_entry *)realloc(table->entries, capacity * sizeof(*grown));
        if (!grown) return NULL;
        table->entries = grown;
        table->capacity = capacity;
    }
    entry = &table->entries[table->count++];
    memset(entry, 0, sizeof(*entry));
    entry->guest = guest;
    return entry;
}

/* Put an object into the table of its kind, replacing what was there when
 * the guest made a new object at the same address.
 *
 * The old copy is not freed: a command that was recorded may still name it,
 * and the replay layer reads it when that command is submitted. */
static void entry_put(bridge_table *table, const void *guest, uint64_t serial, void *object,
                      size_t size) {
    bridge_entry *entry = entry_find(table, guest, 1);
    if (!entry) return;
    if (entry->object && entry->serial != serial) s_regenerated++;
    if (!entry->object) s_objects++;
    entry->serial = serial;
    entry->object = object;
    entry->size = size;
}

const ffrs_tex *nvn_bridge_texture(const void *guest_texture) {
    nvn_texture_info info;
    ffrs_tex *tex;
    const ffrs_pool *pool;
    bridge_entry *entry;
    if (!guest_texture) return NULL;
    if (!nvn_texture_info_of(guest_texture, &info)) return NULL;
    pthread_mutex_lock(&s_lock);
    entry = entry_find(&s_textures, guest_texture, 0);
    if (entry && entry->object && entry->serial == info.serial) {
        pthread_mutex_unlock(&s_lock);
        return (const ffrs_tex *)entry->object;
    }
    pthread_mutex_unlock(&s_lock);

    tex = (ffrs_tex *)calloc(1, sizeof(*tex));
    if (!tex) return NULL;
    tex->magic = FFRS_NVN_MAGIC('T', 'E', 'X', 0);
    tex->d = info.d;
    tex->serial = info.serial;
    pool = nvn_bridge_pool(info.memory_pool);
    if (pool) {
        tex->pool = (ffrs_pool *)pool;
        tex->off = info.memory_offset;
        tex->mem = pool->mem ? pool->mem + info.memory_offset : NULL;
    }
    pthread_mutex_lock(&s_lock);
    entry_put(&s_textures, guest_texture, info.serial, tex, sizeof(*tex));
    pthread_mutex_unlock(&s_lock);
    return tex;
}

const ffrs_prog *nvn_bridge_program(const void *guest_program) {
    nvn_program_info info;
    ffrs_prog *prog;
    bridge_entry *entry;
    if (!guest_program) return NULL;
    if (!nvn_program_info_of(guest_program, &info)) return NULL;
    pthread_mutex_lock(&s_lock);
    entry = entry_find(&s_programs, guest_program, 0);
    if (entry && entry->object && entry->serial == info.serial) {
        pthread_mutex_unlock(&s_lock);
        return (const ffrs_prog *)entry->object;
    }
    pthread_mutex_unlock(&s_lock);

    prog = (ffrs_prog *)calloc(1, sizeof(*prog));
    if (!prog) return NULL;
    prog->magic = FFRS_NVN_MAGIC('P', 'R', 'G', 0);
    prog->nstage = info.nstage;
    memcpy(prog->sd, info.sd, sizeof(prog->sd));
    prog->serial = info.serial;
    pthread_mutex_lock(&s_lock);
    entry_put(&s_programs, guest_program, info.serial, prog, sizeof(*prog));
    pthread_mutex_unlock(&s_lock);
    return prog;
}

const ffrs_pool *nvn_bridge_pool(const void *guest_pool) {
    nvn_memory_pool_info info;
    ffrs_pool *pool;
    bridge_entry *entry;
    if (!guest_pool) return NULL;
    if (!nvn_memory_pool_info_of(guest_pool, &info)) return NULL;
    pthread_mutex_lock(&s_lock);
    entry = entry_find(&s_pools, guest_pool, 0);
    if (entry && entry->object) {
        pthread_mutex_unlock(&s_lock);
        return (const ffrs_pool *)entry->object;
    }
    pthread_mutex_unlock(&s_lock);

    pool = (ffrs_pool *)calloc(1, sizeof(*pool));
    if (!pool) return NULL;
    pool->magic = FFRS_NVN_MAGIC('P', 'O', 'O', 'L');
    pool->flags = info.flags;
    pool->mem = info.mem;
    pool->size = info.size;
    pthread_mutex_lock(&s_lock);
    entry_put(&s_pools, guest_pool, 0, pool, sizeof(*pool));
    pthread_mutex_unlock(&s_lock);
    return pool;
}

/* The descriptor pool of the guest's address. The replay layer keys its
 * tables by the memory the pool's descriptors live in and by where in that
 * memory they start, so the copy itself does not have to stay. */
static const ffrs_dpool *dpool_of(const void *guest_pool, bridge_table *table, int texture_pool) {
    nvn_pool_info info;
    ffrs_dpool *dpool;
    bridge_entry *entry;
    if (!guest_pool) return NULL;
    if (texture_pool) {
        if (!nvn_texture_pool_info_of(guest_pool, &info)) return NULL;
    } else {
        if (!nvn_sampler_pool_info_of(guest_pool, &info)) return NULL;
    }
    pthread_mutex_lock(&s_lock);
    entry = entry_find(table, guest_pool, 0);
    if (entry && entry->object) {
        pthread_mutex_unlock(&s_lock);
        return (const ffrs_dpool *)entry->object;
    }
    pthread_mutex_unlock(&s_lock);

    dpool = (ffrs_dpool *)calloc(1, sizeof(*dpool));
    if (!dpool) return NULL;
    dpool->pool = (ffrs_pool *)nvn_bridge_pool(info.memory_pool);
    dpool->off = info.memory_offset;
    dpool->n = info.size;
    pthread_mutex_lock(&s_lock);
    entry_put(table, guest_pool, 0, dpool, sizeof(*dpool));
    pthread_mutex_unlock(&s_lock);
    return dpool;
}

const ffrs_dpool *nvn_bridge_texture_pool(const void *guest_pool) {
    return dpool_of(guest_pool, &s_texture_pools, 1);
}

const ffrs_dpool *nvn_bridge_sampler_pool(const void *guest_pool) {
    return dpool_of(guest_pool, &s_sampler_pools, 0);
}

const ffrs_samp_par *nvn_bridge_sampler(const void *guest_sampler) {
    ffrs_samp_par params;
    ffrs_samp_par *copy;
    bridge_entry *entry;
    uint64_t serial = 0;
    if (!guest_sampler) return NULL;
    if (!nvn_sampler_info_of(guest_sampler, &params, &serial)) return NULL;
    pthread_mutex_lock(&s_lock);
    entry = entry_find(&s_samplers, guest_sampler, 0);
    if (entry && entry->object && entry->serial == serial) {
        pthread_mutex_unlock(&s_lock);
        return (const ffrs_samp_par *)entry->object;
    }
    pthread_mutex_unlock(&s_lock);

    copy = (ffrs_samp_par *)calloc(1, sizeof(*copy));
    if (!copy) return NULL;
    *copy = params;
    pthread_mutex_lock(&s_lock);
    entry_put(&s_samplers, guest_sampler, serial, copy, sizeof(*copy));
    pthread_mutex_unlock(&s_lock);
    return copy;
}

const ffrs_view *nvn_bridge_view(const void *guest_view) {
    ffrs_view view;
    ffrs_view *copy;
    bridge_entry *entry;
    uint64_t serial = 0;
    if (!guest_view) return NULL;
    if (!nvn_texture_view_info_of(guest_view, &view, &serial)) return NULL;
    pthread_mutex_lock(&s_lock);
    entry = entry_find(&s_views, guest_view, 0);
    if (entry && entry->object && entry->serial == serial) {
        pthread_mutex_unlock(&s_lock);
        return (const ffrs_view *)entry->object;
    }
    pthread_mutex_unlock(&s_lock);

    copy = (ffrs_view *)calloc(1, sizeof(*copy));
    if (!copy) return NULL;
    *copy = view;
    pthread_mutex_lock(&s_lock);
    entry_put(&s_views, guest_view, serial, copy, sizeof(*copy));
    pthread_mutex_unlock(&s_lock);
    return copy;
}

void nvn_bridge_get_stats(uint64_t *objects, uint64_t *regenerated) {
    pthread_mutex_lock(&s_lock);
    if (objects) *objects = s_objects;
    if (regenerated) *regenerated = s_regenerated;
    pthread_mutex_unlock(&s_lock);
}
