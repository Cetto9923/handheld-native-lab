/* The NVN event family implementation.
 *
 * See nvn_event.h for why the state lives on the host and what an event is.
 *
 * The guest hands an event builder to nvnEventInitialize and from then on the
 * event holds a copy of the builder's description. The copy is taken at the
 * moment of the initialization, so an event is a snapshot: the builder can
 * change afterwards without touching the event that was already made.
 *
 * The value of an event is kept on the host, beside the rest of the event's
 * state. The reference host puts it into the guest's pool memory, at the
 * offset of the event inside the pool (hle_nvn.c:649), because its GPU writes
 * it there. This runtime does not write into the guest's memory at all, so the
 * value stays here; the contract proves that the guest's pool memory is
 * untouched by a signal.
 *
 * The evidence for the values:
 *
 *   - The signatures of the six names that the vendored NVN header has are the
 *     ones of that header (src/graphics/nvn/include/nvn/nvn.h:1747-1752),
 *     which is a reconstruction of NVN 53.105 taken from another game
 *     (Darksiders II). Its signatures are therefore NOT confirmed for this
 *     game, which is Unreal Engine 5.6.1.
 *   - The description that is copied is the one of the reference host's
 *     ev_builder_t (hle_nvn.c:644-645): the memory pool and the offset inside
 *     it.
 *   - The defaults are the ones of the reference host (hle_nvn.c:644): the
 *     builder is cleared, so it has no storage.
 *   - The refusal of an initialization without storage is the one of the
 *     reference host (hle_nvn.c:648): a builder whose pool has no memory
 *     cannot make an event, so the call answers false.
 *   - The value of a signal is the one of the reference host (hle_nvn.c:653):
 *     the mode ADD adds to the value, every other mode writes it.
 *
 * Five of the eleven names have no signature in the vendored header and no
 * implementation in the reference host. They are named in the report of this
 * family, and the way they are answered is named here:
 *
 *   - nvnEventBuilderGetStorage, nvnEventBuilderGetMemoryPool and
 *     nvnEventBuilderGetMemoryOffset are the getters of the pair that
 *     nvnEventBuilderSetStorage writes (nvn.h:1748). The reference tree lists
 *     all three among the names it can only give a stub (nvn_protos.h:517-519),
 *     and the vendored header has no prototype for any of them.
 *     nvnEventBuilderGetMemoryPool and nvnEventBuilderGetMemoryOffset answer
 *     the pool and the offset, the way nvnBufferBuilderGetMemoryPool (pointer)
 *     and nvnBufferBuilderGetMemoryOffset (ptrdiff_t) do in the buffer family.
 *   - nvnEventBuilderGetStorage answers with the pool, in the register the ABI
 *     returns a value in. The storage of an event builder is the pair (pool,
 *     offset) and the two halves are already available through the two getters
 *     above, so the pool is the one half that can be handed back in a single
 *     register. A getter that wrote the pair through out-parameters would write
 *     into the guest's memory, which this runtime does not do.
 *   - nvnEventGetMemoryPool and nvnEventGetMemoryOffset answer the same pair
 *     for the event, the way nvnBufferGetMemoryPool (nvn.h:1248) and
 *     nvnBufferGetMemoryOffset (nvn.h:1249) do for a buffer.
 */
#include "nvn_event.h"

#include "nvn_device.h"

#include "nvn_memory_pool.h"
#include "nvn_object_table.h"

#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

/* The signal modes of the vendored header (nvn.h:661-665). The reference host
 * compares the mode against ADD and writes the value for every other mode
 * (hle_nvn.c:653). */
#define NVN_EVENT_SIGNAL_MODE_WRITE 0
#define NVN_EVENT_SIGNAL_MODE_ADD 1

typedef struct {
    void *builder;
    void *memory_pool;
    int64_t memory_offset;
    int in_use;
} nvn_event_builder_state;

typedef struct {
    void *event;
    void *memory_pool;
    int64_t memory_offset;
    uint32_t value;
    int signaled;
    int initialized;
    int in_use;
} nvn_event_state;

