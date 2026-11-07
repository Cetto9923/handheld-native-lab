/* The NVN sync object family of the FFRS RG477M native runtime.
 *
 * A sync object is the guest's handle on a fence. The guest makes one with
 * nvnSyncInitialize, the queue marks it as fenced with nvnQueueFenceSync,
 * and the guest waits on it with nvnSyncWait or with nvnQueueWaitSync. It is
 * the carrier of the ordering between the submissions of two queues, and it
 * is the first thing the guest reaches for on the device: the device run of
 * 2026-10-25 ended on "unimplemented NVN entry \"nvnSyncInitialize\" was
 * called (trap slot 21)", so this family is the one that turns the first
 * trap of the run into an answered call.
 *
 * The state lives on the host and is keyed by the address of the guest's
 * sync object, exactly as the sampler, the texture, the queue and the window
 * families do. Nothing is ever written into the guest's sync object: the
 * layout of an NVNsync is a property of the Nintendo SDK, which this runtime
 * does not have. The reference host writes a magic number into it
 * (.local/refs/nxnative/dm/gfx/nvn/hle_nvn.c:627, where the sync_t is
 * written into the guest's object); this runtime writes nothing, and the
 * contract proves that with a 0xAA sentinel over the guest's own memory.
 *
 * A sync is a snapshot of the moment it was fenced. The fence value that is
 * recorded when the queue fences it is the value at that moment and it does
 * not move afterwards: a later fence of another sync does not change it.
 *
 * Where the evidence comes from:
 *
 *   - The signatures of nvnSyncInitialize, nvnSyncFinalize,
 *     nvnSyncSetDebugLabel and nvnSyncWait are the ones of the vendored NVN
 *     header (src/graphics/nvn/include/nvn/nvn.h:1741-1745), which is a
 *     reconstruction of NVN 53.105 taken from another game (Darksiders II),
 *     so they are NOT confirmed for this game, which is Unreal Engine
 *     5.6.1. The reference host that runs this very game agrees with them
 *     (.local/refs/nxnative/dm/gfx/nvn/nvn_protos.h:468-472).
 *   - The guest's own call sites agree as well, and they are the strongest
 *     evidence there is, because they are the game itself:
 *       - nvnSyncInitialize is called with the sync object in x0 and the
 *         device in x1 (the guest image at the module offsets 0x142d728,
 *         0x144a970, 0x144a984, 0x144a998 and 0x144a9ac; the call at
 *         0x142d728 is the one the device run of 2026-10-25 trapped).
 *       - nvnSyncFinalize is called with the sync object in x0 and nothing
 *         else (the guest image at 0x1444150, 0x14448fc and 0x144b024).
 *       - nvnSyncWait is called with the sync object in x0 and a timeout in
 *         x1, and the timeout the guest passes is -1, that is "wait
 *         forever" (the guest image at 0x142cb30, 0x142cbb0, 0x143d0e0,
 *         0x1443cb0 and 0x14560c8).
 *       - The guest reads the answer of nvnSyncWait as a wait result: it
 *         compares it with 2 and with 1 (the guest image at 0x142cb34,
 *         "cmp w0, #2", and at 0x142cbb4, "cmp w0, #1"), which are the
 *         values of NVN_SYNC_WAIT_RESULT_TIMEOUT_EXPIRED and of the two
 *         results below it in the vendored header (nvn.h:1041-1048). The
 *         answer of the wait is therefore the enumeration of the vendored
 *         header and not a boolean.
 *       - nvnSyncSetDebugLabel, nvnSyncCreateGLSync and
 *         nvnSyncInitializeFromFencedGLSync have no call site in the guest
 *         image: the guest resolves them by name and never calls them.
 *         Their shape comes from the vendored header and the reference host
 *         (the first one) and is this runtime's own (the other two).
 *   - The two GL names, nvnSyncCreateGLSync and
 *     nvnSyncInitializeFromFencedGLSync, are in NEITHER the vendored header
 *     NOR the reference host's implementation. The reference host has them
 *     in its names table (.local/refs/nxnative/dm/gfx/nvn/names.txt:501-502)
 *     and lists them under the names that have no prototype
 *     (nvn_protos.h:515-516), so the reference gives them a named stub and
 *     no behaviour to copy. Their shape here is this runtime's own and is
 *     written down in nvn_sync.c.
 *   - The behaviour of the wait is the one of the reference host
 *     (hle_nvn.c:634 and 638-641): a sync that has no fence has waited until
 *     its timeout, and the wait of the queue answers 1 always.
 */
#ifndef FFRS_NVN_SYNC_H
#define FFRS_NVN_SYNC_H

