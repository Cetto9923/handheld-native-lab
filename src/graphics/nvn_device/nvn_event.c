/* The NVN event family implementation.
 *
 * See nvn_event.h for why the state lives on the host and what an event is.
 *
 * The guest hands an event builder to nvnEventInitialize and from then on the
 * event holds a copy of the builder's description. The copy is taken at the
 * moment of the initialization, so an event is a snapshot: the builder can
 * change afterwards without touching the event that was already made.
 *
 * CPU and GPU access the same uint32_t in the guest's pool, as in the
 * reference host (hle_nvn.c:649-654). Initialization retains its existing
 * value. CPU accesses and lifecycle changes first resolve pending GPU writes
 * through mtl_event_read, without holding the object-table lock.
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
#include "nvn_obj_ffrs.h"

#include "nvn_device.h"

#include "nvn_memory_pool.h"
#include "nvn_object_table.h"

#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
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
    uint64_t value_address;
    uint64_t generation;
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
static uint64_t s_event_generation;
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

/* A component without a GPU may omit this hook. A running production engine
 * may not: its adapter provides a strong mtl_event_read definition. */
#if defined(__APPLE__)
/* Mach-O requires weak definitions rather than unresolved weak references.
 * The actual engine's strong definition replaces this no-GPU readiness. */
__attribute__((weak)) int nvn_engine_ready(void) { return 0; }
#else
extern int nvn_engine_ready(void) __attribute__((weak));
#endif

/* A strong adapter definition replaces this no-GPU component fallback.
 * Missing integration is fatal once a real engine is ready, never a silent
 * skip of pending GPU results. */
__attribute__((weak)) void mtl_event_read(const volatile uint32_t *address) {
    (void)address;
#if defined(__APPLE__)
    int ready = nvn_engine_ready();
#else
    int ready = nvn_engine_ready && nvn_engine_ready();
#endif
    if (ready) {
        fprintf(stderr, "[nvn] event: running GPU engine has no event completion hook\n");
        abort();
    }
}

static void complete_value(uint64_t address) {
    mtl_event_read((const volatile uint32_t *)(uintptr_t)address);
}

/* Never retain an interior object-table pointer across an unlock: another
 * event creation can grow/reallocate that table. Pool lifetime must extend
 * through its event users, just as for the recorded GPU commands. */
static int event_snapshot(const void *event, nvn_event_state *out) {
    int found = 0;
    pthread_mutex_lock(&s_event_lock);
    nvn_event_state *entry = event_find((void *)(uintptr_t)event, 0);
    if (entry && entry->initialized) { *out = *entry; found = 1; }
    pthread_mutex_unlock(&s_event_lock);
    return found;
}

static int storage_address(const nvn_event_state *event, uint64_t *out) {
    nvn_memory_pool_info pool;
    if (!event->memory_pool || event->memory_offset < 0 ||
        !nvn_memory_pool_info_of(event->memory_pool, &pool) || !pool.mem) return 0;
    uint64_t offset = (uint64_t)event->memory_offset;
    uintptr_t base = (uintptr_t)pool.mem;
    if (offset > pool.size || pool.size - offset < sizeof(uint32_t) ||
        offset > UINTPTR_MAX - base || UINTPTR_MAX - (base + offset) < sizeof(uint32_t) - 1) return 0;
    uint64_t address = base + offset;
    if ((address & (sizeof(uint32_t) - 1)) ||
        (event->value_address && event->value_address != address)) return 0;
    *out = address;
    return 1;
}