/* The names of this family, in the order the guest itself resolves them in
 * (the device run of 2026-10-25, .local/logs/p-mm-44-guest-transcript.log,
 * trap slots 29 to 39). The index is used for the "only the first call is
 * logged" bookkeeping. */
enum {
    EVENT_BUILDER_SET_DEFAULTS,
    EVENT_BUILDER_SET_STORAGE,
    EVENT_BUILDER_GET_STORAGE,
    EVENT_BUILDER_GET_MEMORY_POOL,
    EVENT_BUILDER_GET_MEMORY_OFFSET,
    EVENT_INITIALIZE,
    EVENT_FINALIZE,
    EVENT_GET_VALUE,
    EVENT_SIGNAL,
    EVENT_GET_MEMORY_POOL,
    EVENT_GET_MEMORY_OFFSET,
    EVENT_NAME_COUNT
};

static pthread_mutex_t s_event_lock = PTHREAD_MUTEX_INITIALIZER;
static nvn_object_table s_builders = { NULL, 0, 0, sizeof(nvn_event_builder_state) };
static nvn_object_table s_events = { NULL, 0, 0, sizeof(nvn_event_state) };
static nvn_event_stats s_stats;
static int s_logged[EVENT_NAME_COUNT];

/* The state of a builder, keyed by the guest's builder address. A builder that
 * was never prepared has no state, and the functions that need one say so
 * instead of inventing one. */
static nvn_event_builder_state *builder_find(void *builder, int create) {
    size_t i;
    nvn_event_builder_state *free_slot = NULL;
    if (!builder) return NULL;
    for (i = 0; i < s_builders.count; i++) {
        nvn_event_builder_state *entry = (nvn_event_builder_state *)nvn_object_table_at(&s_builders, i);
        if (!entry) continue;
        if (entry->in_use) {
            if (entry->builder == builder) return entry;
        } else if (!free_slot) {
            free_slot = entry;
        }
    }
    if (!create) return NULL;
    if (!free_slot) {
        free_slot = (nvn_event_builder_state *)nvn_object_table_new_entry(&s_builders, NVN_EVENT_MAX_BUILDERS);
        if (!free_slot) return NULL;
    }
    memset(free_slot, 0, sizeof(*free_slot));
    free_slot->builder = builder;
    free_slot->in_use = 1;
    return free_slot;
}

