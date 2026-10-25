/* The NVN sync object family implementation.
 *
 * See nvn_sync.h for why the state lives on the host and what a sync is.
 *
 * A sync object is the guest's handle on a fence. The guest makes one with
 * nvnSyncInitialize, the queue marks it as fenced with nvnQueueFenceSync,
 * and the guest waits on it with nvnSyncWait or with nvnQueueWaitSync. The
 * state of the object - whether it has been initialized, whether it has a
 * fence, which value it was fenced with - is kept here, on the host, keyed
 * by the address the guest gave. Nothing is written into the guest's sync
 * object.
 *
 * The evidence for every answer:
 *
 *   - nvnSyncInitialize(sync, device) answers true. The reference host
 *     writes its magic number into the guest's object and answers 1
 *     (.local/refs/nxnative/dm/gfx/nvn/hle_nvn.c:627). Here the object is
 *     made on the host instead and the guest's object is left alone.
 *   - nvnSyncFinalize(sync) drops the object. The reference clears the
 *     magic (hle_nvn.c:628); here the record is dropped, so that a getter
 *     or a wait on that address afterwards says that the sync does not
 *     exist instead of answering with the values of a sync that is gone.
 *   - nvnSyncSetDebugLabel(sync, label) keeps the label on the host. The
 *     reference throws the label away (hle_nvn.c:629). The guest cannot
 *     observe the difference, and the log can name the sync the guest is
 *     working with.
 *   - nvnSyncWait(sync, timeout) answers with the wait result of the
 *     vendored header (NVNsyncWaitResult, src/graphics/nvn/include/nvn/
 *     nvn.h:1041-1048). The reference (hle_nvn.c:638-641) answers
 *     TIMEOUT_EXPIRED for a sync that has no fence, and for a sync that has
 *     one it polls the fence with a zero timeout and answers
 *     ALREADY_SIGNALED when the work is already done, CONDITION_SATISFIED
 *     when the wait inside the timeout finishes it, and TIMEOUT_EXPIRED
 *     when it does not.
 *
 *     The guest reads the answer as that enumeration and not as a boolean:
 *     it compares the answer with 2 and with 1 (the guest image at
 *     0x142cb34, "cmp w0, #2", and at 0x142cbb4, "cmp w0, #1"), so 2 is
 *     the timeout and the two answers below it are the ones that mean the
 *     condition was met. The guest passes -1 as the timeout (the guest
 *     image at 0x142cba4, 0x143d0cc and 0x1443ca0), that is "wait
 *     forever".
 *
 *     Here there is nothing that could be waited for: the replay reads the
 *     packages and issues no command, so no GPU work is pending, and the
 *     work that was submitted before the fence was read on the thread that
 *     submitted it. The zero timeout poll of the reference therefore
 *     succeeds at once, and the answer of a fenced sync is
 *     ALREADY_SIGNALED. The timeout is not waited for: there is no work to
 *     wait for, and sleeping would only slow the guest down. When the
 *     replay starts issuing commands to the GPU, this answer has to become
 *     a real wait; that is the card that owns the submission.
 *
 *   - nvnSyncCreateGLSync(sync) and nvnSyncInitializeFromFencedGLSync(sync,
 *     glSync) are in NEITHER the vendored header NOR the reference host's
 *     implementation. The reference host has the two names in its names
 *     table (.local/refs/nxnative/dm/gfx/nvn/names.txt:501-502) and lists
 *     them under the names that have no prototype
 *     (.local/refs/nxnative/dm/gfx/nvn/nvn_protos.h:515-516): "Names
 *     without a prototype in the header (newer interfaces): they can only
 *     get named stubs". So the reference gives them a stub and no behaviour
 *     to copy, and the shape below is this runtime's own:
 *
 *       nvnSyncCreateGLSync(sync)            -> NVNboolean
 *       nvnSyncInitializeFromFencedGLSync(sync, glSync) -> NVNboolean
 *
 *     The first one is the wrong direction for this runtime to answer: it
 *     would have to make an OpenGL sync object, and there is no OpenGL
 *     context here - the device is a Vulkan device and the guest runs the
 *     NVN API. No GL sync is made, the answer is false, and the log says
 *     why. The second one goes the other way: the guest hands over a GL
 *     sync that it says is fenced. The handle itself is opaque here - it
 *     cannot be inspected without a GL context, and it is never
 *     dereferenced - but what the name asserts is that the sync it comes
 *     from is ALREADY FENCED, and that is what is recorded: the sync object
 *     is initialized and fenced, and a wait on it answers that the
 *     condition is already satisfied.
 *
 *     Both answers are false rather than a success that cannot be had: a
 *     guest that is told the truth can live with it, and a guest that is
 *     told a GL sync exists would use a handle that does not exist.
 */