#include <stddef.h>
#include <stdint.h>

#include "nvn_obj_ffrs.h"

/* How many sync objects the runtime tracks at the same time.
 *
 * The table grows with the number of sync objects the guest has, so this is a
 * limit and not a guess: it is above the number of syncs a real device keeps
 * live, so a guest that runs out of them is told so instead of being served
 * silently. */
#define NVN_SYNC_MAX_OBJECTS 65536
/* Retained recorded-command generations. Stable 4096-object slabs consume
 * at most16MiB of sync payload plus2KiB of slab pointers. Allocation failure
 * or exhaustion refuses the new generation; an existing generation survives.
 * Old recorded pointers live until process exit, because this family does
 * not own command-package retirement. No per-frame generation is required
 * merely to fence an existing initialized sync. */
#define NVN_SYNC_MAX_GENERATIONS 1048576u

/* The names of the family, so that the device dispatch can answer them
 * without a second table. */
typedef struct nvn_sync_family_entry {
    const char *name;
    void *fn;
} nvn_sync_family_entry;

size_t nvn_sync_family_count(void);
const nvn_sync_family_entry *nvn_sync_family_entry_at(size_t index);

/* ------------------------------------------------------------------ */
/* The seam with the queue family                                       */
/* ------------------------------------------------------------------ */

/* Mark a sync object as fenced.
 *
 * The queue family owns the moment of the fence (it knows what was
 * submitted), and this family owns the object (it knows whether it is
 * fenced and what value it was fenced with). The value is this runtime's own
 * sequence number of fences: the host has no GPU fence to hand out, because
 * the replay reads the packages and issues no command, so there is nothing
 * that could be signalled. The log says so.
 *
 * A sync object that the guest never initialized can still be fenced: the
 * queue may fence a sync the guest has not made yet, and refusing that
 * would drop a fence the guest asked for. */
void nvn_sync_family_fence(void *sync);

/* Mark a sync object as fenced via its internal object pointer. */
void nvn_sync_family_fence_object(ffrs_sync *object);

/* Stable current-generation object for deferred replay. Table growth never
 * moves it. Reinitialize publishes a different zero-fence generation;
 * Finalize removes the key lookup but retains previously recorded pointers.
 * This lifetime protection does not make premature guest Finalize valid NVN
 * synchronization, nor does it signal any GPU work by itself. */
ffrs_sync *nvn_sync_family_object(const void *sync);
/* Managed reference for a command recording/package. The pointer stays valid
 * until the matching release, even if the guest reinitializes/finalizes. */
ffrs_sync *nvn_sync_family_managed_acquire(const void *sync,void **token);
void nvn_sync_family_managed_release(void *token);

/* Whether a sync object has a fence. A sync object that does not exist has
 * none. */
int nvn_sync_family_is_fenced(const void *sync);

/* The value a sync object was fenced with, or zero when it has no fence.
 * The value is a snapshot: it does not move when another sync is fenced
 * afterwards. */
uint64_t nvn_sync_family_fence_value(const void *sync);

/* Wait for a sync object, on behalf of a queue.
 *
 * This returns 1 when the sync object has a fence and 0 when it has none.
 * The wait of the queue answers 1 to the guest either way (the reference
 * tree does the same, hle_nvn.c:634), and the answer of this function is
 * what the queue uses to count the waits that had no fence.
 *
 * *logged is set to 1 when THIS call is the one that logged the missing
 * fence. The log of a missing fence happens once for the whole run, as in
 * the reference tree, where it is a static flag (hle_nvn.c:634). */
int nvn_sync_family_wait(const void *sync, int *logged);

/* Statistics for the contract. */
typedef struct nvn_sync_stats {
    uint64_t calls;                /* every entry point of this family */
    uint64_t initialized;          /* a sync object was initialized */
    uint64_t finalized;            /* a sync object was finalized */
    uint64_t waits;                /* waits on a sync object */
    uint64_t waits_without_fence;  /* waits on a sync object with no fence */
    uint64_t waits_without_fence_logged; /* the ones that were logged (once) */
    uint64_t fences_taken;         /* sync objects that were fenced */
    uint64_t refused;              /* the call could not be answered */
    int live_syncs;                /* sync objects the table holds */
    uint64_t retained_generations; /* current plus old recorded generations */
    uint64_t snapshot_bytes;       /* actual allocated stable slab payload */
    uint64_t generation_limit;     /* explicit bounded retention policy */
} nvn_sync_stats;

void nvn_sync_get_stats(nvn_sync_stats *out);

#endif /* FFRS_NVN_SYNC_H */