/* The state of an event, keyed by the guest's event address. */
static nvn_event_state *event_find(void *event, int create) {
    size_t i;
    nvn_event_state *free_slot = NULL;
    if (!event) return NULL;
    for (i = 0; i < s_events.count; i++) {
        nvn_event_state *entry = (nvn_event_state *)nvn_object_table_at(&s_events, i);
        if (!entry) continue;
        if (entry->in_use) {
            if (entry->event == event) return entry;
        } else if (!free_slot) {
            free_slot = entry;
        }
    }
    if (!create) return NULL;
    if (!free_slot) {
        free_slot = (nvn_event_state *)nvn_object_table_new_entry(&s_events, NVN_EVENT_MAX_EVENTS);
        if (!free_slot) return NULL;
    }
    memset(free_slot, 0, sizeof(*free_slot));
    free_slot->event = event;
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

/* The entry point of the memory pool family, found by its name. The event
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
 * This is the same thing as the reference host's pool->mem (hle_nvn.c:648), and
 * it is what decides whether an event can be made at all: the reference host
 * refuses an initialization whose pool has no memory. */
static void *memory_pool_map(const void *pool) {
    typedef void *(*pool_map_fn)(const void *);
    pool_map_fn fn = (pool_map_fn)(uintptr_t)memory_pool_entry("nvnMemoryPoolMap");
    if (!fn || !pool) return NULL;
    return fn(pool);
}

/* ------------------------------------------------------------------ */
/* The builder                                                          */
/* ------------------------------------------------------------------ */

/* nvnEventBuilderSetDefaults(builder).
 *
 * The reference host clears the whole builder (hle_nvn.c:644), so it has no
 * storage any more. The state here is on the host, so the same thing is done
 * to the host's own copy of the builder's state. */
void nvnEventBuilderSetDefaults(void *builder) {
    nvn_event_builder_state *b;
    int first = 0;
    pthread_mutex_lock(&s_event_lock);
    s_stats.calls++;
    s_stats.builder_calls++;
    b = builder_find(builder, 1);
    if (b) {
        b->memory_pool = NULL;
        b->memory_offset = 0;
    } else {
        s_stats.refused++;
    }
    first = mark_logged(EVENT_BUILDER_SET_DEFAULTS);
    pthread_mutex_unlock(&s_event_lock);
    if (!b) {
        fprintf(stderr, "[nvn] nvnEventBuilderSetDefaults: builder=%p: no room for another builder\n", builder);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnEventBuilderSetDefaults: builder=%p -> the storage is cleared\n", builder);
    }
    NVN_VOID_RETURN();
}

/* nvnEventBuilderSetStorage(builder, pool, offset).
 *
 * The reference host records the pool and the offset inside it
 * (hle_nvn.c:645). The memory itself belongs to the guest; the runtime only
 * records where the guest put it. The offset is an int64_t in the vendored
 * header (nvn.h:1748). */
void nvnEventBuilderSetStorage(void *builder, void *memory_pool, int64_t memory_offset) {
    nvn_event_builder_state *b;
    int first = 0;
    pthread_mutex_lock(&s_event_lock);
    s_stats.calls++;
    s_stats.builder_calls++;
    b = builder_find(builder, 1);
    if (b) {
        b->memory_pool = memory_pool;
        b->memory_offset = memory_offset;
    } else {
        s_stats.refused++;
    }
    first = mark_logged(EVENT_BUILDER_SET_STORAGE);
    pthread_mutex_unlock(&s_event_lock);
    if (!b) {
        fprintf(stderr, "[nvn] nvnEventBuilderSetStorage: builder=%p: no room for another builder\n", builder);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnEventBuilderSetStorage: builder=%p pool=%p offset=%" PRId64 "\n", builder, memory_pool,
                memory_offset);
    }
    NVN_VOID_RETURN();
}

/* nvnEventBuilderGetStorage(builder) -> the pool.
 *
 * This name has no signature in the vendored header and no implementation in
 * the reference host (nvn_protos.h:517). The storage of an event builder is
 * the pair (pool, offset) that nvnEventBuilderSetStorage writes, and the two
 * halves are answered by the two getters below, so the pool is the half that
 * can be handed back in a single register. A getter that wrote the pair
 * through out-parameters would write into the guest's memory, which this
 * runtime does not do. */
void *nvnEventBuilderGetStorage(const void *builder) {
    nvn_event_builder_state *b;
    void *pool = NULL;
    int first = 0;
    int refused = 0;
    pthread_mutex_lock(&s_event_lock);
    s_stats.calls++;
    s_stats.builder_calls++;
    s_stats.gets++;
    b = builder_find((void *)(uintptr_t)builder, 0);
    if (b) {
        pool = b->memory_pool;
    } else {
        refused = 1;
        s_stats.refused++;
    }
    first = mark_logged(EVENT_BUILDER_GET_STORAGE);
    pthread_mutex_unlock(&s_event_lock);
    if (refused) {
        fprintf(stderr, "[nvn] nvnEventBuilderGetStorage: builder=%p was never prepared; the answer is NULL\n", builder);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnEventBuilderGetStorage: builder=%p pool=%p; this name has no signature in the header "
                        "this runtime was built against, so the pool is the half that fits in one register\n",
                builder, pool);
    }
    return pool;
}

/* nvnEventBuilderGetMemoryPool(builder) -> the pool.
 *
 * The memory pool the guest put into the builder. The vendored header writes
 * the return of the buffer's builder getter as the pool by value (nvn.h:1238),
 * which is 256 opaque bytes and cannot be handed to the guest through a
 * pointer; the sibling getter nvnBufferGetMemoryPool (nvn.h:1248) returns a
 * pointer, and so does this one. */
