/* The NVN blend state family of the FFRS RG477M native runtime.
 *
 * A blend state carries the blend parameters of the guest's renderer: which
 * blend functions the source and the destination use, which equation
 * combines them, and the advanced mode that replaces the whole pair with a
 * single named operation.
 *
 * Small copied value fields use the runtime-private packed command ABI.
 * Host records retain auxiliary metadata; guest object copies preserve the
 * packed fixed-function state consumed by the renderer.
 */
#ifndef FFRS_NVN_BLEND_STATE_H
#define FFRS_NVN_BLEND_STATE_H

#include <stddef.h>
#include <stdint.h>

typedef struct nvn_blend_state_family_entry {
    const char *name;
    void *fn;
} nvn_blend_state_family_entry;

size_t nvn_blend_state_family_count(void);
const nvn_blend_state_family_entry *nvn_blend_state_family_entry_at(size_t index);

/* Statistics for the contract. */
typedef struct nvn_blend_state_stats {
    uint64_t builder_calls;
    uint64_t defaults_applied;
    uint64_t sets;
    uint64_t gets;
    int live_states;
} nvn_blend_state_stats;

void nvn_blend_state_get_stats(nvn_blend_state_stats *out);

#endif /* FFRS_NVN_BLEND_STATE_H */
