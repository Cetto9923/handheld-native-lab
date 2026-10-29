/* The NVN queue family of the FFRS RG477M native runtime.
 *
 * The guest builds a queue with the builder functions and then initializes it.
 * Neither the queue object nor the builder is read by the guest field by
 * field, so this layer keeps the whole state on the host: the guest's
 * addresses are the keys, and nothing is ever written into guest memory.
 * That is deliberate. The layout of an NVN queue object is a property of the
 * Nintendo SDK, which this runtime does not have, so writing into the guest's
 * queue would risk corrupting it.
 *
 * The entry points follow the NVN API the guest resolves by name. Their
 * signatures are the ones the guest's own call sites use; the runtime log
 * confirms them at run time.
 */
#ifndef FFRS_NVN_QUEUE_H
#define FFRS_NVN_QUEUE_H

#include <stddef.h>
#include <stdint.h>

/* The names of the family, so that the device dispatch can answer them
 * without a second table. */
typedef struct nvn_queue_family_entry {
    const char *name;
    void *fn;
} nvn_queue_family_entry;

size_t nvn_queue_family_count(void);
const nvn_queue_family_entry *nvn_queue_family_entry_at(size_t index);

/* ------------------------------------------------------------------ */
/* The submission seam                                                  */
/* ------------------------------------------------------------------ */

/* Hand the recorded commands of the guest to the replay.
 *
 * This is the seam between the two halves of the runtime. The guest records
 * commands into its command buffers, the recorder turns each recording into
 * an immutable package, and the guest hands the handles of those packages
 * over here. The reference tree runs the package at this point
 * (.local/refs/nxnative/dm/gfx/nvn/hle_nvn.c L1132, where the handle
 * h[i] is what is executed); this runtime's replay reads the package
 * instead of executing it.
 *
 * The handle is never dereferenced. It goes through the registry of the
 * recorder, and a handle the registry does not know is refused by name and
 * counted: it is not skipped silently and it is never counted as submitted.
 *
 * Nothing is handed to Vulkan here. The replay decodes the package and
 * stops there; the commands it reads are not submitted yet, and the log
 * says so.
 *
 * The signature is the one of the vendored NVN header:
 * PFNNVNQUEUESUBMITCOMMANDSPROC = void (*)(NVNqueue*, int, const NVNcommandHandle*),
 * with NVNcommandHandle being a uint64_t.
 *
 * The state of the queue is on the host and is keyed by the guest's address.
 * Nothing is written into the guest's queue object and nothing is written
 * into the array of handles. */
void nvn_queue_submit_commands(void *queue, int count, const uint64_t *handles);

/* Ask for a window's texture to be shown.
 *
 * The present path is not connected yet: it belongs to the card that owns
 * the swapchain (P-MM-28). This call therefore records the request - which
 * window and which index - and says that nothing was presented. It does not
 * report a success it cannot have.
 *
 * The signature is the one of the vendored NVN header:
 * PFNNVNQUEUEPRESENTTEXTUREPROC = void (*)(NVNqueue*, NVNwindow*, int). */
void nvn_queue_present_texture(void *queue, void *window, int index);

/* Mark a sync object as fenced.
 *
 * The sync family does not exist yet (it is the next card), so the state of
 * a sync object lives in this family's own table, keyed by the guest's
 * address. That table is to be merged into the sync family when that family
 * arrives; until then this is the only place that knows about a sync.
 *
 * The fence value that is recorded is this runtime's own submission counter,
 * not a GPU fence: the host has no fence to hand out yet, because the
 * replay does not submit anything to the GPU. The log says so.
 *
 * The signature is the one of the vendored NVN header:
 * PFNNVNQUEUEFENCESYNCPROC = void (*)(NVNqueue*, NVNsync*, NVNsyncCondition, int). */
void nvn_queue_fence_sync(void *queue, void *sync, int condition, int flags);

/* Wait for a sync object.
 *
 * This always answers 1, exactly as the reference tree does
 * (.local/refs/nxnative/dm/gfx/nvn/hle_nvn.c L634): the host has a single
 * submission queue, the commands before the fence are done first, and a sync
 * that is fenced later by another thread is not waited for. A sync that has
 * no fence yet is logged once - once for the whole run, as in the reference -
 * and the wait does not block.
 *
 * The signature is the one of the vendored NVN header:
 * PFNNVNQUEUEWAITSYNCPROC = NVNboolean (*)(NVNqueue*, const NVNsync*). */
int nvn_queue_wait_sync(void *queue, const void *sync);

/* The memory the builder was given for the queue.
 *
 * This name is in neither the vendored NVN header nor the reference tree:
 * the header has nvnQueueBuilderGetQueueMemorySize and the reference tree
 * has that name too, but neither has nvnQueueBuilderGetMemory. The guest
 * asks for it by name all the same (the guest transcript of P-MM-33 shows
 * it as a name the guest resolves). Its shape follows this runtime's own
 * convention for the builder getters, and the same memory that
 * nvnQueueBuilderSetQueueMemory was given comes back.
 *
 * This runtime's own naming already treats nvnQueueBuilderGetMemorySize and
 * nvnQueueBuilderGetQueueMemorySize as the same question asked twice, so
 * this name is answered the same way the queue memory getter answers it. */
void *nvn_queue_builder_get_memory(const void *builder);

/* Statistics for the contract. */
typedef struct nvn_queue_stats {
    uint64_t builder_calls;
    uint64_t initialized;
    uint64_t finalized;
    uint64_t refused;
    uint64_t platform_queue_acquired;
    /* What the guest asked the builder for, not what a queue uses. */
    uint64_t command_memory_requested;
    uint64_t compute_memory_requested;
    uint64_t control_memory_requested;
    int live_queues;
    int platform_queue_present;

    /* The submission seam. */
    uint64_t submit_calls;             /* calls into nvnQueueSubmitCommands */
    uint64_t packages_handed_to_replay; /* packages the replay was given */
    uint64_t packages_refused;          /* handles with no package at all */
    uint64_t records_read;              /* records the replay decoded for us */
    uint64_t replay_refusals;           /* refusals the replay reported */
    uint64_t presents_requested;        /* present requests that were recorded */
    uint64_t presents_presented;        /* the images that reached the screen */
    uint64_t presents_performed;        /* presents that really happened: none */
    uint64_t fences_taken;              /* sync objects that were fenced */
    uint64_t waits;                     /* waits on a sync object */
    uint64_t waits_without_fence;       /* waits on a sync object with no fence */
    uint64_t waits_without_fence_logged;/* the ones that were logged (once) */
    int live_syncs;                     /* sync objects the table holds */
} nvn_queue_stats;

void nvn_queue_get_stats(nvn_queue_stats *out);

#endif /* FFRS_NVN_QUEUE_H */
