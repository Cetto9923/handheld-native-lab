/* The NVN render state families of the FFRS RG477M native runtime.
 *
 * Seven families describe the fixed function state of a draw: the colour
 * state, the channel mask state, the multisample state, the polygon state,
 * the depth stencil state, the vertex attribute state and the vertex stream
 * state.
 *
 * Small copied value fields use the runtime-private packed command ABI.
 * Host records retain auxiliary metadata; guest object copies preserve the
 * packed fixed-function state consumed by the renderer.
 */
#ifndef FFRS_NVN_RENDER_STATE_H
#define FFRS_NVN_RENDER_STATE_H

#include <stddef.h>
#include <stdint.h>

typedef struct nvn_render_state_family_entry {
    const char *name;
    void *fn;
} nvn_render_state_family_entry;

size_t nvn_render_state_family_count(void);
const nvn_render_state_family_entry *nvn_render_state_family_entry_at(size_t index);

/* Statistics for the contract. */
typedef struct nvn_render_state_stats {
    uint64_t calls;
    uint64_t defaults_applied;
    uint64_t sets;
    uint64_t gets;
    int live_states;
} nvn_render_state_stats;

void nvn_render_state_get_stats(nvn_render_state_stats *out);

#endif /* FFRS_NVN_RENDER_STATE_H */
