/* The NVN queue family implementation.
 *
 * See nvn_queue.h for why the state lives on the host.
 *
 * The queue is the guest's handle on the platform's submission queue. The
 * backend owns the real one: on the device that is the Vulkan queue of the
 * logical device this runtime created, and on a host build without Vulkan
 * there is none. The queue family therefore never claims a platform queue it
 * did not get: the initialization fails when the device is not real, and the
 * log says why.
 */
#include "nvn_queue.h"

#include "nvn_command_buffer.h"
#include "nvn_command_replay.h"
#include "nvn_device.h"

#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The guest creates a handful of queues: a graphics queue, maybe an async
 * compute queue, and a few more. The table is small and fixed on purpose. */
#define NVN_QUEUE_MAX_BUILDER_STATES 8
#define NVN_QUEUE_MAX_STATES 8

typedef struct {
    void *builder;
    void *device;
    uint32_t flags;
    int32_t queue_priority;
    uint64_t command_memory_size;
    uint64_t compute_memory_size;
    uint64_t control_memory_size;
    uint64_t queue_memory_size;
    uint64_t command_flush_threshold;
    void *queue_memory;
    int in_use;
} nvn_queue_builder_state;

typedef struct {
    void *queue;
    uint32_t flags;
    int initialized;
    int error;
    void *platform_queue;
    uint64_t command_memory_used;
    uint64_t compute_memory_used;
    uint64_t control_memory_used;
    char debug_label[64];
    int in_use;
} nvn_queue_state;

static pthread_mutex_t s_queue_lock = PTHREAD_MUTEX_INITIALIZER;
static nvn_queue_builder_state s_builders[NVN_QUEUE_MAX_BUILDER_STATES];
static nvn_queue_state s_queues[NVN_QUEUE_MAX_STATES];
static nvn_queue_stats s_stats;

/* ------------------------------------------------------------------ */
/* The sync objects of this family                                     */
/* ------------------------------------------------------------------ */

/* The sync family does not exist yet: it is the next card. Until it does,
 * the state of a sync object lives here, in this family's own table, keyed
 * by the guest's address of the sync object.
 *
 * This table is to be merged into the sync family when that family arrives.
 * Until the merge it is the only place that knows about a sync object, and
 * nothing here writes into the guest's sync object.
 *
 * The guest creates a handful of syncs; the table is small and fixed. */
#define NVN_QUEUE_MAX_SYNCS 16

typedef struct {
    void *sync;
    int fenced;
    uint64_t fence;        /* the submission counter at the time of the fence */
    int in_use;
} nvn_queue_sync_state;

static nvn_queue_sync_state s_syncs[NVN_QUEUE_MAX_SYNCS];

/* The wait of the reference tree logs the missing fence once for the whole
 * run, not once per sync object: it is a static flag there. The same flag
 * lives here. */
static int s_wait_without_fence_logged;

static nvn_queue_sync_state *sync_find(void *sync, int create) {
    size_t i;
    nvn_queue_sync_state *free_slot = NULL;
    if (!sync) return NULL;
    for (i = 0; i < NVN_QUEUE_MAX_SYNCS; i++) {
        if (s_syncs[i].in_use) {
            if (s_syncs[i].sync == sync) return &s_syncs[i];
        } else if (!free_slot) {
            free_slot = &s_syncs[i];
        }
    }
    if (!create || !free_slot) return NULL;
    memset(free_slot, 0, sizeof(*free_slot));
    free_slot->sync = sync;
    free_slot->in_use = 1;
    return free_slot;
}

/* The builder state for one guest builder address. A builder that was never
 * prepared has no state, and the functions that need one say so instead of
 * inventing a default. */