void *nvnEventBuilderGetMemoryPool(const void *builder) {
    nvn_event_builder_state *b;
    void *pool = NULL;
    int first = 0;
    int refused = 0;
    pthread_mutex_lock(&s_event_lock);
    s_stats.calls++;
    s_stats.builder_calls++;
    s_stats.gets++;
    b = builder_find((void *)(uintptr_t)builder, 0);
    if (b) {
        pool = b->memory_pool;
    } else {
        refused = 1;
        s_stats.refused++;
    }
    first = mark_logged(EVENT_BUILDER_GET_MEMORY_POOL);
    pthread_mutex_unlock(&s_event_lock);
    if (refused) {
        fprintf(stderr, "[nvn] nvnEventBuilderGetMemoryPool: builder=%p was never prepared; the answer is NULL\n", builder);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnEventBuilderGetMemoryPool: builder=%p pool=%p\n", builder, pool);
    }
    return pool;
}

/* nvnEventBuilderGetMemoryOffset(builder) -> the offset.
 *
 * The offset of the storage inside the pool, the way nvnBufferBuilderGetMemoryOffset
 * (nvn.h:1239) answers it for a buffer. */
ptrdiff_t nvnEventBuilderGetMemoryOffset(const void *builder) {
    nvn_event_builder_state *b;
    ptrdiff_t offset = 0;
    int first = 0;
    int refused = 0;
    pthread_mutex_lock(&s_event_lock);
    s_stats.calls++;
    s_stats.builder_calls++;
    s_stats.gets++;
    b = builder_find((void *)(uintptr_t)builder, 0);
    if (b) {
        offset = (ptrdiff_t)b->memory_offset;
    } else {
        refused = 1;
        s_stats.refused++;
    }
    first = mark_logged(EVENT_BUILDER_GET_MEMORY_OFFSET);
    pthread_mutex_unlock(&s_event_lock);
    if (refused) {
        fprintf(stderr, "[nvn] nvnEventBuilderGetMemoryOffset: builder=%p was never prepared; the answer is 0\n", builder);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnEventBuilderGetMemoryOffset: builder=%p offset=%" PRIdPTR "\n", builder, offset);
    }
    return offset;
}

/* ------------------------------------------------------------------ */
/* The event                                                            */
/* ------------------------------------------------------------------ */

/* nvnEventInitialize(event, builder) -> NVNboolean.
 *
 * The reference host refuses an initialization whose builder has no storage or
 * whose pool has no memory (hle_nvn.c:646-650), and otherwise makes the event
 * out of the builder's description. The description is copied here as well, so
 * that an event is a snapshot of the builder it was made from.
 *
 * The value of the new event is zero: the reference host reads it out of the
 * guest's pool memory, which holds whatever the guest left there, and this
 * runtime keeps the value on the host, where there is nothing yet. */
int nvnEventInitialize(void *event, const void *builder) {
    nvn_event_builder_state *b = NULL;
    nvn_event_state *e = NULL;
    void *pool = NULL;
    int64_t offset = 0;
    int result = 0;
    int first = 0;
    const char *why = NULL;

    /* The description of the builder is copied out before the lock of this
     * family is held, and the memory pool family is asked before the lock is
     * taken again: the memory pool family has its own lock, and this family
     * must not hold its own lock while it calls into that one. */
    if (builder) {
        pthread_mutex_lock(&s_event_lock);
        b = builder_find((void *)(uintptr_t)builder, 0);
        if (b) {
            pool = b->memory_pool;
            offset = b->memory_offset;
        }
        pthread_mutex_unlock(&s_event_lock);
    }
    /* The pool has to have memory, or the event has nowhere to live. This is
     * the refusal of the reference host (hle_nvn.c:648). */
    if (pool && !memory_pool_map(pool)) {
        pool = NULL;
    }

    pthread_mutex_lock(&s_event_lock);
    s_stats.calls++;
    if (!event) {
        why = "there is no event to initialize";
    } else if (!b) {
        why = "the guest did not prepare this builder";
    } else if (!pool) {
        why = "the builder has no storage";
    } else {
        e = event_find(event, 1);
        if (!e) {
            why = "no room for another event";
        } else {
            e->memory_pool = pool;
            e->memory_offset = offset;
            e->value = 0;
            e->signaled = 0;
            e->initialized = 1;
            s_stats.initialized++;
            result = 1;
        }
    }
    if (!result) s_stats.refused++;
    first = mark_logged(EVENT_INITIALIZE);
    pthread_mutex_unlock(&s_event_lock);

    if (!result) {
        fprintf(stderr, "[nvn] nvnEventInitialize: event=%p builder=%p was refused: %s\n", event, builder, why);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnEventInitialize: event=%p builder=%p pool=%p offset=%" PRId64
                        " -> the description of the builder is copied into the event's own state\n",
                event, builder, pool, offset);
    }
    return result;
}