#include "nvn_sync.h"

#include "nvn_device.h"

#include "../nvn/include/nvn/nvn.h"

#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include "nvn_object_table.h"

typedef struct {
    void *sync;       /* the key: the address the guest gave */
    void *device;     /* the device the guest gave at the initialization */
    uint64_t gl_sync; /* the opaque GL handle, when the sync came from one */
    int has_gl_sync;
    int initialized;
    /* The fence state of this sync object. It is an object of its own and
     * not two fields, because the replay layer writes the fence into it when
     * it reaches the point of the command stream where the guest fenced it:
     * the host and the replay layer have to be looking at the same thing. */
    ffrs_sync object;
    char debug_label[64];
    int in_use;
} nvn_sync_state;

/* The names of this family, in the order the guest itself resolves them
 * (the trap slots 21, 22, 23, 25, 27 and 28 of the device run of
 * 2026-10-25). The index is used for the "only the first call is logged"
 * bookkeeping. */
enum {
    SY_INITIALIZE,
    SY_FINALIZE,
    SY_SET_DEBUG_LABEL,
    SY_WAIT,
    SY_CREATE_GL_SYNC,
    SY_INITIALIZE_FROM_FENCED_GL_SYNC,
    SY_NAME_COUNT
};

static pthread_mutex_t s_sync_lock = PTHREAD_MUTEX_INITIALIZER;
static nvn_object_table s_syncs = { NULL, 0, 0, sizeof(nvn_sync_state) };
static nvn_sync_stats s_stats;
static int s_logged[SY_NAME_COUNT];

/* The host's own fence sequence.
 *
 * The host has no GPU fence to hand out: the replay reads the packages and
 * issues no command, so there is nothing that could be signalled. The value
 * that is recorded when a sync is fenced is therefore a sequence number of
 * this runtime's own, and it says in which order the fences were taken. */
static uint64_t s_fence_sequence;

/* The wait of a sync object that has no fence is logged once for the whole
 * run, not once per sync object. The reference tree does the same with a
 * static flag (hle_nvn.c:634). The flag lives here, because the state of a
 * sync object lives here; the queue family asks for it through
 * nvn_sync_family_wait and prints the line itself, so that the line still
 * names the queue's own entry point. */
static int s_wait_without_fence_logged;

/* The record of one guest sync address. A sync object that was never
 * initialized has no record to answer with, and the functions that need one
 * say so instead of inventing one. */
