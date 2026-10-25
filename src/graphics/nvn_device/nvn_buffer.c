/* The NVN buffer family implementation.
 *
 * See nvn_buffer.h for why the state lives on the host and what a buffer is.
 *
 * The guest hands a buffer builder to nvnBufferInitialize and from then on the
 * buffer holds a copy of the builder's description. The copy is taken at the
 * moment of the initialization, so a buffer is a snapshot: the builder can
 * change afterwards without touching the buffer that was already made.
 *
 * The storage of a buffer is the guest's own memory, handed to the runtime by
 * the guest's memory pool. The address this family hands out is the address of
 * that memory plus the offset of the buffer inside it, the way the reference
 * host answers (hle_nvn.c:1779, 1780). The address is therefore an address of
 * the guest, and the command buffer's replay resolves it back into the host's
 * memory through the memory pool family (nvn_memory_pool_resolve_address).
 * An address invented here would not resolve, which is why the address comes
 * from the memory pool family and is never composed out of thin air.
 *
 * The evidence for the values:
 *
 *   - The signatures of the entry points are the ones of the vendored NVN
 *     header (src/graphics/nvn/include/nvn/nvn.h:1234-1251), which is a
 *     reconstruction of NVN 53.105 taken from another game (Darksiders II).
 *     Its signatures are therefore NOT confirmed for this game, which is Unreal
 *     Engine 5.6.1.
 *   - The description that is copied is the one of the reference host's
 *     buf_builder_t (hle_nvn.c:1760): the device, the memory pool, the offset
 *     inside the pool and the size of the buffer.
 *   - The defaults are the ones of the reference host (hle_nvn.c:1766):
 *     nvnBufferBuilderSetDefaults keeps the device and clears everything else.
 *   - The refusal of an initialization without storage is the one of the
 *     reference host (hle_nvn.c:1770): a builder whose pool has no memory
 *     cannot make a buffer, so the call answers false.
 *   - The flush and the invalidate are the ones of the reference host
 *     (hle_nvn.c:1784, 1785), which records them as empty operations and says
 *     why: *unified memory: nothing to flush*.
 *
 * Three of the eighteen names have no implementation in the reference host, and
 * they are named in the report of this family:
 *
 *   - nvnBufferBuilderGetDevice has no signature in the vendored header at all
 *     and is in the reference tree's list of "names without a prototype in the
 *     header" (nvn_protos.h:506). Its arguments are therefore recorded as the
 *     words they came in, the way nvnTextureBuilderGetDevice does it, and the
 *     device of the builder is returned in the register the ABI returns a
 *     value in.
 *   - nvnBufferBuilderGetMemoryPool has a signature in the vendored header
 *     (nvn.h:1238) that returns the pool by value, and the reference host does
 *     not implement it. A pool of 256 opaque bytes cannot be handed to the
 *     guest as a value, and the sibling getter nvnBufferGetMemoryPool
 *     (nvn.h:1248) returns a pointer, so this returns the pointer.
 *   - nvnBufferGetDebugID has a signature in the vendored header (nvn.h:1251)
 *     and the reference host does not implement it. The answer is the id this
 *     runtime hands out for the object, taken from the device's debug domain
 *     id generator, the same source every other object of this runtime uses.
 */
#include "nvn_buffer.h"

#include "nvn_device.h"

#include "nvn_memory_pool.h"

#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    void *builder;
    void *device;
    void *memory_pool;
    ptrdiff_t memory_offset;
    size_t size;
    int in_use;
} nvn_buffer_builder_state;

typedef struct {
    void *buffer;
    void *device;
    void *memory_pool;
    ptrdiff_t memory_offset;
    size_t size;
    uint64_t debug_id;
    char debug_label[64];
    int initialized;
    int in_use;
} nvn_buffer_state;

/* The names of this family, in the order the guest itself resolves them in
 * (the device run of 2026-10-25). The index is used for the "only the first
 * call is logged" bookkeeping. */
enum {
    BUF_BUILDER_SET_DEFAULTS,
    BUF_BUILDER_SET_DEVICE,
    BUF_BUILDER_SET_STORAGE,
    BUF_BUILDER_GET_DEVICE,
    BUF_BUILDER_GET_SIZE,
    BUF_BUILDER_GET_MEMORY_OFFSET,
    BUF_BUILDER_GET_MEMORY_POOL,
    BUF_INITIALIZE,
    BUF_FINALIZE,
    BUF_SET_DEBUG_LABEL,
    BUF_GET_DEBUG_ID,
    BUF_GET_ADDRESS,
    BUF_MAP,
    BUF_GET_SIZE,
    BUF_GET_MEMORY_POOL,
    BUF_GET_MEMORY_OFFSET,
    BUF_FLUSH_MAPPED_RANGE,
    BUF_INVALIDATE_MAPPED_RANGE,
    BUF_NAME_COUNT
};