/* nvnEventFinalize(event).
 *
 * The reference host clears the object (hle_nvn.c:651). The object here is
 * the guest's own memory, which this runtime does not write, so the event is
 * released here instead: its record is marked free and the slot is ready for
 * the next event. */
void nvnEventFinalize(void *event) {
    nvn_event_state *e;
    int first = 0;
    pthread_mutex_lock(&s_event_lock);
    s_stats.calls++;
    s_stats.finalized++;
    e = event_find(event, 0);
    if (e) {
        memset(e, 0, sizeof(*e));
    } else {
        s_stats.refused++;
    }
    first = mark_logged(EVENT_FINALIZE);
    pthread_mutex_unlock(&s_event_lock);
    if (!e) {
        fprintf(stderr, "[nvn] nvnEventFinalize: event=%p was never initialized\n", event);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnEventFinalize: event=%p -> released\n", event);
    }
    NVN_VOID_RETURN();
}

/* nvnEventSignal(event, mode, value).
 *
 * The reference host adds the value to the event's value when the mode is ADD
 * and writes it for every other mode (hle_nvn.c:653-654). The value is kept
 * on the host here, so the guest's pool memory is not touched. */
void nvnEventSignal(void *event, int mode, uint32_t value) {
    nvn_event_state *e;
    int first = 0;
    pthread_mutex_lock(&s_event_lock);
    s_stats.calls++;
    s_stats.signals++;
    e = event_find(event, 0);
    if (e) {
        if (mode == NVN_EVENT_SIGNAL_MODE_ADD) {
            e->value += value;
        } else {
            e->value = value;
        }
        e->signaled = 1;
    } else {
        s_stats.refused++;
    }
    first = mark_logged(EVENT_SIGNAL);
    pthread_mutex_unlock(&s_event_lock);
    if (!e) {
        fprintf(stderr, "[nvn] nvnEventSignal: event=%p was never initialized; the signal is refused\n", event);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnEventSignal: event=%p mode=%d value=0x%" PRIx32 "\n", event, mode, value);
    }
    NVN_VOID_RETURN();
}

/* nvnEventGetValue(event) -> the value of the event.
 *
 * The reference host reads the value out of the guest's pool memory
 * (hle_nvn.c:652). Here the value is the one the host kept, so a signal is
 * read back as it was given. */
uint32_t nvnEventGetValue(const void *event) {
    nvn_event_state *e;
    uint32_t value = 0;
    int first = 0;
    int refused = 0;
    pthread_mutex_lock(&s_event_lock);
    s_stats.calls++;
    s_stats.gets++;
    e = event_find((void *)(uintptr_t)event, 0);
    if (e) {
        value = e->value;
    } else {
        refused = 1;
        s_stats.refused++;
    }
    first = mark_logged(EVENT_GET_VALUE);
    pthread_mutex_unlock(&s_event_lock);
    if (refused) {
        fprintf(stderr, "[nvn] nvnEventGetValue: event=%p was never initialized; the answer is 0\n", event);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnEventGetValue: event=%p value=0x%" PRIx32 "\n", event, value);
    }
    return value;
}

/* nvnEventGetMemoryPool(event) -> the pool.
 *
 * The pool the event was made with, the way nvnBufferGetMemoryPool
 * (nvn.h:1248) answers it for a buffer. */