static nvn_sync_state *sync_find(void *sync, int create) {
    size_t i;
    nvn_sync_state *free_slot = NULL;
    if (!sync) return NULL;
    for (i = 0; i < s_syncs.count; i++) {
        nvn_sync_state *entry = (nvn_sync_state *)nvn_object_table_at(&s_syncs, i);
        if (!entry) continue;
        if (entry->in_use) {
            if (entry->sync == sync) return entry;
        } else if (!free_slot) {
            free_slot = entry;
        }
    }
    if (!create) return NULL;
    if (!free_slot) {
        free_slot = (nvn_sync_state *)nvn_object_table_new_entry(&s_syncs, NVN_SYNC_MAX_OBJECTS);
        if (!free_slot) return NULL;
    }
    memset(free_slot, 0, sizeof(*free_slot));
    free_slot->sync = sync;
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
/* nvnSyncInitialize                                                   */
/* ------------------------------------------------------------------ */

/* nvnSyncInitialize(sync, device) -> boolean.
 *
 * The signature is the one of the vendored NVN header:
 * PFNNVNSYNCINITIALIZEPROC = NVNboolean (*)(NVNsync*, NVNdevice*).
 * The guest's own call site agrees: it puts the sync object in x0 and the
 * device in x1 and calls through the resolved pointer (the guest image at
 * the module offset 0x142d728, the call the device run of 2026-10-25
 * trapped).
 *
 * The record is made on the host and the guest's object is not written. The
 * device is kept with it: a sync belongs to the device it was made on.
 *
 * A sync object that is initialized again starts over: it has no fence
 * until the queue fences it. */
int nvnSyncInitialize(void *sync, void *device) {
    nvn_sync_state *slot = NULL;
    int ready = 0;
    const char *why = NULL;
    int first = 0;

    pthread_mutex_lock(&s_sync_lock);
    s_stats.calls++;
    if (!sync) {
        why = "there is no sync object to initialize";
    } else {
        slot = sync_find(sync, 1);
        if (!slot) {
            why = "no room for another sync object";
        } else {
            slot->device = device;
            slot->initialized = 1;
            memset(&slot->object, 0, sizeof(slot->object));
            slot->object.magic = FFRS_NVN_MAGIC('S', 'Y', 'N', 'C');
            slot->gl_sync = 0;
            slot->has_gl_sync = 0;
            slot->debug_label[0] = '\0';
            s_stats.initialized++;
            ready = 1;
        }
    }
    if (!ready) s_stats.refused++;
    first = mark_logged(SY_INITIALIZE);
    pthread_mutex_unlock(&s_sync_lock);

    if (!ready) {
        fprintf(stderr, "[nvn] nvnSyncInitialize: sync=%p device=%p was refused: %s\n", sync, device, why);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnSyncInitialize: sync=%p device=%p -> the sync object is on the host and the "
                        "guest's object was not written\n",
                sync, device);
    }
    return ready ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* nvnSyncFinalize                                                     */
/* ------------------------------------------------------------------ */

/* nvnSyncFinalize(sync).
 *
 * The signature is the one of the vendored NVN header:
 * PFNNVNSYNCFINALIZEPROC = void (*)(NVNsync*).
 *
 * The reference clears the magic of the object (hle_nvn.c:628). Here the
 * record is dropped, so that a wait on that address afterwards says that
 * the sync does not exist instead of answering with the values of a sync
 * that does not exist any more. */
void nvnSyncFinalize(void *sync) {
    nvn_sync_state *found;
    int refused = 0;
    int first = 0;

    pthread_mutex_lock(&s_sync_lock);
    s_stats.calls++;
    found = sync_find(sync, 0);
    if (found) {
        found->initialized = 0;
        found->object.fenced = 0;
        found->object.fence = 0;
        found->in_use = 0;
        s_stats.finalized++;
    } else {
        s_stats.refused++;
        refused = 1;
    }
    first = mark_logged(SY_FINALIZE);
    pthread_mutex_unlock(&s_sync_lock);

    if (refused) {
        fprintf(stderr, "[nvn] nvnSyncFinalize: sync=%p was never initialized, so there is nothing to finalize\n",
                sync);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnSyncFinalize: sync=%p\n", sync);
    }
    NVN_VOID_RETURN();
}

/* ------------------------------------------------------------------ */
/* nvnSyncSetDebugLabel                                                */
/* ------------------------------------------------------------------ */

/* nvnSyncSetDebugLabel(sync, label).
 *
 * The signature is the one of the vendored NVN header:
 * PFNNVNSYNCSETDEBUGLABELPROC = void (*)(NVNsync*, const char*).
 *
 * The reference throws the label away (hle_nvn.c:629). Here it is kept on
 * the host with the rest of the state of the sync, so that the log can
 * name the sync the guest is working with. Nothing the guest can observe
 * changes.
 *
 * The label is only recorded for a sync that exists, because a label on an
 * address the guest never initialized would take a slot of the table away
 * from a sync that does exist. */
void nvnSyncSetDebugLabel(void *sync, const char *label) {
    nvn_sync_state *found;
    int refused = 0;
    int first = 0;

    pthread_mutex_lock(&s_sync_lock);
    s_stats.calls++;
    found = sync_find(sync, 0);
    if (found && found->initialized) {
        snprintf(found->debug_label, sizeof(found->debug_label), "%s", label ? label : "");
    } else {
        s_stats.refused++;
        refused = 1;
    }
    first = mark_logged(SY_SET_DEBUG_LABEL);
    pthread_mutex_unlock(&s_sync_lock);

    if (refused) {
        fprintf(stderr, "[nvn] nvnSyncSetDebugLabel: sync=%p was never initialized, so the label was not recorded\n",
                sync);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnSyncSetDebugLabel: sync=%p \"%s\"\n", sync, label ? label : "");
    }
    NVN_VOID_RETURN();
}

