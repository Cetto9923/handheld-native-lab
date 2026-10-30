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

#include "../nvn_vulkan/nvn/nvn_tex.h"

/* One kind of object. The bridge keeps one table per kind, because the
 * guest's addresses of the different kinds are addresses of different
 * objects. */
/* Pool versions own immutable copies until process teardown. Retaining their
 * metadata makes publication idempotent even when concurrent readers captured
 * different family generations. No reclamation/GPU lifetime claim is made. */
typedef struct bridge_snapshot {
    uint64_t generation;
    const void *dependency;
    void *object;
    struct bridge_snapshot *next;
} bridge_snapshot;

typedef struct bridge_entry {
    const void *guest;   /* the address the guest named */
    uint64_t serial;     /* the generation of the object there */
    void *object;        /* the copy the replay layer reads */
    size_t size;         /* how many bytes the copy is */
    bridge_snapshot *snapshots; /* pool/descriptor versions only */
    int retained; /* texture pointer escaped into a recorded command */
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
        if (table->capacity > SIZE_MAX / 2 / sizeof(*table->entries)) return NULL;
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

/* Called with s_lock held; all returned objects are individually allocated.
 * Table reallocations move only metadata, never an object captured in commands. */
static void *snapshot_find(bridge_table *table, const void *guest, uint64_t generation,
                           const void *dependency) {
    bridge_entry *entry = entry_find(table, guest, 0);
    if (entry) for (bridge_snapshot *v = entry->snapshots; v; v = v->next)
        if (v->generation == generation && v->dependency == dependency) return v->object;
    return NULL;
}

/* The losing concurrent creator frees only its unpublished private candidate.
 * Failure to grow metadata is reported as NULL, never a transient uncached copy. */
static void *snapshot_publish(bridge_table *table, const void *guest, uint64_t generation,
                              const void *dependency, void *object, size_t size) {
    void *existing = snapshot_find(table, guest, generation, dependency);
    if (existing) { free(object); return existing; }
    bridge_entry *entry = entry_find(table, guest, 1);
    bridge_snapshot *v = entry ? malloc(sizeof(*v)) : NULL;
    if (!v) { free(object); return NULL; }
    v->generation = generation; v->dependency = dependency; v->object = object;
    v->next = entry->snapshots; entry->snapshots = v;
    if (entry->object) ++s_regenerated; else ++s_objects;
    entry->serial = generation; entry->object = object; entry->size = size;
    return object;
}

extern void xbg_texture_lifetime_end(const ffrs_tex *texture) __attribute__((weak));
void nvn_bridge_texture_end(const void *guest_texture, uint64_t serial) {
    ffrs_tex copy;
    int found = 0;
    if (!guest_texture || !serial) return;
    pthread_mutex_lock(&s_lock);
    bridge_entry *entry = entry_find(&s_textures, guest_texture, 0);
    if (entry && entry->serial == serial && entry->object) {
        copy = *(const ffrs_tex *)entry->object; found = 1;
    }
    pthread_mutex_unlock(&s_lock);
    /* Never call the renderer under either object-table lock. The immutable
     * copy stays alive for recorded commands and a generation mismatch must
     * never release a newer object at the same guest address. */
    if (found && xbg_texture_lifetime_end) xbg_texture_lifetime_end(&copy);
}

/* Descriptor registration consumes a value immediately. Reuse the latest
 * unescaped copy at an address; once a command borrows a pointer, that copy
 * becomes immutable and is retained under the existing command lifetime rule.
 * All reads/copies of reusable storage occur under s_lock. */
static const ffrs_tex *texture_get(const void *guest_texture, ffrs_tex *out, int retain) {
    nvn_texture_info info;
    ffrs_tex value, *tex;
    const ffrs_pool *pool;
    bridge_entry *entry;
    if (!guest_texture || !nvn_texture_info_of(guest_texture, &info)) return NULL;
    pthread_mutex_lock(&s_lock);
    entry = entry_find(&s_textures, guest_texture, 0);
    if (entry && entry->object && entry->serial == info.serial) {
        tex = entry->object;
        if (retain) entry->retained = 1;
        if (out) *out = *tex;
        pthread_mutex_unlock(&s_lock);
        return tex;
    }
    pthread_mutex_unlock(&s_lock);

    memset(&value, 0, sizeof(value));
    value.magic = FFRS_NVN_MAGIC('T', 'E', 'X', 0);
    value.d = info.d;
    value.serial = info.serial;
    pool = nvn_bridge_pool(info.memory_pool);
    if (pool) {
        value.pool = (ffrs_pool *)pool;
        value.off = info.memory_offset;
        value.mem = pool->mem ? pool->mem + info.memory_offset : NULL;
    }
    if (value.d.format != 0) {
        nvn_tex_layout((const nvn_texdesc *)&value.d, (nvn_texlayout *)&value.L);
        value.has_layout = 1;
    }
    pthread_mutex_lock(&s_lock);
    entry = entry_find(&s_textures, guest_texture, 1);
    if (!entry) { pthread_mutex_unlock(&s_lock); return NULL; }
    /* Another reader may have published this generation while we resolved
     * its pool. Return that canonical object rather than leaking a duplicate. */
    if (entry->object && entry->serial == info.serial) {
        tex = entry->object;
        if (retain) entry->retained = 1;
    } else {
        tex = entry->object && !entry->retained ? entry->object : calloc(1, sizeof(*tex));
        if (!tex) { pthread_mutex_unlock(&s_lock); return NULL; }
        if (entry->object) ++s_regenerated; else ++s_objects;
        *tex = value;
        entry->object = tex; entry->serial = info.serial; entry->size = sizeof(*tex);
        entry->retained = retain;
    }
    if (out) *out = *tex;
    pthread_mutex_unlock(&s_lock);
    return tex;
}

const ffrs_tex *nvn_bridge_texture(const void *guest_texture) {
    return texture_get(guest_texture, NULL, 1);
}

int nvn_bridge_texture_copy(const void *guest_texture, ffrs_tex *out) {
    if (!out) return 0;
    return texture_get(guest_texture, out, 0) != NULL;
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

    prog = (ffrs_prog *)calloc(1, sizeof(*prog)+sizeof(info.control));
    if (!prog) return NULL;
    prog->magic = FFRS_NVN_MAGIC('P', 'R', 'G', 0);
    prog->nstage = info.nstage;
    memcpy(prog->sd, info.sd, sizeof(prog->sd));
    unsigned char *control=(unsigned char*)(prog+1);
    memcpy(control,info.control,sizeof(info.control));
    for(unsigned i=0;i<6;i++)prog->sd[i].control=control+i*0x880;
    prog->serial = info.serial;
    pthread_mutex_lock(&s_lock);
    entry_put(&s_programs, guest_program, info.serial, prog, sizeof(*prog)+sizeof(info.control));
    pthread_mutex_unlock(&s_lock);
    return prog;
}

const ffrs_pool *nvn_bridge_pool(const void *guest_pool) {
    nvn_memory_pool_info info;
    ffrs_pool *pool;
    if (!guest_pool || !nvn_memory_pool_info_of(guest_pool, &info) || !info.generation) return NULL;
    pthread_mutex_lock(&s_lock);
    pool = snapshot_find(&s_pools, guest_pool, info.generation, NULL);
    pthread_mutex_unlock(&s_lock);
    if (pool) return pool;

    pool = calloc(1, sizeof(*pool));
    if (!pool) return NULL;
    pool->magic = FFRS_NVN_MAGIC('P', 'O', 'O', 'L');
    pool->flags = info.flags; pool->mem = info.mem; pool->size = info.size;
    pthread_mutex_lock(&s_lock);
    pool = snapshot_publish(&s_pools, guest_pool, info.generation, NULL, pool, sizeof(*pool));
    pthread_mutex_unlock(&s_lock);
    return pool;
}

/* Descriptor snapshots include the stable backing identity in the key. Family
 * and bridge locks are never nested; no engine/GPU callback occurs here. */
static const ffrs_dpool *dpool_of(const void *guest_pool, bridge_table *table, int texture_pool) {
    nvn_pool_info info;
    ffrs_dpool *dpool;
    const ffrs_pool *backing;
    if (!guest_pool) return NULL;
    if (texture_pool) {
        if (!nvn_texture_pool_info_of(guest_pool, &info)) return NULL;
    } else {
        if (!nvn_sampler_pool_info_of(guest_pool, &info)) return NULL;
    }
    if (!info.generation || !(backing = nvn_bridge_pool(info.memory_pool))) return NULL;
    pthread_mutex_lock(&s_lock);
    dpool = snapshot_find(table, guest_pool, info.generation, backing);
    pthread_mutex_unlock(&s_lock);
    if (dpool) return dpool;

    dpool = calloc(1, sizeof(*dpool));
    if (!dpool) return NULL;
    dpool->pool = backing; dpool->off = info.memory_offset; dpool->n = info.size;
    pthread_mutex_lock(&s_lock);
    dpool = snapshot_publish(table, guest_pool, info.generation, backing, dpool, sizeof(*dpool));
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
