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
} nvn_queue_stats;

void nvn_queue_get_stats(nvn_queue_stats *out);

#endif /* FFRS_NVN_QUEUE_H */