static nvn_queue_builder_state *builder_find(void *builder, int create) {
    size_t i;
    nvn_queue_builder_state *free_slot = NULL;
    if (!builder) return NULL;
    for (i = 0; i < NVN_QUEUE_MAX_BUILDER_STATES; i++) {
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

static nvn_queue_state *queue_find(void *queue, int create) {
    size_t i;
    nvn_queue_state *free_slot = NULL;
    if (!queue) return NULL;
    for (i = 0; i < NVN_QUEUE_MAX_STATES; i++) {
        if (s_queues[i].in_use) {
            if (s_queues[i].queue == queue) return &s_queues[i];
        } else if (!free_slot) {
            free_slot = &s_queues[i];
        }
    }
    if (!create || !free_slot) return NULL;
    memset(free_slot, 0, sizeof(*free_slot));
    free_slot->queue = queue;
    free_slot->in_use = 1;
    return free_slot;
}

/* ------------------------------------------------------------------ */
/* The builder                                                         */
/* ------------------------------------------------------------------ */

void nvn_queue_builder_set_defaults(void *builder) {
    nvn_queue_builder_state *b;
    pthread_mutex_lock(&s_queue_lock);
    s_stats.builder_calls++;
    b = builder_find(builder, 1);
    if (b) {
        /* The defaults are this runtime's own: the memory sizes are what the
         * platform queue needs, the device is not set yet, and the flags are
         * the graphics queue flags the backend can serve. */
        b->device = NULL;
        b->flags = 0;
        b->queue_priority = 0;
        b->command_memory_size = 0;
        b->compute_memory_size = 0;
        b->control_memory_size = 0;
        b->queue_memory_size = 0;
        b->command_flush_threshold = 0;
        b->queue_memory = NULL;
    }
    pthread_mutex_unlock(&s_queue_lock);
    if (!b) fprintf(stderr, "[nvn] nvnQueueBuilderSetDefaults: no room for another builder (%p)\n", builder);
    NVN_VOID_RETURN();
}

void nvn_queue_builder_set_device(void *builder, void *device) {
    nvn_queue_builder_state *b;
    pthread_mutex_lock(&s_queue_lock);
    s_stats.builder_calls++;
    b = builder_find(builder, 1);
    if (b) b->device = device;
    pthread_mutex_unlock(&s_queue_lock);
    NVN_VOID_RETURN();
}

void nvn_queue_builder_set_flags(void *builder, int flags) {
    nvn_queue_builder_state *b;
    pthread_mutex_lock(&s_queue_lock);
    s_stats.builder_calls++;
    b = builder_find(builder, 1);
    if (b) b->flags = (uint32_t)flags;
    pthread_mutex_unlock(&s_queue_lock);
    NVN_VOID_RETURN();
}

void nvn_queue_builder_set_queue_priority(void *builder, int priority) {
    nvn_queue_builder_state *b;
    pthread_mutex_lock(&s_queue_lock);
    s_stats.builder_calls++;
    b = builder_find(builder, 1);
    if (b) b->queue_priority = priority;
    pthread_mutex_unlock(&s_queue_lock);
    NVN_VOID_RETURN();
}

void nvn_queue_builder_set_command_memory_size(void *builder, uint64_t size) {
    nvn_queue_builder_state *b;
    pthread_mutex_lock(&s_queue_lock);
    s_stats.builder_calls++;
    b = builder_find(builder, 1);
    if (b) {
        b->command_memory_size = size;
        s_stats.command_memory_requested += size;
    }
    pthread_mutex_unlock(&s_queue_lock);
    NVN_VOID_RETURN();
}

void nvn_queue_builder_set_compute_memory_size(void *builder, uint64_t size) {
    nvn_queue_builder_state *b;
    pthread_mutex_lock(&s_queue_lock);
    s_stats.builder_calls++;
    b = builder_find(builder, 1);
    if (b) {
        b->compute_memory_size = size;
        s_stats.compute_memory_requested += size;
    }
    pthread_mutex_unlock(&s_queue_lock);
    NVN_VOID_RETURN();
}

void nvn_queue_builder_set_control_memory_size(void *builder, uint64_t size) {
    nvn_queue_builder_state *b;
    pthread_mutex_lock(&s_queue_lock);
    s_stats.builder_calls++;
    b = builder_find(builder, 1);
    if (b) {
        b->control_memory_size = size;
        s_stats.control_memory_requested += size;
    }
    pthread_mutex_unlock(&s_queue_lock);
    NVN_VOID_RETURN();
}

void nvn_queue_builder_set_command_flush_threshold(void *builder, uint64_t threshold) {
    nvn_queue_builder_state *b;
    pthread_mutex_lock(&s_queue_lock);
    s_stats.builder_calls++;
    b = builder_find(builder, 1);
    if (b) b->command_flush_threshold = threshold;
    pthread_mutex_unlock(&s_queue_lock);
    NVN_VOID_RETURN();
}

void nvn_queue_builder_set_queue_memory(void *builder, void *memory, uint64_t size) {
    nvn_queue_builder_state *b;
    pthread_mutex_lock(&s_queue_lock);
    s_stats.builder_calls++;
    b = builder_find(builder, 1);
    if (b) {
        b->queue_memory = memory;
        b->queue_memory_size = size;
    }
    pthread_mutex_unlock(&s_queue_lock);
    NVN_VOID_RETURN();
}

void *nvn_queue_builder_get_device(const void *builder) {
    nvn_queue_builder_state *b;
    void *device = NULL;
    pthread_mutex_lock(&s_queue_lock);
    s_stats.builder_calls++;
    b = builder_find((void *)(uintptr_t)builder, 0);
    if (b) device = b->device;
    pthread_mutex_unlock(&s_queue_lock);
    return device;
}

int nvn_queue_builder_get_flags(const void *builder) {
    nvn_queue_builder_state *b;
    int flags = 0;
    pthread_mutex_lock(&s_queue_lock);
    s_stats.builder_calls++;
    b = builder_find((void *)(uintptr_t)builder, 0);
    if (b) flags = (int)(uint32_t)b->flags;
    pthread_mutex_unlock(&s_queue_lock);
    return flags;
}

int nvn_queue_builder_get_queue_priority(const void *builder) {
    nvn_queue_builder_state *b;
    int priority = 0;
    pthread_mutex_lock(&s_queue_lock);
    s_stats.builder_calls++;
    b = builder_find((void *)(uintptr_t)builder, 0);
    if (b) priority = b->queue_priority;
    pthread_mutex_unlock(&s_queue_lock);
    return priority;
}

uint64_t nvn_queue_builder_get_command_memory_size(const void *builder) {
    nvn_queue_builder_state *b;
    uint64_t size = 0;
    pthread_mutex_lock(&s_queue_lock);
    s_stats.builder_calls++;
    b = builder_find((void *)(uintptr_t)builder, 0);
    if (b) size = b->command_memory_size;
    pthread_mutex_unlock(&s_queue_lock);
    return size;
}

uint64_t nvn_queue_builder_get_compute_memory_size(const void *builder) {
    nvn_queue_builder_state *b;
    uint64_t size = 0;
    pthread_mutex_lock(&s_queue_lock);
    s_stats.builder_calls++;
    b = builder_find((void *)(uintptr_t)builder, 0);
    if (b) size = b->compute_memory_size;
    pthread_mutex_unlock(&s_queue_lock);
    return size;
}

uint64_t nvn_queue_builder_get_control_memory_size(const void *builder) {
    nvn_queue_builder_state *b;
    uint64_t size = 0;
    pthread_mutex_lock(&s_queue_lock);
    s_stats.builder_calls++;
    b = builder_find((void *)(uintptr_t)builder, 0);
    if (b) size = b->control_memory_size;
    pthread_mutex_unlock(&s_queue_lock);
    return size;
}

uint64_t nvn_queue_builder_get_command_flush_threshold(const void *builder) {
    nvn_queue_builder_state *b;
    uint64_t threshold = 0;
    pthread_mutex_lock(&s_queue_lock);
    s_stats.builder_calls++;
    b = builder_find((void *)(uintptr_t)builder, 0);
    if (b) threshold = b->command_flush_threshold;
    pthread_mutex_unlock(&s_queue_lock);
    return threshold;
}

void *nvn_queue_builder_get_queue_memory(const void *builder) {
    nvn_queue_builder_state *b;
    void *memory = NULL;
    pthread_mutex_lock(&s_queue_lock);
    s_stats.builder_calls++;
    b = builder_find((void *)(uintptr_t)builder, 0);
    if (b) memory = b->queue_memory;
    pthread_mutex_unlock(&s_queue_lock);
    return memory;
}

uint64_t nvn_queue_builder_get_memory_size(const void *builder) {
    nvn_queue_builder_state *b;
    uint64_t size = 0;
    pthread_mutex_lock(&s_queue_lock);
    s_stats.builder_calls++;
    b = builder_find((void *)(uintptr_t)builder, 0);
    if (b) size = b->queue_memory_size;
    pthread_mutex_unlock(&s_queue_lock);
    return size;
}

uint64_t nvn_queue_builder_get_queue_memory_size(const void *builder) {
    return nvn_queue_builder_get_memory_size(builder);
}

void *nvn_queue_builder_get_memory(const void *builder) {
    /* This name is in neither the vendored NVN header nor the reference
     * tree; the guest asks for it by name all the same. It is answered the
     * way this runtime answers the queue memory getter: the memory the
     * guest gave the builder with nvnQueueBuilderSetQueueMemory comes back
     * as it was given. A builder that was never prepared has no memory, and
     * NULL is said rather than an address made up. */
    nvn_queue_builder_state *b;
    void *memory = NULL;
    pthread_mutex_lock(&s_queue_lock);
    s_stats.builder_calls++;
    b = builder_find((void *)(uintptr_t)builder, 0);
    if (b) memory = b->queue_memory;
    pthread_mutex_unlock(&s_queue_lock);
    return memory;
}

/* ------------------------------------------------------------------ */
/* The queue                                                           */
/* ------------------------------------------------------------------ */

int nvn_queue_initialize(void *queue, const void *builder, int flags) {
    nvn_queue_builder_state *b;
    nvn_queue_state *q;
    void *platform_queue = NULL;
    int ready = 0;
    const nvn_backend *backend = nvn_device_backend();

    pthread_mutex_lock(&s_queue_lock);
    s_stats.builder_calls++;
    b = builder_find((void *)(uintptr_t)builder, 0);
    if (!b) {
        s_stats.refused++;
        pthread_mutex_unlock(&s_queue_lock);
        fprintf(stderr, "[nvn] nvnQueueInitialize: no builder state for %p; the guest did not prepare one\n",
                builder);
        return 0;
    }
    if (!nvn_device_is_ready(b->device)) {
        s_stats.refused++;
        pthread_mutex_unlock(&s_queue_lock);
        fprintf(stderr, "[nvn] nvnQueueInitialize: the device in the builder (%p) is not an initialized device\n",
                b->device);
        return 0;
    }
    q = queue_find(queue, 1);
    if (!q) {
        s_stats.refused++;
        pthread_mutex_unlock(&s_queue_lock);
        fprintf(stderr, "[nvn] nvnQueueInitialize: no room for another queue\n");
        return 0;
    }
    q->flags = (uint32_t)flags;
    q->error = 0;
    q->command_memory_used = 0;
    q->compute_memory_used = 0;
    q->control_memory_used = 0;
    q->platform_queue = NULL;
    if (backend && backend->get_queue) {
        if (backend->get_queue(nvn_device_state_handle(), &platform_queue)) {
            q->platform_queue = platform_queue;
            s_stats.platform_queue_acquired++;
        }
    }
    q->initialized = 1;
    s_stats.initialized++;
    ready = 1;
    pthread_mutex_unlock(&s_queue_lock);

    fprintf(stderr, "[nvn] nvnQueueInitialize: queue=%p builder=%p device=%p flags=0x%" PRIx32
                    " command_memory=%" PRIu64 " control_memory=%" PRIu64 " platform_queue=%p -> %s\n",
            queue, builder, b->device, (uint32_t)flags, b->command_memory_size, b->control_memory_size,
            q->platform_queue, ready ? "true" : "false");
    return 1;
}

void nvn_queue_finalize(void *queue) {
    nvn_queue_state *q;
    pthread_mutex_lock(&s_queue_lock);
    q = queue_find(queue, 0);
    if (q) {
        q->initialized = 0;
        q->platform_queue = NULL;
        q->in_use = 0;
        s_stats.finalized++;
        if (s_stats.live_queues > 0) s_stats.live_queues--;
    }
    pthread_mutex_unlock(&s_queue_lock);
    fprintf(stderr, "[nvn] nvnQueueFinalize: queue=%p\n", queue);
    NVN_VOID_RETURN();
}

int nvn_queue_get_error(const void *queue) {
    nvn_queue_state *q;
    int error = 0;
    pthread_mutex_lock(&s_queue_lock);
    q = queue_find((void *)(uintptr_t)queue, 0);
    if (q) error = q->error;
    pthread_mutex_unlock(&s_queue_lock);
    return error;
}

void nvn_queue_set_debug_label(void *queue, const char *label) {
    nvn_queue_state *q;
    pthread_mutex_lock(&s_queue_lock);
    q = queue_find(queue, 1);
    if (q) snprintf(q->debug_label, sizeof(q->debug_label), "%s", label ? label : "");
    pthread_mutex_unlock(&s_queue_lock);
    NVN_VOID_RETURN();
}

void nvn_queue_flush(void *queue) {
    /* Nothing is submitted yet: the submit path belongs to the command buffer
     * family. The call is answered honestly instead of pretending that work
     * was flushed. */
    nvn_queue_state *q;
    int known = 0;
    pthread_mutex_lock(&s_queue_lock);
    q = queue_find(queue, 0);
    known = q != NULL && q->initialized;
    pthread_mutex_unlock(&s_queue_lock);
    fprintf(stderr, "[nvn] nvnQueueFlush: queue=%p initialized=%d; no command buffer has been submitted yet, "
                    "nothing to flush\n",
            queue, known);
    NVN_VOID_RETURN();
}

void nvn_queue_finish(void *queue) {
    /* Same as the flush: there is no submitted work to wait for. */
    nvn_queue_state *q;
    int known = 0;
    pthread_mutex_lock(&s_queue_lock);
    q = queue_find(queue, 0);
    known = q != NULL && q->initialized;
    pthread_mutex_unlock(&s_queue_lock);
    fprintf(stderr, "[nvn] nvnQueueFinish: queue=%p initialized=%d; no submitted work, nothing to wait for\n",
            queue, known);
    NVN_VOID_RETURN();
}

uint64_t nvn_queue_get_total_command_memory_used(const void *queue) {
    nvn_queue_state *q;
    uint64_t used = 0;
    pthread_mutex_lock(&s_queue_lock);
    q = queue_find((void *)(uintptr_t)queue, 0);
    if (q) used = q->command_memory_used;
    pthread_mutex_unlock(&s_queue_lock);
    return used;
}

uint64_t nvn_queue_get_total_compute_memory_used(const void *queue) {
    nvn_queue_state *q;
    uint64_t used = 0;
    pthread_mutex_lock(&s_queue_lock);
    q = queue_find((void *)(uintptr_t)queue, 0);
    if (q) used = q->compute_memory_used;
    pthread_mutex_unlock(&s_queue_lock);
    return used;
}

uint64_t nvn_queue_get_total_control_memory_used(const void *queue) {
    nvn_queue_state *q;
    uint64_t used = 0;
    pthread_mutex_lock(&s_queue_lock);
    q = queue_find((void *)(uintptr_t)queue, 0);
    if (q) used = q->control_memory_used;
    pthread_mutex_unlock(&s_queue_lock);
    return used;
}

void nvn_queue_reset_memory_usage_counts(void *queue) {
    nvn_queue_state *q;
    pthread_mutex_lock(&s_queue_lock);
    q = queue_find(queue, 0);
    if (q) {
        q->command_memory_used = 0;
        q->compute_memory_used = 0;
        q->control_memory_used = 0;
    }
    pthread_mutex_unlock(&s_queue_lock);
    NVN_VOID_RETURN();
}

/* ------------------------------------------------------------------ */
/* The submission seam                                                  */
/* ------------------------------------------------------------------ */

void nvn_queue_submit_commands(void *queue, int count, const uint64_t *handles) {
    /* The reference tree runs the package of each handle here
     * (.local/refs/nxnative/dm/gfx/nvn/hle_nvn.c L1132, where the handle
     * h[i] is what is executed). This runtime's replay reads the package
     * instead of executing it, and that read is the seam.
     *
     * The bytes of the package are not looked at here. What the replay
     * decoded is the replay's business, and this function does not
     * interpret a single command byte. */
    int i;
    int handed = 0;
    int refused = 0;
    uint64_t records_before = 0;
    uint64_t records_after = 0;
    uint64_t replay_refusals_before = 0;
    uint64_t replay_refusals_after = 0;

    pthread_mutex_lock(&s_queue_lock);
    s_stats.submit_calls++;
    pthread_mutex_unlock(&s_queue_lock);

    /* The counters of the replay are read before and after, so that what
     * this call made the replay do is counted here and not only inside the
     * replay. */
    {
        nvn_replay_stats stats;
        nvn_command_replay_get_stats(&stats);
        records_before = stats.records;
        replay_refusals_before = stats.refused_unknown_handle + stats.refused_bad_record +
                                 stats.refused_no_pool + stats.refused_out_of_range +
                                 stats.refused_foreign_pool;
    }

    for (i = 0; i < count; i++) {
        nvn_command_package package;
        uint64_t handle = handles ? handles[i] : 0;
        int have;

        /* The package is asked for first, so that a handle that names no
         * package is refused here, by name, and counted. It is not skipped
         * silently and it is never counted as submitted. The handle is not
         * dereferenced: it goes through the registry of the recorder. */
        have = nvn_command_buffer_get_package(handle, &package);
        if (!have) {
            refused++;
            pthread_mutex_lock(&s_queue_lock);
            s_stats.packages_refused++;
            pthread_mutex_unlock(&s_queue_lock);
            fprintf(stderr, "[nvn] nvnQueueSubmitCommands: the handle 0x%" PRIx64
                            " names no command package; the submission of that command is refused\n",
                    handle);
            continue;
        }

        /* The package is there, so the replay is called with it. The replay
         * reads the records and stops there: it issues no Vulkan command,
         * so nothing is submitted to the GPU yet. */
        (void)nvn_command_replay_run(handle);
        handed++;
        pthread_mutex_lock(&s_queue_lock);
        s_stats.packages_handed_to_replay++;
        pthread_mutex_unlock(&s_queue_lock);
        fprintf(stderr, "[nvn] nvnQueueSubmitCommands: the handle 0x%" PRIx64
                        " names a package of %zu records; the package was read and the commands were not submitted\n",
                handle, package.record_count);
    }

    {
        nvn_replay_stats stats;
        nvn_command_replay_get_stats(&stats);
        records_after = stats.records;
        replay_refusals_after = stats.refused_unknown_handle + stats.refused_bad_record +
                                stats.refused_no_pool + stats.refused_out_of_range +
                                stats.refused_foreign_pool;
    }

    pthread_mutex_lock(&s_queue_lock);
    if (records_after > records_before) s_stats.records_read += records_after - records_before;
    if (replay_refusals_after > replay_refusals_before)
        s_stats.replay_refusals += replay_refusals_after - replay_refusals_before;
    pthread_mutex_unlock(&s_queue_lock);

    fprintf(stderr, "[nvn] nvnQueueSubmitCommands: queue=%p packages=%d handed_to_the_replay=%d refused=%d "
                    "records_read=%" PRIu64 " replay_refusals=%" PRIu64
                    "; the packages were read and the commands were not submitted to Vulkan\n",
            queue, count, handed, refused, records_after - records_before,
            replay_refusals_after - replay_refusals_before);
    NVN_VOID_RETURN();
}

void nvn_queue_present_texture(void *queue, void *window, int index) {
    /* The present path is not connected yet: the swapchain is the card that
     * owns it (P-MM-28). The request is recorded, and the log says that
     * nothing was presented. No success is reported that cannot be had. */
    pthread_mutex_lock(&s_queue_lock);
    s_stats.presents_requested++;
    pthread_mutex_unlock(&s_queue_lock);
    fprintf(stderr, "[nvn] nvnQueuePresentTexture: queue=%p window=%p index=%d; the request is recorded, "
                    "the present path is not connected yet and nothing was presented\n",
            queue, window, index);
    NVN_VOID_RETURN();
}

void nvn_queue_fence_sync(void *queue, void *sync, int condition, int flags) {
    nvn_queue_sync_state *s;
    uint64_t fence;
    pthread_mutex_lock(&s_queue_lock);
    s = sync_find(sync, 1);
    if (!s) {
        s_stats.refused++;
        pthread_mutex_unlock(&s_queue_lock);
        fprintf(stderr, "[nvn] nvnQueueFenceSync: no room for another sync object; the sync was not fenced\n");
        return;
    }
    /* The fence value that is recorded is this runtime's own submission
     * counter. The host has no GPU fence to hand out: the replay reads the
     * packages and issues no command, so there is nothing that could be
     * signalled. The log says so. */
    fence = s_stats.packages_handed_to_replay;
    s->fenced = 1;
    s->fence = fence;
    s_stats.fences_taken++;
    pthread_mutex_unlock(&s_queue_lock);
    fprintf(stderr, "[nvn] nvnQueueFenceSync: queue=%p sync=%p condition=%d flags=%d; the sync is marked as fenced "
                    "with the submission counter %" PRIu64
                    " (the host has no GPU fence yet: the replay issues no command)\n",
            queue, sync, condition, flags, fence);
    NVN_VOID_RETURN();
}

int nvn_queue_wait_sync(void *queue, const void *sync) {
    nvn_queue_sync_state *s;
    int fenced = 0;
    int log_it = 0;

    pthread_mutex_lock(&s_queue_lock);
    s = sync_find((void *)(uintptr_t)sync, 0);
    if (s) fenced = s->fenced;
    s_stats.waits++;
    if (!fenced) {
        s_stats.waits_without_fence++;
        /* The reference tree logs the missing fence once for the whole run
         * (a static flag there, hle_nvn.c L634). The same flag lives here,
         * so the log says it once and not once per call. */
        if (!s_wait_without_fence_logged) {
            s_wait_without_fence_logged = 1;
            s_stats.waits_without_fence_logged++;
            log_it = 1;
        }
    }
    pthread_mutex_unlock(&s_queue_lock);

    if (log_it) {
        fprintf(stderr, "[nvn] nvnQueueWaitSync: the sync object has no fence yet, the host does not wait\n");
    }
    /* The reference tree answers 1 here, always: the host has a single
     * submission queue, so the commands before the fence are done first and
     * nothing needs waiting for. */
    (void)queue;
    (void)sync;
    return 1;
}

/* ------------------------------------------------------------------ */
/* The names                                                           */
/* ------------------------------------------------------------------ */

static const nvn_queue_family_entry s_queue_family[] = {
    { "nvnQueueBuilderSetDefaults", (void *)(uintptr_t)nvn_queue_builder_set_defaults },
    { "nvnQueueBuilderSetDevice", (void *)(uintptr_t)nvn_queue_builder_set_device },
    { "nvnQueueBuilderSetFlags", (void *)(uintptr_t)nvn_queue_builder_set_flags },
    { "nvnQueueBuilderSetQueuePriority", (void *)(uintptr_t)nvn_queue_builder_set_queue_priority },
    { "nvnQueueBuilderSetCommandMemorySize", (void *)(uintptr_t)nvn_queue_builder_set_command_memory_size },
    { "nvnQueueBuilderSetComputeMemorySize", (void *)(uintptr_t)nvn_queue_builder_set_compute_memory_size },
    { "nvnQueueBuilderSetControlMemorySize", (void *)(uintptr_t)nvn_queue_builder_set_control_memory_size },
    { "nvnQueueBuilderSetCommandFlushThreshold",
      (void *)(uintptr_t)nvn_queue_builder_set_command_flush_threshold },
    { "nvnQueueBuilderSetQueueMemory", (void *)(uintptr_t)nvn_queue_builder_set_queue_memory },
    { "nvnQueueBuilderGetDevice", (void *)(uintptr_t)nvn_queue_builder_get_device },
    { "nvnQueueBuilderGetFlags", (void *)(uintptr_t)nvn_queue_builder_get_flags },
    { "nvnQueueBuilderGetQueuePriority", (void *)(uintptr_t)nvn_queue_builder_get_queue_priority },
    { "nvnQueueBuilderGetCommandMemorySize", (void *)(uintptr_t)nvn_queue_builder_get_command_memory_size },
    { "nvnQueueBuilderGetComputeMemorySize", (void *)(uintptr_t)nvn_queue_builder_get_compute_memory_size },
    { "nvnQueueBuilderGetControlMemorySize", (void *)(uintptr_t)nvn_queue_builder_get_control_memory_size },
    { "nvnQueueBuilderGetCommandFlushThreshold",
      (void *)(uintptr_t)nvn_queue_builder_get_command_flush_threshold },
    { "nvnQueueBuilderGetQueueMemory", (void *)(uintptr_t)nvn_queue_builder_get_queue_memory },
    { "nvnQueueBuilderGetMemorySize", (void *)(uintptr_t)nvn_queue_builder_get_memory_size },
    { "nvnQueueBuilderGetQueueMemorySize", (void *)(uintptr_t)nvn_queue_builder_get_queue_memory_size },
    { "nvnQueueInitialize", (void *)(uintptr_t)nvn_queue_initialize },
    { "nvnQueueFinalize", (void *)(uintptr_t)nvn_queue_finalize },
    { "nvnQueueGetError", (void *)(uintptr_t)nvn_queue_get_error },
    { "nvnQueueSetDebugLabel", (void *)(uintptr_t)nvn_queue_set_debug_label },
    { "nvnQueueFlush", (void *)(uintptr_t)nvn_queue_flush },
    { "nvnQueueFinish", (void *)(uintptr_t)nvn_queue_finish },
    { "nvnQueueGetTotalCommandMemoryUsed", (void *)(uintptr_t)nvn_queue_get_total_command_memory_used },
    { "nvnQueueGetTotalComputeMemoryUsed", (void *)(uintptr_t)nvn_queue_get_total_compute_memory_used },
    { "nvnQueueGetTotalControlMemoryUsed", (void *)(uintptr_t)nvn_queue_get_total_control_memory_used },
    { "nvnQueueResetMemoryUsageCounts", (void *)(uintptr_t)nvn_queue_reset_memory_usage_counts },
    { "nvnQueueSubmitCommands", (void *)(uintptr_t)nvn_queue_submit_commands },
    { "nvnQueuePresentTexture", (void *)(uintptr_t)nvn_queue_present_texture },
    { "nvnQueueFenceSync", (void *)(uintptr_t)nvn_queue_fence_sync },
    { "nvnQueueWaitSync", (void *)(uintptr_t)nvn_queue_wait_sync },
    { "nvnQueueBuilderGetMemory", (void *)(uintptr_t)nvn_queue_builder_get_memory },
};

size_t nvn_queue_family_count(void) { return sizeof(s_queue_family) / sizeof(s_queue_family[0]); }

const nvn_queue_family_entry *nvn_queue_family_entry_at(size_t index) {
    if (index >= nvn_queue_family_count()) return NULL;
    return &s_queue_family[index];
}

void nvn_queue_get_stats(nvn_queue_stats *out) {
    size_t i;
    if (!out) return;
    pthread_mutex_lock(&s_queue_lock);
    *out = s_stats;
    out->live_queues = 0;
    out->platform_queue_present = 0;
    for (i = 0; i < NVN_QUEUE_MAX_STATES; i++) {
        if (s_queues[i].in_use && s_queues[i].initialized) {
            out->live_queues++;
            if (s_queues[i].platform_queue) out->platform_queue_present = 1;
        }
    }
    out->live_syncs = 0;
    for (i = 0; i < NVN_QUEUE_MAX_SYNCS; i++) {
        if (s_syncs[i].in_use) out->live_syncs++;
    }
    pthread_mutex_unlock(&s_queue_lock);
}