static int event_current_locked(const nvn_event_state *snapshot) {
    nvn_event_state *entry = event_find(snapshot->event, 0);
    return entry && entry->initialized && entry->generation == snapshot->generation &&
        entry->value_address == snapshot->value_address;
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

/* Reference hle_nvn.c:646-650 binds the existing pool word; it never
 * initializes that word to zero. Old/new pending writes are completed before
 * rebinding an existing event, without holding this family's lock. */
int nvnEventInitialize(void *event, const void *builder) {
    nvn_event_state next = {0}, previous = {0};
    uint64_t address = 0, previous_address = 0;
    int prepared = 0, had_previous = event_snapshot(event, &previous), result = 0;
    pthread_mutex_lock(&s_event_lock);
    s_stats.calls++;
    nvn_event_builder_state *b = builder_find((void *)(uintptr_t)builder, 0);
    if (b) { next.memory_pool = b->memory_pool; next.memory_offset = b->memory_offset; prepared = 1; }
    pthread_mutex_unlock(&s_event_lock);
    if (!event || !prepared || !storage_address(&next, &address)) goto refused;
    if (had_previous) {
        if (!storage_address(&previous, &previous_address)) goto refused;
        complete_value(previous_address);
    }
    if (!had_previous || address != previous_address) complete_value(address);
    /* The completion hook can reenter table queries and create other events.
     * Re-find entries after it, and refuse a concurrent lifecycle change. */
    pthread_mutex_lock(&s_event_lock);
    nvn_event_state *entry = event_find(event, 0);
    if ((had_previous && !event_current_locked(&previous)) ||
        (!had_previous && entry && entry->initialized) || s_event_generation == UINT64_MAX) {
        s_stats.refused++;
    } else {
        entry = event_find(event, 1);
        if (!entry) s_stats.refused++;
        else {
            next.event = event; next.value_address = address; next.generation = ++s_event_generation;
            next.initialized = next.in_use = 1; *entry = next;
            s_stats.initialized++; result = 1;
        }
    }
    mark_logged(EVENT_INITIALIZE);
    pthread_mutex_unlock(&s_event_lock);
    if (!result) fprintf(stderr, "[nvn] nvnEventInitialize: concurrent lifecycle change or allocation failure event=%p\n", event);
    return result;
refused:
    pthread_mutex_lock(&s_event_lock); s_stats.refused++; pthread_mutex_unlock(&s_event_lock);
    fprintf(stderr, "[nvn] nvnEventInitialize: invalid builder/storage/range/alignment event=%p builder=%p\n", event, builder);
    return 0;
}

void nvnEventFinalize(void *event) {
    nvn_event_state snapshot; uint64_t address = 0;
    int valid = event_snapshot(event, &snapshot) && storage_address(&snapshot, &address);
    if (valid) complete_value(address);
    pthread_mutex_lock(&s_event_lock);
    s_stats.calls++; s_stats.finalized++;
    if (valid && event_current_locked(&snapshot)) {
        nvn_event_state *entry = event_find(event, 0); memset(entry, 0, sizeof(*entry));
    } else s_stats.refused++;
    mark_logged(EVENT_FINALIZE);
    pthread_mutex_unlock(&s_event_lock);
    if (!valid) fprintf(stderr, "[nvn] nvnEventFinalize: unknown event or unavailable storage event=%p\n", event);
    NVN_VOID_RETURN();
}

void nvnEventSignal(void *event, int mode, uint32_t value) {
    nvn_event_state snapshot; uint64_t address = 0;
    int valid = event_snapshot(event, &snapshot) && storage_address(&snapshot, &address);
    if (valid) complete_value(address);
    pthread_mutex_lock(&s_event_lock);
    s_stats.calls++; s_stats.signals++;
    if (valid && event_current_locked(&snapshot)) {
        uint32_t *word = (uint32_t *)(uintptr_t)address;
        if (mode == NVN_EVENT_SIGNAL_MODE_ADD) __atomic_fetch_add(word, value, __ATOMIC_SEQ_CST);
        else __atomic_store_n(word, value, __ATOMIC_SEQ_CST);
    } else { valid = 0; s_stats.refused++; }
    mark_logged(EVENT_SIGNAL);
    pthread_mutex_unlock(&s_event_lock);
    if (!valid) fprintf(stderr, "[nvn] nvnEventSignal: unknown event or unavailable storage event=%p\n", event);
    NVN_VOID_RETURN();
}

uint32_t nvnEventGetValue(const void *event) {
    nvn_event_state snapshot; uint64_t address = 0; uint32_t value = 0;
    int valid = event_snapshot(event, &snapshot) && storage_address(&snapshot, &address);
    if (valid) complete_value(address);
    pthread_mutex_lock(&s_event_lock);
    s_stats.calls++; s_stats.gets++;
    if (valid && event_current_locked(&snapshot)) value = __atomic_load_n((uint32_t *)(uintptr_t)address, __ATOMIC_SEQ_CST);
    else { valid = 0; s_stats.refused++; }
    mark_logged(EVENT_GET_VALUE);
    pthread_mutex_unlock(&s_event_lock);
    if (!valid) fprintf(stderr, "[nvn] nvnEventGetValue: unknown event or unavailable storage event=%p\n", event);
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

/* The recorder obtains the same validated pool word used by CPU APIs. This
 * does not resolve GPU work: it records the future GPU signal address. */
int nvn_event_value_address_of(const void *event, uint64_t *address) {
    nvn_event_state snapshot; uint64_t value;
    if (!address || !event_snapshot(event, &snapshot) || !storage_address(&snapshot, &value)) return 0;
    pthread_mutex_lock(&s_event_lock);
    int current = event_current_locked(&snapshot);
    pthread_mutex_unlock(&s_event_lock);
    if (!current) return 0;
    *address = value;
    return 1;
}
