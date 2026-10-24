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
    pthread_mutex_unlock(&s_queue_lock);
}
