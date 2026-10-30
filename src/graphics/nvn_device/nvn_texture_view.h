/* The NVN texture view family of the FFRS RG477M native runtime.
 *
 * A texture view is what the guest makes out of a view builder: the guest
 * fills in the levels, the layers, the format, the target, the swizzle and the
 * depth stencil mode of a view, and from then on the view says which part of a
 * texture a draw refers to.
 *
 * The state lives on the host and is keyed by the address of the guest's view,
 * exactly as the queue, the window, the memory pool, the pool families, the
 * blend state, the render state, the sampler and the texture families do.
 * Nothing is ever written into the guest's view: the layout of an NVNtextureView
 * is a property of the Nintendo SDK, which this runtime does not have. The
 * contract proves that with a 0xAA sentinel over the guest's own memory.
 *
 * Where the evidence comes from:
 *
 *   - the signatures of the fourteen entry points are the ones of the vendored
 *     NVN header (src/graphics/nvn/include/nvn/nvn.h:1300-1319), which is a
 *     reconstruction of NVN 53.105 taken from another game (Darksiders II), so
 *     the signatures are NOT confirmed for this game.
 *   - the semantics are the ones of the reference host that runs this very
 *     game (.local/refs/nxnative/dm/gfx/nvn/hle_nvn.c, MIT). The one name
 *     that the reference host does not have is nvnTextureViewCompare, and its
 *     meaning in this runtime is this runtime's own; it is named in the report
 *     of this family and is not claimed to agree with the reference.
 */
#ifndef FFRS_NVN_TEXTURE_VIEW_H
#define FFRS_NVN_TEXTURE_VIEW_H

#include <stddef.h>
#include <stdint.h>

/* How many texture views the runtime tracks at the same time.
 *
 * The table grows with the number of views the guest has, so this is a limit
 * and not a guess: it is the number of textures the device allows, and a view
 * belongs to a texture. A guest that runs out of them is told so instead of
 * being served silently. */
#define NVN_TEXTURE_VIEW_MAX_OBJECTS 1048576

typedef struct nvn_texture_view_family_entry {
    const char *name;
    void *fn;
} nvn_texture_view_family_entry;

size_t nvn_texture_view_family_count(void);
const nvn_texture_view_family_entry *nvn_texture_view_family_entry_at(size_t index);

/* Statistics for the contract. */
typedef struct nvn_texture_view_stats {
    uint64_t calls;          /* every entry point of this family */
    uint64_t set_defaults;   /* the defaults were asked for */
    uint64_t sets;           /* a setter ran */
    uint64_t gets;           /* a getter ran */
    uint64_t compares;       /* two views were compared */
    uint64_t refused;        /* the call could not be answered */
    int live_views;          /* views the guest has described */
} nvn_texture_view_stats;

void nvn_texture_view_get_stats(nvn_texture_view_stats *out);

#endif /* FFRS_NVN_TEXTURE_VIEW_H */