void *nvnEventGetMemoryPool(const void *event) {
    nvn_event_state *e;
    void *pool = NULL;
    int first = 0;
    int refused = 0;
    pthread_mutex_lock(&s_event_lock);
    s_stats.calls++;
    s_stats.gets++;
    e = event_find((void *)(uintptr_t)event, 0);
    if (e) {
        pool = e->memory_pool;
    } else {
        refused = 1;
        s_stats.refused++;
    }
    first = mark_logged(EVENT_GET_MEMORY_POOL);
    pthread_mutex_unlock(&s_event_lock);
    if (refused) {
        fprintf(stderr, "[nvn] nvnEventGetMemoryPool: event=%p was never initialized; the answer is NULL\n", event);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnEventGetMemoryPool: event=%p pool=%p\n", event, pool);
    }
    return pool;
}

/* nvnEventGetMemoryOffset(event) -> the offset.
 *
 * The offset of the event inside its pool, the way nvnBufferGetMemoryOffset
 * (nvn.h:1249) answers it for a buffer. */
ptrdiff_t nvnEventGetMemoryOffset(const void *event) {
    nvn_event_state *e;
    ptrdiff_t offset = 0;
    int first = 0;
    int refused = 0;
    pthread_mutex_lock(&s_event_lock);
    s_stats.calls++;
    s_stats.gets++;
    e = event_find((void *)(uintptr_t)event, 0);
    if (e) {
        offset = (ptrdiff_t)e->memory_offset;
    } else {
        refused = 1;
        s_stats.refused++;
    }
    first = mark_logged(EVENT_GET_MEMORY_OFFSET);
    pthread_mutex_unlock(&s_event_lock);
    if (refused) {
        fprintf(stderr, "[nvn] nvnEventGetMemoryOffset: event=%p was never initialized; the answer is 0\n", event);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnEventGetMemoryOffset: event=%p offset=%" PRIdPTR "\n", event, offset);
    }
    return offset;
}

/* ------------------------------------------------------------------ */
/* The table of the family                                              */
/* ------------------------------------------------------------------ */

/* The eleven names, in the order the guest resolves them in. */
static const nvn_event_family_entry s_entries[] = {
    { "nvnEventBuilderSetDefaults", (void *)(uintptr_t)nvnEventBuilderSetDefaults },
    { "nvnEventBuilderSetStorage", (void *)(uintptr_t)nvnEventBuilderSetStorage },
    { "nvnEventBuilderGetStorage", (void *)(uintptr_t)nvnEventBuilderGetStorage },
    { "nvnEventBuilderGetMemoryPool", (void *)(uintptr_t)nvnEventBuilderGetMemoryPool },
    { "nvnEventBuilderGetMemoryOffset", (void *)(uintptr_t)nvnEventBuilderGetMemoryOffset },
    { "nvnEventInitialize", (void *)(uintptr_t)nvnEventInitialize },
    { "nvnEventFinalize", (void *)(uintptr_t)nvnEventFinalize },
    { "nvnEventGetValue", (void *)(uintptr_t)nvnEventGetValue },
    { "nvnEventSignal", (void *)(uintptr_t)nvnEventSignal },
    { "nvnEventGetMemoryPool", (void *)(uintptr_t)nvnEventGetMemoryPool },
    { "nvnEventGetMemoryOffset", (void *)(uintptr_t)nvnEventGetMemoryOffset },
};

size_t nvn_event_family_count(void) {
    return sizeof(s_entries) / sizeof(s_entries[0]);
}

const nvn_event_family_entry *nvn_event_family_entry_at(size_t index) {
    if (index >= nvn_event_family_count()) return NULL;
    return &s_entries[index];
}

void nvn_event_get_stats(nvn_event_stats *out) {
    size_t i;
    if (!out) return;
    pthread_mutex_lock(&s_event_lock);
    *out = s_stats;
    out->live_events = 0;
    out->live_builders = 0;
    for (i = 0; i < s_events.count; i++) {
        nvn_event_state *entry = (nvn_event_state *)nvn_object_table_at(&s_events, i);
        if (entry && entry->in_use && entry->initialized) out->live_events++;
    }
    for (i = 0; i < s_builders.count; i++) {
        nvn_event_builder_state *entry = (nvn_event_builder_state *)nvn_object_table_at(&s_builders, i);
        if (entry && entry->in_use) out->live_builders++;
    }
    pthread_mutex_unlock(&s_event_lock);
}
