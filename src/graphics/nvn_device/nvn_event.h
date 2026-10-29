/* The NVN event family of the FFRS RG477M native runtime.
 *
 * An event is the signal between the GPU and the CPU. The guest fills an
 * event builder with the storage it wants (a memory pool and an offset inside
 * it), hands it to nvnEventInitialize, and from then on the event is an object
 * that can be signalled (nvnEventSignal) and whose value can be read back
 * (nvnEventGetValue). It is the other half of the synchronisation story: the
 * sync family carries a fence and a wait, the event family carries a value and
 * a signal.
 *
 * The state lives on the host and is keyed by the address of the guest's
 * object, exactly as the queue, the window, the memory pool, the pool
 * families, the blend state, the render state, the sampler families, the
 * texture families and the buffer family do. Nothing is ever written into the
 * guest's event object or into the guest's event builder: the layout of an
 * NVNevent and of an NVNeventBuilder is a property of the Nintendo SDK, which
 * this runtime does not have. The contract proves that with a 0xAA sentinel
 * over the guest's own memory.
 *
 * An event is a snapshot of the builder. The description is copied at the
 * moment of nvnEventInitialize; a later change of the builder does not change
 * an event that was already made, and every getter answers with the event's
 * own values.
 *
 * The value lives in the memory pool at the builder's offset, shared by CPU
 * and GPU accesses. Initialization preserves that word. Reads, CPU signals
 * and lifecycle changes resolve pending GPU writes before touching it. Only
 * this validated four-byte word may change; opaque event/builder bytes and
 * the surrounding pool bytes remain untouched.
 *
 * Where the evidence comes from:
 *
 *   - the signatures of the six names that the vendored NVN header has
 *     (src/graphics/nvn/include/nvn/nvn.h:1747-1752) are the ones of that
 *     header. That header is a reconstruction of NVN 53.105 taken from
 *     another game (Darksiders II), so the signatures are NOT confirmed for
 *     this game.
 *   - the semantics are the ones of the reference host that runs this very
 *     game (.local/refs/nxnative/dm/gfx/nvn/hle_nvn.c, MIT): the builder is
 *     cleared (644), the storage is recorded (645), an initialization without
 *     storage is refused (646-650), a finalize releases the object (651), the
 *     value is read back (652) and a signal writes it or adds to it (653-654).
 *   - the five names that the vendored header does not have are named in the
 *     report of this family and are not claimed to agree with it.
 */
#ifndef FFRS_NVN_EVENT_H
#define FFRS_NVN_EVENT_H

#include <stddef.h>
#include <stdint.h>

#include "nvn_obj_ffrs.h"

/* How many events the runtime tracks at the same time. The guest keeps its
 * events in a pool, so a handful is enough; a guest that wants more is told
 * so instead of being served silently. The table grows with the number of
 * events that are live, so this is a cap and not a size of the table. */
#define NVN_EVENT_MAX_EVENTS 65536

/* How many event builders the runtime tracks at the same time. The guest makes
 * a builder, fills it, initializes an event with it and reuses the builder for
 * the next event, so a few are enough. The table grows with the number of
 * builders that are live, so this is a cap and not a size of the table. */
#define NVN_EVENT_MAX_BUILDERS 4096

/* The eleven names of this family. The builder's five names and the event's
 * six names live in one table, because the guest resolves them as one group
 * and the coordinator wires one family block for them. */
typedef struct nvn_event_family_entry {
    const char *name;
    void *fn;
} nvn_event_family_entry;

size_t nvn_event_family_count(void);
const nvn_event_family_entry *nvn_event_family_entry_at(size_t index);

/* Where the value of an event lives, for the replay layer that writes it.
 * Returns 0 for unknown events, unavailable pools or invalid four-byte storage. */
int nvn_event_value_address_of(const void *event, uint64_t *address);

/* Statistics for the contract. */
typedef struct nvn_event_stats {
    uint64_t calls;        /* every entry point of the event family */
    uint64_t builder_calls; /* every entry point of the builder part */
    uint64_t initialized;  /* an event was made out of a builder */
    uint64_t finalized;    /* an event was finalized */
    uint64_t signals;      /* an event was signalled */
    uint64_t gets;         /* a getter ran */
    uint64_t refused;      /* the call could not be answered */
    int live_events;       /* events the guest has initialized */
    int live_builders;     /* builders the guest has prepared */
} nvn_event_stats;

void nvn_event_get_stats(nvn_event_stats *out);

#endif /* FFRS_NVN_EVENT_H */