/* ------------------------------------------------------------------ */
/* nvnSyncWait                                                         */
/* ------------------------------------------------------------------ */

/* nvnSyncWait(sync, timeout) -> NVNsyncWaitResult.
 *
 * The signature is the one of the vendored NVN header:
 * PFNNVNSYNCWAITPROC = NVNsyncWaitResult (*)(const NVNsync*, uint64_t).
 *
 * The answer is the one of the reference (hle_nvn.c:638-641):
 *   - a sync that has no fence has waited until its timeout, so the answer
 *     is TIMEOUT_EXPIRED;
 *   - a sync that has a fence is already done on this host, because the
 *     replay reads the packages on the thread that submits them and issues
 *     no command, so the answer is ALREADY_SIGNALED.
 *
 * A sync object that does not exist has no fence either, so it answers
 * TIMEOUT_EXPIRED, and the log says that the sync does not exist so that
 * the answer cannot be mistaken for one about a sync that does exist. */
NVNsyncWaitResult nvnSyncWait(const void *sync, uint64_t timeout) {
    nvn_sync_state *found;
    int known = 0;
    int fenced = 0;
    int first = 0;
    NVNsyncWaitResult result;

    pthread_mutex_lock(&s_sync_lock);
    s_stats.calls++;
    s_stats.waits++;
    found = sync_find((void *)(uintptr_t)sync, 0);
    if (found) {
        /* The reference looks at the fence of the object and at nothing
         * else (hle_nvn.c:639): a sync that the queue fenced answers the
         * signalled result even when the guest never initialized it, and
         * the queue may fence a sync the guest has not made yet. */
        fenced = found->object.fenced;
        if (found->initialized) known = 1;
    }
    if (!fenced) s_stats.waits_without_fence++;
    result = fenced ? NVN_SYNC_WAIT_RESULT_ALREADY_SIGNALED : NVN_SYNC_WAIT_RESULT_TIMEOUT_EXPIRED;
    first = mark_logged(SY_WAIT);
    pthread_mutex_unlock(&s_sync_lock);

    if (!known) {
        fprintf(stderr, "[nvn] nvnSyncWait: sync=%p was never initialized by the guest; the answer is %d (%s)\n", sync,
                (int)result, fenced ? "already signalled" : "timeout expired");
    } else if (first) {
        fprintf(stderr, "[nvn] nvnSyncWait: sync=%p timeout=%" PRIu64 " -> %d (%s); the host has no GPU work to wait "
                        "for\n",
                sync, timeout, (int)result,
                fenced ? "already signalled" : "timeout expired");
    }
    return result;
}

/* ------------------------------------------------------------------ */
/* nvnSyncCreateGLSync                                                 */
/* ------------------------------------------------------------------ */

/* nvnSyncCreateGLSync(sync) -> boolean.
 *
 * This name is in neither the vendored NVN header nor the reference host's
 * implementation: the reference host lists it under the names that have no
 * prototype (nvn_protos.h:516) and gives it a named stub. The shape here is
 * this runtime's own: the sync object in x0, a boolean in the return.
 *
 * The answer is false. Making a GL sync object needs an OpenGL context,
 * and there is none here: the device of this runtime is a Vulkan device and
 * the guest runs the NVN API. The GL handle is never made up and never
 * handed to the guest, because a guest that believes it holds a GL sync
 * would use a handle that does not exist. */