static pthread_mutex_t s_buffer_lock = PTHREAD_MUTEX_INITIALIZER;
static nvn_buffer_builder_state s_builders[NVN_BUFFER_MAX_BUILDERS];
static nvn_buffer_state s_buffers[NVN_BUFFER_MAX_BUFFERS];
static nvn_buffer_stats s_stats;
static int s_logged[BUF_NAME_COUNT];

/* The state of a builder, keyed by the guest's builder address. A builder that
 * was never prepared has no state, and the functions that need one say so
 * instead of inventing one. */
static nvn_buffer_builder_state *builder_find(void *builder, int create) {
    size_t i;
    nvn_buffer_builder_state *free_slot = NULL;
    if (!builder) return NULL;
    for (i = 0; i < NVN_BUFFER_MAX_BUILDERS; i++) {
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

/* The state of a buffer, keyed by the guest's buffer address. */
static nvn_buffer_state *buffer_find(void *buffer, int create) {
    size_t i;
    nvn_buffer_state *free_slot = NULL;
    if (!buffer) return NULL;
    for (i = 0; i < NVN_BUFFER_MAX_BUFFERS; i++) {
        if (s_buffers[i].in_use) {
            if (s_buffers[i].buffer == buffer) return &s_buffers[i];
        } else if (!free_slot) {
            free_slot = &s_buffers[i];
        }
    }
    if (!create || !free_slot) return NULL;
    memset(free_slot, 0, sizeof(*free_slot));
    free_slot->buffer = buffer;
    free_slot->in_use = 1;
    return free_slot;
}

/* Mark a name as logged and say whether this call is the first one. Called
 * with the lock held. */
static int mark_logged(int index) {
    if (s_logged[index]) return 0;
    s_logged[index] = 1;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Asking the memory pool family                                        */
/* ------------------------------------------------------------------ */

/* The entry point of the memory pool family, found by its name. The buffer
 * family does not reach into the memory pool family's tables: it asks the
 * memory pool family for the values, the same way the guest asks it. */
static void *memory_pool_entry(const char *name) {
    size_t i;
    size_t count = nvn_memory_pool_family_count();
    for (i = 0; i < count; i++) {
        const nvn_memory_pool_family_entry *entry = nvn_memory_pool_family_entry_at(i);
        if (entry && entry->name && strcmp(entry->name, name) == 0) return entry->fn;
    }
    return NULL;
}

/* The CPU mapping of a pool, which is the memory the guest allocated for it.
 * This is the same thing as the reference host's pool->mem (hle_nvn.c:1779),
 * and it is the only place a buffer's address may come from: the guest's own
 * memory, not a number made up here. A pool that was never initialized has no
 * memory, and the answer is then a null pointer. */
static void *memory_pool_map(const void *pool) {
    typedef void *(*pool_map_fn)(const void *);
    pool_map_fn fn = (pool_map_fn)(uintptr_t)memory_pool_entry("nvnMemoryPoolMap");
    if (!fn || !pool) return NULL;
    return fn(pool);
}

/* The size of a pool, so that a buffer that reaches past the end of its pool
 * is named as the reference host names it (hle_nvn.c:1773). */
static uint64_t memory_pool_size(const void *pool) {
    typedef uint64_t (*pool_get_size_fn)(const void *);
    pool_get_size_fn fn = (pool_get_size_fn)(uintptr_t)memory_pool_entry("nvnMemoryPoolGetSize");
    if (!fn || !pool) return 0;
    return fn(pool);
}

/* ------------------------------------------------------------------ */
/* The builder                                                          */
/* ------------------------------------------------------------------ */

/* nvnBufferBuilderSetDefaults(builder).
 *
 * The reference host keeps the device of the builder and clears everything
 * else (hle_nvn.c:1766): memset over the whole builder, then the device is
 * put back. The state here is on the host, so the same thing is done to the
 * host's own copy of the builder's state. */
void nvnBufferBuilderSetDefaults(void *builder) {
    nvn_buffer_builder_state *b;
    int first = 0;
    pthread_mutex_lock(&s_buffer_lock);
    s_stats.calls++;
    s_stats.builder_calls++;
    b = builder_find(builder, 1);
    if (b) {
        /* The device stays; everything else goes back to nothing. */
        b->memory_pool = NULL;
        b->memory_offset = 0;
        b->size = 0;
    } else {
        s_stats.refused++;
    }
    first = mark_logged(BUF_BUILDER_SET_DEFAULTS);
    pthread_mutex_unlock(&s_buffer_lock);
    if (!b) {
        fprintf(stderr, "[nvn] nvnBufferBuilderSetDefaults: builder=%p: no room for another builder\n", builder);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnBufferBuilderSetDefaults: builder=%p -> the device stays, the rest is cleared\n",
                builder);
    }
    NVN_VOID_RETURN();
}

/* nvnBufferBuilderSetDevice(builder, device).
 *
 * The reference host puts the device into the builder (hle_nvn.c:1767). */
void nvnBufferBuilderSetDevice(void *builder, void *device) {
    nvn_buffer_builder_state *b;
    int first = 0;
    pthread_mutex_lock(&s_buffer_lock);
    s_stats.calls++;
    s_stats.builder_calls++;
    b = builder_find(builder, 1);
    if (b) {
        b->device = device;
    } else {
        s_stats.refused++;
    }
    first = mark_logged(BUF_BUILDER_SET_DEVICE);
    pthread_mutex_unlock(&s_buffer_lock);
    if (!b) {
        fprintf(stderr, "[nvn] nvnBufferBuilderSetDevice: builder=%p: no room for another builder\n", builder);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnBufferBuilderSetDevice: builder=%p device=%p\n", builder, device);
    }
    NVN_VOID_RETURN();
}

/* nvnBufferBuilderSetStorage(builder, pool, offset, size).
 *
 * The reference host records the pool, the offset inside it and the size
 * (hle_nvn.c:1768). The memory itself belongs to the guest; the runtime only
 * records where the guest put it. */
void nvnBufferBuilderSetStorage(void *builder, void *memory_pool, ptrdiff_t memory_offset, size_t size) {
    nvn_buffer_builder_state *b;
    int first = 0;
    pthread_mutex_lock(&s_buffer_lock);
    s_stats.calls++;
    s_stats.builder_calls++;
    b = builder_find(builder, 1);
    if (b) {
        b->memory_pool = memory_pool;
        b->memory_offset = memory_offset;
        b->size = size;
    } else {
        s_stats.refused++;
    }
    first = mark_logged(BUF_BUILDER_SET_STORAGE);
    pthread_mutex_unlock(&s_buffer_lock);
    if (!b) {
        fprintf(stderr, "[nvn] nvnBufferBuilderSetStorage: builder=%p: no room for another builder\n", builder);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnBufferBuilderSetStorage: builder=%p pool=%p offset=%" PRIdPTR " size=%zu\n", builder,
                memory_pool, memory_offset, size);
    }
    NVN_VOID_RETURN();
}

/* nvnBufferBuilderGetDevice.
 *
 * The signature of this entry point is not in the vendored header this runtime
 * was built against, and the reference tree lists it among the names it can
 * only give a stub (nvn_protos.h:506, "Names without a prototype in the
 * header"). The arguments are therefore taken as plain words and written down
 * as they came, the way nvnTextureBuilderGetDevice does it. Nothing but the
 * first word is dereferenced, so a signature with fewer or more arguments
 * cannot do any harm here.
 *
 * The answer is the device the guest put into this builder, not zero. The
 * signature is not confirmed, but a getter that hands back a null pointer is
 * the one answer that can crash the guest if it dereferences the result, and
 * the device the guest itself set is the only value this can give that is
 * both safe and true. The log says that the signature is not confirmed. */
void nvnBufferBuilderGetDevice(uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4,
                               uint64_t arg5, uint64_t arg6, uint64_t arg7) {
    void *device = NULL;
    nvn_buffer_builder_state *b;
    int first = 0;
    pthread_mutex_lock(&s_buffer_lock);
    s_stats.calls++;
    s_stats.builder_calls++;
    s_stats.gets++;
    b = builder_find((void *)(uintptr_t)arg0, 0);
    if (b) device = b->device;
    first = mark_logged(BUF_BUILDER_GET_DEVICE);
    pthread_mutex_unlock(&s_buffer_lock);
    if (first) {
        fprintf(stderr, "[nvn] nvnBufferBuilderGetDevice: args=0x%" PRIx64 ",0x%" PRIx64 ",0x%" PRIx64 ",0x%" PRIx64
                        ",0x%" PRIx64 ",0x%" PRIx64 ",0x%" PRIx64 ",0x%" PRIx64
                        "; the signature of this entry point is not in the header this runtime was built against, "
                        "so the arguments are recorded as they came and the device of the builder is returned\n",
                arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7);
    }
    NVN_SET_RESULT((uint64_t)(uintptr_t)device);
}

/* nvnBufferBuilderGetSize(builder) -> size_t.
 *
 * The reference host answers with the size of the storage the builder was
 * given (hle_nvn.c:2022). */
size_t nvnBufferBuilderGetSize(const void *builder) {
    nvn_buffer_builder_state *b;
    size_t size = 0;
    int first = 0;
    pthread_mutex_lock(&s_buffer_lock);
    s_stats.calls++;
    s_stats.builder_calls++;
    s_stats.gets++;
    b = builder_find((void *)(uintptr_t)builder, 0);
    if (b) size = b->size;
    first = mark_logged(BUF_BUILDER_GET_SIZE);
    pthread_mutex_unlock(&s_buffer_lock);
    if (first) {
        fprintf(stderr, "[nvn] nvnBufferBuilderGetSize: builder=%p size=%zu\n", builder, size);
    }
    return size;
}

/* nvnBufferBuilderGetMemoryOffset(builder) -> ptrdiff_t.
 *
 * The reference host answers with the offset of the storage inside the pool
 * (hle_nvn.c:2023). */
ptrdiff_t nvnBufferBuilderGetMemoryOffset(const void *builder) {
    nvn_buffer_builder_state *b;
    ptrdiff_t offset = 0;
    int first = 0;
    pthread_mutex_lock(&s_buffer_lock);
    s_stats.calls++;
    s_stats.builder_calls++;
    s_stats.gets++;
    b = builder_find((void *)(uintptr_t)builder, 0);
    if (b) offset = b->memory_offset;
    first = mark_logged(BUF_BUILDER_GET_MEMORY_OFFSET);
    pthread_mutex_unlock(&s_buffer_lock);
    if (first) {
        fprintf(stderr, "[nvn] nvnBufferBuilderGetMemoryOffset: builder=%p offset=%" PRIdPTR "\n", builder, offset);
    }
    return offset;
}

/* nvnBufferBuilderGetMemoryPool(builder) -> the pool.
 *
 * The vendored header writes the return of this one as the pool by value
 * (nvn.h:1238), and the reference host does not implement it at all. A pool
 * is 256 opaque bytes (nvn.h:153-155), so a value of that shape cannot be
 * handed to the guest through a pointer, and the sibling getter
 * nvnBufferGetMemoryPool (nvn.h:1248) returns a pointer. The pointer is
 * therefore what this answers, and the pool the guest put into the builder is
 * the pool that is named.
 *
 * This is one of the three names of this family that the reference host does
 * not implement, so the value is not claimed to agree with it. */
void *nvnBufferBuilderGetMemoryPool(const void *builder) {
    nvn_buffer_builder_state *b;
    void *pool = NULL;
    int first = 0;
    pthread_mutex_lock(&s_buffer_lock);
    s_stats.calls++;
    s_stats.builder_calls++;
    s_stats.gets++;
    b = builder_find((void *)(uintptr_t)builder, 0);
    if (b) pool = b->memory_pool;
    first = mark_logged(BUF_BUILDER_GET_MEMORY_POOL);
    pthread_mutex_unlock(&s_buffer_lock);
    if (first) {
        fprintf(stderr, "[nvn] nvnBufferBuilderGetMemoryPool: builder=%p pool=%p\n", builder, pool);
    }
    return pool;
}

/* ------------------------------------------------------------------ */
/* nvnBufferInitialize                                                   */
/* ------------------------------------------------------------------ */

/* nvnBufferInitialize(buffer, builder) -> boolean.
 *
 * The reference host copies the description of the builder into the buffer and
 * answers true (hle_nvn.c:1769). Here the copy goes into the host's own
 * table, keyed by the address of the buffer, because the guest's buffer
 * object has a layout this runtime does not know.
 *
 * The reference host refuses a builder whose pool has no memory
 * (hle_nvn.c:1770): a buffer without storage is not a buffer. Here the pool's
 * memory is asked of the memory pool family, and a pool that was never
 * initialized has none, so the call answers false and the log says why.
 *
 * The reference host names a buffer that reaches past the end of its pool
 * (hle_nvn.c:1773) and still makes it. Here the same thing happens: the
 * buffer is made and the log says that it reaches past the end of its pool. */
int nvnBufferInitialize(void *buffer, const void *builder) {
    nvn_buffer_builder_state *b = NULL;
    nvn_buffer_state *slot = NULL;
    void *device = NULL;
    void *pool = NULL;
    void *mapped = NULL;
    ptrdiff_t offset = 0;
    size_t size = 0;
    uint64_t pool_size = 0;
    int ready = 0;
    const char *why = NULL;
    int first = 0;

    /* The description of the builder is copied out before the lock of this
     * family is held, and the memory pool family is asked before the lock is
     * taken again: the memory pool family has its own lock, and this family
     * must not hold its own lock while it calls into that one. */
    if (builder) {
        pthread_mutex_lock(&s_buffer_lock);
        b = builder_find((void *)(uintptr_t)builder, 0);
        if (b) {
            pool = b->memory_pool;
            device = b->device;
            offset = b->memory_offset;
            size = b->size;
        }
        pthread_mutex_unlock(&s_buffer_lock);
        mapped = memory_pool_map(pool);
        pool_size = memory_pool_size(pool);
    }

    pthread_mutex_lock(&s_buffer_lock);
    s_stats.calls++;
    if (!buffer) {
        why = "there is no buffer to initialize";
    } else if (!b) {
        why = "the guest did not prepare this builder";
    } else if (!pool) {
        why = "the builder has no memory pool";
    } else if (!mapped) {
        why = "the memory pool of the builder has no storage";
    } else {
        slot = buffer_find(buffer, 1);
        if (!slot) {
            why = "no room for another buffer";
        } else {
            slot->buffer = buffer;
            slot->device = device;
            slot->memory_pool = pool;
            slot->memory_offset = offset;
            slot->size = size;
            /* The debug id comes from the device's own debug domain id
             * generator, the same one every other object of this runtime
             * uses. The generator hands out an id from the device's own
             * sequence and ignores its arguments. */
            slot->debug_id = nvn_device_generate_debug_domain_id(slot->device, "nvnBuffer");
            slot->initialized = 1;
            slot->in_use = 1;
            s_stats.initialized++;
            ready = 1;
        }
    }
    if (!ready) s_stats.refused++;
    first = mark_logged(BUF_INITIALIZE);
    pthread_mutex_unlock(&s_buffer_lock);

    if (!ready) {
        fprintf(stderr, "[nvn] nvnBufferInitialize: buffer=%p builder=%p was refused: %s\n", buffer, builder, why);
    } else {
        if ((uint64_t)slot->memory_offset + (uint64_t)slot->size > pool_size) {
            fprintf(stderr, "[nvn] nvnBufferInitialize: buffer=%p reaches past the end of its pool (offset=%" PRIdPTR
                            " size=%zu pool=%" PRIu64 "); the buffer is made anyway, as the reference host does\n",
                    buffer, slot->memory_offset, slot->size, pool_size);
        }
        if (first) {
            fprintf(stderr, "[nvn] nvnBufferInitialize: buffer=%p builder=%p pool=%p offset=%" PRIdPTR " size=%zu -> the "
                            "description of the builder is copied into the buffer's own state\n",
                    buffer, builder, slot->memory_pool, slot->memory_offset, slot->size);
        }
    }
    return ready ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* nvnBufferFinalize                                                     */
/* ------------------------------------------------------------------ */

/* nvnBufferFinalize(buffer).
 *
 * The reference host clears the magic of the buffer (hle_nvn.c:1777). Here
 * the state of the buffer is dropped, so that a getter on that address
 * afterwards says that the buffer was never initialized instead of answering
 * with the values of a buffer that does not exist any more. */
void nvnBufferFinalize(void *buffer) {
    nvn_buffer_state *found;
    int refused = 0;
    int first = 0;
    pthread_mutex_lock(&s_buffer_lock);
    s_stats.calls++;
    found = buffer_find(buffer, 0);
    if (found && found->initialized) {
        found->initialized = 0;
        found->in_use = 0;
        s_stats.finalized++;
    } else {
        s_stats.refused++;
        refused = 1;
    }
    first = mark_logged(BUF_FINALIZE);
    pthread_mutex_unlock(&s_buffer_lock);
    if (refused) {
        fprintf(stderr, "[nvn] nvnBufferFinalize: buffer=%p was never initialized, so there is nothing to finalize\n",
                buffer);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnBufferFinalize: buffer=%p\n", buffer);
    }
    NVN_VOID_RETURN();
}

/* ------------------------------------------------------------------ */
/* nvnBufferSetDebugLabel                                               */
/* ------------------------------------------------------------------ */

/* nvnBufferSetDebugLabel(buffer, label).
 *
 * The reference host throws the label away (hle_nvn.c:1778). Here it is kept
 * on the host with the rest of the state of the buffer, so that the log can
 * name the buffer the guest is working with.
 *
 * The label is only recorded for a buffer that exists, because a label on an
 * address the guest never initialized would take a slot of the table away from
 * a buffer that does exist. The reference drops the label in every case, so
 * nothing the guest can observe changes. */
void nvnBufferSetDebugLabel(void *buffer, const char *label) {
    nvn_buffer_state *found;
    int refused = 0;
    int first = 0;
    pthread_mutex_lock(&s_buffer_lock);
    s_stats.calls++;
    found = buffer_find(buffer, 0);
    if (found && found->initialized) {
        snprintf(found->debug_label, sizeof(found->debug_label), "%s", label ? label : "");
    } else {
        s_stats.refused++;
        refused = 1;
    }
    first = mark_logged(BUF_SET_DEBUG_LABEL);
    pthread_mutex_unlock(&s_buffer_lock);
    if (refused) {
        fprintf(stderr, "[nvn] nvnBufferSetDebugLabel: buffer=%p was never initialized, so the label was not recorded\n",
                buffer);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnBufferSetDebugLabel: buffer=%p \"%s\"\n", buffer, label ? label : "");
    }
    NVN_VOID_RETURN();
}

/* ------------------------------------------------------------------ */
/* The getters                                                          */
/* ------------------------------------------------------------------ */

/* Every getter answers with the buffer's own values. A buffer that was never
 * initialized has none, so the getter says so and answers with the defaults:
 * a defined answer that the guest cannot mistake for a value of this buffer,
 * because the log names the refusal. */

/* nvnBufferGetDebugID(buffer) -> uint64_t.
 *
 * The vendored header has the signature (nvn.h:1251) and the reference host
 * does not implement it, so the reference gives no value for it. The answer
 * here is the id this runtime hands out for the object, taken from the
 * device's debug domain id generator (nvnDeviceGenerateDebugDomainId), which
 * is the same source every other object of this runtime uses for its debug
 * ids. A buffer that was never initialized has no id, so the answer is zero
 * and the log says that the buffer does not exist. */
uint64_t nvnBufferGetDebugID(const void *buffer) {
    nvn_buffer_state *found;
    uint64_t debug_id = 0;
    int refused = 0;
    int first = 0;
    pthread_mutex_lock(&s_buffer_lock);
    s_stats.calls++;
    s_stats.gets++;
    found = buffer_find((void *)(uintptr_t)buffer, 0);
    if (found && found->initialized) {
        debug_id = found->debug_id;
    } else {
        s_stats.refused++;
        refused = 1;
    }
    first = mark_logged(BUF_GET_DEBUG_ID);
    pthread_mutex_unlock(&s_buffer_lock);
    if (refused) {
        fprintf(stderr, "[nvn] nvnBufferGetDebugID: buffer=%p was never initialized, so it has no debug id; the answer "
                        "is 0\n",
                buffer);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnBufferGetDebugID: buffer=%p debug_id=0x%" PRIx64 "\n", buffer, debug_id);
    }
    return debug_id;
}

/* The address of the storage of a buffer, and the pointer the guest can map.
 *
 * The address is the memory of the pool the buffer lives in, plus the offset
 * of the buffer inside it, the way the reference host answers
 * (hle_nvn.c:1779, 1780). It is an address of the guest, so the command
 * buffer's replay can resolve it back into the host's memory through the
 * memory pool family. A buffer whose pool has no memory has no address, and
 * the answer is then zero with the log saying why.
 *
 * The pool is asked for its memory outside the lock of this family: the
 * memory pool family has its own lock. */
static void *buffer_storage(const void *pool, ptrdiff_t memory_offset, uint64_t *out_address) {
    void *mapped;
    uint64_t address;
    if (out_address) *out_address = 0;
    if (!pool) return NULL;
    mapped = memory_pool_map(pool);
    if (!mapped) return NULL;
    address = (uint64_t)(uintptr_t)mapped + (uint64_t)memory_offset;
    if (out_address) *out_address = address;
    return (void *)(uintptr_t)address;
}

/* nvnBufferGetAddress(buffer) -> the address of the storage.
 *
 * The reference host answers with the address of the pool's memory plus the
 * offset of the buffer (hle_nvn.c:1779). */
uint64_t nvnBufferGetAddress(const void *buffer) {
    nvn_buffer_state *found;
    void *pool = NULL;
    ptrdiff_t offset = 0;
    uint64_t address = 0;
    int refused = 0;
    int first = 0;
    pthread_mutex_lock(&s_buffer_lock);
    s_stats.calls++;
    s_stats.gets++;
    found = buffer_find((void *)(uintptr_t)buffer, 0);
    if (found && found->initialized) {
        pool = found->memory_pool;
        offset = found->memory_offset;
    } else {
        refused = 1;
    }
    first = mark_logged(BUF_GET_ADDRESS);
    pthread_mutex_unlock(&s_buffer_lock);
    /* The memory pool family is asked outside the lock of this family: it has
     * its own lock, and this family must not hold its lock while it calls
     * into that one. */
    if (!refused) (void)buffer_storage(pool, offset, &address);
    pthread_mutex_lock(&s_buffer_lock);
    if (address) {
        s_stats.addresses++;
    } else {
        s_stats.refused++;
        refused = 1;
    }
    pthread_mutex_unlock(&s_buffer_lock);
    if (refused) {
        fprintf(stderr, "[nvn] nvnBufferGetAddress: buffer=%p has no address, because its pool has no memory; the "
                        "answer is 0\n",
                buffer);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnBufferGetAddress: buffer=%p address=0x%" PRIx64 "\n", buffer, address);
    }
    return address;
}

/* nvnBufferMap(buffer) -> the pointer to the storage.
 *
 * The reference host answers with the address of the pool's memory plus the
 * offset of the buffer (hle_nvn.c:1780). */
void *nvnBufferMap(const void *buffer) {
    nvn_buffer_state *found;
    void *pool = NULL;
    ptrdiff_t offset = 0;
    void *mapped = NULL;
    int refused = 0;
    int first = 0;
    pthread_mutex_lock(&s_buffer_lock);
    s_stats.calls++;
    s_stats.gets++;
    s_stats.maps++;
    found = buffer_find((void *)(uintptr_t)buffer, 0);
    if (found && found->initialized) {
        pool = found->memory_pool;
        offset = found->memory_offset;
    } else {
        refused = 1;
    }
    first = mark_logged(BUF_MAP);
    pthread_mutex_unlock(&s_buffer_lock);
    /* The memory pool family is asked outside the lock of this family: it has
     * its own lock, and this family must not hold its lock while it calls
     * into that one. */
    if (!refused) mapped = buffer_storage(pool, offset, NULL);
    pthread_mutex_lock(&s_buffer_lock);
    if (mapped) {
        s_stats.addresses++;
    } else {
        s_stats.refused++;
        refused = 1;
    }
    pthread_mutex_unlock(&s_buffer_lock);
    if (refused) {
        fprintf(stderr, "[nvn] nvnBufferMap: buffer=%p has no memory to map, because its pool has none; the answer is "
                        "NULL\n",
                buffer);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnBufferMap: buffer=%p -> %p\n", buffer, mapped);
    }
    return mapped;
}

/* nvnBufferGetSize(buffer) -> size_t.
 *
 * The reference host answers with the size of the storage (hle_nvn.c:1781). */
size_t nvnBufferGetSize(const void *buffer) {
    nvn_buffer_state *found;
    size_t size = 0;
    int refused = 0;
    int first = 0;
    pthread_mutex_lock(&s_buffer_lock);
    s_stats.calls++;
    s_stats.gets++;
    found = buffer_find((void *)(uintptr_t)buffer, 0);
    if (found && found->initialized) {
        size = found->size;
    } else {
        s_stats.refused++;
        refused = 1;
    }
    first = mark_logged(BUF_GET_SIZE);
    pthread_mutex_unlock(&s_buffer_lock);
    if (refused) {
        fprintf(stderr, "[nvn] nvnBufferGetSize: buffer=%p was never initialized; the answer is 0\n", buffer);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnBufferGetSize: buffer=%p size=%zu\n", buffer, size);
    }
    return size;
}

/* nvnBufferGetMemoryPool(buffer) -> the pool.
 *
 * The reference host answers with the pool the buffer was made from
 * (hle_nvn.c:1782). */
void *nvnBufferGetMemoryPool(const void *buffer) {
    nvn_buffer_state *found;
    void *pool = NULL;
    int refused = 0;
    int first = 0;
    pthread_mutex_lock(&s_buffer_lock);
    s_stats.calls++;
    s_stats.gets++;
    found = buffer_find((void *)(uintptr_t)buffer, 0);
    if (found && found->initialized) {
        pool = found->memory_pool;
    } else {
        s_stats.refused++;
        refused = 1;
    }
    first = mark_logged(BUF_GET_MEMORY_POOL);
    pthread_mutex_unlock(&s_buffer_lock);
    if (refused) {
        fprintf(stderr, "[nvn] nvnBufferGetMemoryPool: buffer=%p was never initialized; the answer is NULL\n", buffer);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnBufferGetMemoryPool: buffer=%p pool=%p\n", buffer, pool);
    }
    return pool;
}

/* nvnBufferGetMemoryOffset(buffer) -> ptrdiff_t.
 *
 * The reference host answers with the offset of the buffer inside its pool
 * (hle_nvn.c:1783). */
ptrdiff_t nvnBufferGetMemoryOffset(const void *buffer) {
    nvn_buffer_state *found;
    ptrdiff_t offset = 0;
    int refused = 0;
    int first = 0;
    pthread_mutex_lock(&s_buffer_lock);
    s_stats.calls++;
    s_stats.gets++;
    found = buffer_find((void *)(uintptr_t)buffer, 0);
    if (found && found->initialized) {
        offset = found->memory_offset;
    } else {
        s_stats.refused++;
        refused = 1;
    }
    first = mark_logged(BUF_GET_MEMORY_OFFSET);
    pthread_mutex_unlock(&s_buffer_lock);
    if (refused) {
        fprintf(stderr, "[nvn] nvnBufferGetMemoryOffset: buffer=%p was never initialized; the answer is 0\n", buffer);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnBufferGetMemoryOffset: buffer=%p offset=%" PRIdPTR "\n", buffer, offset);
    }
    return offset;
}

/* ------------------------------------------------------------------ */
/* The flush and the invalidate                                          */
/* ------------------------------------------------------------------ */

/* nvnBufferFlushMappedRange(buffer, offset, size).
 *
 * The reference host records this as an empty operation and says why
 * (hle_nvn.c:1784): *unified memory: nothing to flush*. The memory of a
 * buffer belongs to the guest and the guest writes into it with its own CPU
 * instructions, so there is no host side cache to push out: the memory is
 * already coherent. The call is answered as done and the log says why,
 * because pretending that a cache operation happened when none was needed
 * would hide a real problem later. */
void nvnBufferFlushMappedRange(const void *buffer, ptrdiff_t offset, size_t size) {
    nvn_buffer_state *found;
    int known = 0;
    pthread_mutex_lock(&s_buffer_lock);
    s_stats.calls++;
    s_stats.flushes++;
    found = buffer_find((void *)(uintptr_t)buffer, 0);
    known = found != NULL && found->initialized;
    pthread_mutex_unlock(&s_buffer_lock);
    fprintf(stderr, "[nvn] nvnBufferFlushMappedRange: buffer=%p offset=%" PRIdPTR " size=%zu initialized=%d; the "
                    "storage is host memory the guest writes itself, so it is already coherent\n",
            buffer, offset, size, known);
    NVN_VOID_RETURN();
}

/* nvnBufferInvalidateMappedRange(buffer, offset, size).
 *
 * The reference host records this as an empty operation as well
 * (hle_nvn.c:1785), for the same reason as the flush. */
void nvnBufferInvalidateMappedRange(const void *buffer, ptrdiff_t offset, size_t size) {
    nvn_buffer_state *found;
    int known = 0;
    pthread_mutex_lock(&s_buffer_lock);
    s_stats.calls++;
    s_stats.invalidates++;
    found = buffer_find((void *)(uintptr_t)buffer, 0);
    known = found != NULL && found->initialized;
    pthread_mutex_unlock(&s_buffer_lock);
    fprintf(stderr, "[nvn] nvnBufferInvalidateMappedRange: buffer=%p offset=%" PRIdPTR " size=%zu initialized=%d; the "
                    "storage is host memory the guest writes itself, so there is no stale cache to drop\n",
            buffer, offset, size, known);
    NVN_VOID_RETURN();
}

/* ------------------------------------------------------------------ */
/* The names                                                            */
/* ------------------------------------------------------------------ */

/* The eighteen names the guest itself resolves, in the order the guest
 * resolves them in (the device run of 2026-10-25). */
static const nvn_buffer_family_entry s_buffer_family[] = {
    { "nvnBufferBuilderSetDefaults", (void *)(uintptr_t)nvnBufferBuilderSetDefaults },
    { "nvnBufferBuilderSetDevice", (void *)(uintptr_t)nvnBufferBuilderSetDevice },
    { "nvnBufferBuilderSetStorage", (void *)(uintptr_t)nvnBufferBuilderSetStorage },
    { "nvnBufferBuilderGetDevice", (void *)(uintptr_t)nvnBufferBuilderGetDevice },
    { "nvnBufferBuilderGetSize", (void *)(uintptr_t)nvnBufferBuilderGetSize },
    { "nvnBufferBuilderGetMemoryOffset", (void *)(uintptr_t)nvnBufferBuilderGetMemoryOffset },
    { "nvnBufferBuilderGetMemoryPool", (void *)(uintptr_t)nvnBufferBuilderGetMemoryPool },
    { "nvnBufferInitialize", (void *)(uintptr_t)nvnBufferInitialize },
    { "nvnBufferFinalize", (void *)(uintptr_t)nvnBufferFinalize },
    { "nvnBufferSetDebugLabel", (void *)(uintptr_t)nvnBufferSetDebugLabel },
    { "nvnBufferGetDebugID", (void *)(uintptr_t)nvnBufferGetDebugID },
    { "nvnBufferGetAddress", (void *)(uintptr_t)nvnBufferGetAddress },
    { "nvnBufferMap", (void *)(uintptr_t)nvnBufferMap },
    { "nvnBufferGetSize", (void *)(uintptr_t)nvnBufferGetSize },
    { "nvnBufferGetMemoryPool", (void *)(uintptr_t)nvnBufferGetMemoryPool },
    { "nvnBufferGetMemoryOffset", (void *)(uintptr_t)nvnBufferGetMemoryOffset },
    { "nvnBufferFlushMappedRange", (void *)(uintptr_t)nvnBufferFlushMappedRange },
    { "nvnBufferInvalidateMappedRange", (void *)(uintptr_t)nvnBufferInvalidateMappedRange },
};

size_t nvn_buffer_family_count(void) {
    return sizeof(s_buffer_family) / sizeof(s_buffer_family[0]);
}

const nvn_buffer_family_entry *nvn_buffer_family_entry_at(size_t index) {
    if (index >= nvn_buffer_family_count()) return NULL;
    return &s_buffer_family[index];
}

void nvn_buffer_get_stats(nvn_buffer_stats *out) {
    size_t i;
    if (!out) return;
    pthread_mutex_lock(&s_buffer_lock);
    *out = s_stats;
    out->live_buffers = 0;
    out->live_builders = 0;
    for (i = 0; i < NVN_BUFFER_MAX_BUFFERS; i++) {
        if (s_buffers[i].in_use && s_buffers[i].initialized) out->live_buffers++;
    }
    for (i = 0; i < NVN_BUFFER_MAX_BUILDERS; i++) {
        if (s_builders[i].in_use) out->live_builders++;
    }
    pthread_mutex_unlock(&s_buffer_lock);
}