int nvnSyncCreateGLSync(void *sync) {
    nvn_sync_state *found;
    int known = 0;
    int first = 0;

    pthread_mutex_lock(&s_sync_lock);
    s_stats.calls++;
    found = sync_find(sync, 0);
    if (found && found->initialized) known = 1;
    /* The call is refused either way: there is no OpenGL context, so no GL
     * sync object can be made. The counter says that the call could not be
     * answered. */
    s_stats.refused++;
    first = mark_logged(SY_CREATE_GL_SYNC);
    pthread_mutex_unlock(&s_sync_lock);

    if (!known) {
        fprintf(stderr, "[nvn] nvnSyncCreateGLSync: sync=%p was never initialized, and this runtime has no OpenGL "
                        "context, so no GL sync object was made\n",
                sync);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnSyncCreateGLSync: sync=%p -> false; this runtime has no OpenGL context, so no GL "
                        "sync object was made\n",
                sync);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* nvnSyncInitializeFromFencedGLSync                                   */
/* ------------------------------------------------------------------ */

/* nvnSyncInitializeFromFencedGLSync(sync, glSync) -> boolean.
 *
 * This name is in neither the vendored NVN header nor the reference host's
 * implementation: the reference host lists it under the names that have no
 * prototype (nvn_protos.h:515) and gives it a named stub. The shape here is
 * this runtime's own: the sync object in x0, the GL handle in x1, a
 * boolean in the return.
 *
 * The GL handle is opaque here and is never dereferenced: there is no
 * OpenGL context, so nothing about it can be inspected. What the name
 * asserts is that the sync it comes from is ALREADY FENCED, and that is
 * what is recorded. The sync object is initialized and fenced, and a wait
 * on it answers that the condition is already satisfied.
 *
 * The handle is kept on the host with the rest of the state, so that the
 * log can name it. */
int nvnSyncInitializeFromFencedGLSync(void *sync, void *gl_sync) {
    nvn_sync_state *slot = NULL;
    int ready = 0;
    const char *why = NULL;
    int first = 0;

    pthread_mutex_lock(&s_sync_lock);
    s_stats.calls++;
    if (!sync) {
        why = "there is no sync object to initialize";
    } else {
        slot = sync_find(sync, 1);
        if (!slot) {
            why = "no room for another sync object";
        } else {
            slot->device = NULL;
            slot->initialized = 1;
            slot->object.fenced = 1;
            slot->object.fence = ++s_fence_sequence;
            slot->gl_sync = (uint64_t)(uintptr_t)gl_sync;
            slot->has_gl_sync = 1;
            slot->debug_label[0] = '\0';
            s_stats.initialized++;
            s_stats.fences_taken++;
            ready = 1;
        }
    }
    if (!ready) s_stats.refused++;
    first = mark_logged(SY_INITIALIZE_FROM_FENCED_GL_SYNC);
    pthread_mutex_unlock(&s_sync_lock);

    if (!ready) {
        fprintf(stderr, "[nvn] nvnSyncInitializeFromFencedGLSync: sync=%p gl_sync=%p was refused: %s\n", sync,
                gl_sync, why);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnSyncInitializeFromFencedGLSync: sync=%p gl_sync=%p -> the sync object is "
                        "initialized and fenced with the host's own fence sequence %" PRIu64
                        "; the GL handle is opaque here and was not dereferenced\n",
                sync, gl_sync, slot ? slot->object.fence : 0);
    }
    return ready ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* The seam with the queue family                                      */
/* ------------------------------------------------------------------ */

/* The queue family owns the moment of the fence and this family owns the
 * object. The queue asks for the fence here and reads the state of the
 * object here. */
void nvn_sync_family_fence(void *sync) {
    nvn_sync_state *slot;
    uint64_t fence = 0;
    int refused = 0;

    pthread_mutex_lock(&s_sync_lock);
    slot = sync_find(sync, 1);
    if (!slot) {
        s_stats.refused++;
        refused = 1;
    } else {
        fence = ++s_fence_sequence;
        slot->object.fenced = 1;
        slot->object.fence = fence;
        s_stats.fences_taken++;
    }
    pthread_mutex_unlock(&s_sync_lock);

    if (refused) {
        fprintf(stderr, "[nvn] nvn_sync_family_fence: no room for another sync object; the sync %p was not fenced\n",
                sync);
    }
}

/* The object of a sync, for the replay layer.
 *
 * The replay layer fences a sync by writing the fence value into the object
 * (the reference does the same, hle_nvn.c:7702), so the object has to be
 * the one this family keeps its own state in. A sync that the guest has not
 * made yet is made here: the queue may fence a sync the guest has not made
 * yet, and the fence must not be dropped. */
ffrs_sync *nvn_sync_family_object(const void *sync) {
    nvn_sync_state *slot;
    pthread_mutex_lock(&s_sync_lock);
    slot = sync_find((void *)(uintptr_t)sync, 1);
    if (!slot) {
        pthread_mutex_unlock(&s_sync_lock);
        return NULL;
    }
    if (!slot->object.magic) slot->object.magic = FFRS_NVN_MAGIC('S', 'Y', 'N', 'C');
    pthread_mutex_unlock(&s_sync_lock);
    return &slot->object;
}

int nvn_sync_family_is_fenced(const void *sync) {
    nvn_sync_state *found;
    int fenced = 0;

    pthread_mutex_lock(&s_sync_lock);
    found = sync_find((void *)(uintptr_t)sync, 0);
    if (found) fenced = found->object.fenced;
    pthread_mutex_unlock(&s_sync_lock);
    return fenced;
}

uint64_t nvn_sync_family_fence_value(const void *sync) {
    nvn_sync_state *found;
    uint64_t fence = 0;

    pthread_mutex_lock(&s_sync_lock);
    found = sync_find((void *)(uintptr_t)sync, 0);
    if (found) fence = found->object.fence;
    pthread_mutex_unlock(&s_sync_lock);
    return fence;
}

int nvn_sync_family_wait(const void *sync, int *logged) {
    nvn_sync_state *found;
    int fenced = 0;
    int log_it = 0;

    if (logged) *logged = 0;
    pthread_mutex_lock(&s_sync_lock);
    found = sync_find((void *)(uintptr_t)sync, 0);
    if (found) fenced = found->object.fenced;
    s_stats.waits++;
    if (!fenced) {
        s_stats.waits_without_fence++;
        /* The reference tree logs the missing fence once for the whole run
         * (a static flag there, hle_nvn.c:634). The same flag lives here,
         * so the log says it once and not once per call. The line itself is
         * printed by the caller, so that it names the queue's own entry
         * point. */
        if (!s_wait_without_fence_logged) {
            s_wait_without_fence_logged = 1;
            s_stats.waits_without_fence_logged++;
            log_it = 1;
        }
    }
    pthread_mutex_unlock(&s_sync_lock);

    if (log_it && logged) *logged = 1;
    return fenced;
}

/* ------------------------------------------------------------------ */
/* The names                                                           */
/* ------------------------------------------------------------------ */

/* The six names the guest itself resolves, in the order the guest resolves
 * them in (the trap slots 21, 22, 23, 25, 27 and 28 of the device run of
 * 2026-10-25). */
static const nvn_sync_family_entry s_sync_family[] = {
    { "nvnSyncInitialize", (void *)(uintptr_t)nvnSyncInitialize },
    { "nvnSyncFinalize", (void *)(uintptr_t)nvnSyncFinalize },
    { "nvnSyncSetDebugLabel", (void *)(uintptr_t)nvnSyncSetDebugLabel },
    { "nvnSyncWait", (void *)(uintptr_t)nvnSyncWait },
    { "nvnSyncCreateGLSync", (void *)(uintptr_t)nvnSyncCreateGLSync },
    { "nvnSyncInitializeFromFencedGLSync", (void *)(uintptr_t)nvnSyncInitializeFromFencedGLSync },
};

size_t nvn_sync_family_count(void) { return sizeof(s_sync_family) / sizeof(s_sync_family[0]); }

const nvn_sync_family_entry *nvn_sync_family_entry_at(size_t index) {
    if (index >= nvn_sync_family_count()) return NULL;
    return &s_sync_family[index];
}

void nvn_sync_get_stats(nvn_sync_stats *out) {
    size_t i;
    if (!out) return;
    pthread_mutex_lock(&s_sync_lock);
    *out = s_stats;
    out->live_syncs = 0;
    for (i = 0; i < s_syncs.count; i++) {
        nvn_sync_state *entry = (nvn_sync_state *)nvn_object_table_at(&s_syncs, i);
        if (entry && entry->in_use) out->live_syncs++;
    }
    pthread_mutex_unlock(&s_sync_lock);
}
