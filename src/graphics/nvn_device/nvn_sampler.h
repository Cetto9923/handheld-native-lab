/* The NVN sampler object family of the FFRS RG477M native runtime.
 *
 * A sampler is what the guest makes out of a sampler builder: the guest fills
 * a builder, hands it to nvnSamplerInitialize, and from then on the sampler is
 * the object the draw calls refer to. The guest reads the parameters of a
 * sampler back through the getters of this family, and it compares two
 * samplers with nvnSamplerCompare to keep its sampler pool free of duplicates.
 *
 * The state lives on the host and is keyed by the address of the guest's
 * sampler, exactly as the queue, the window, the memory pool, the two pool
 * families, the blend state and the render state families do. Nothing is ever
 * written into the guest's sampler object: the layout of an NVNsampler is a
 * property of the Nintendo SDK, which this runtime does not have. The
 * contract proves that with a 0xAA sentinel over the guest's own memory.
 *
 * A sampler is a snapshot of the builder. The parameters are copied at the
 * moment of nvnSamplerInitialize; a later change of the builder does not
 * change a sampler that was already made, and every getter answers with the
 * sampler's own values.
 *
 * Where the evidence comes from:
 *
 *   - the signatures of the fifteen entry points are the ones of the vendored
 *     NVN header (src/graphics/nvn/include/nvn/nvn.h:1391-1406), which is a
 *     reconstruction of NVN 53.105 taken from another game (Darksiders II),
 *     so they are not confirmed for this game. The reference host that runs
 *     this very game agrees with them (.local/refs/nxnative/dm/gfx/nvn/
 *     nvn_protos.h:234-248).
 *   - the defaults, the copy of the parameters and the fields that take part
 *     in the comparison are the ones of that reference host
 *     (hle_nvn.c:533, 558 and 1935).
 */
#ifndef FFRS_NVN_SAMPLER_H
#define FFRS_NVN_SAMPLER_H

#include <stddef.h>
#include <stdint.h>

/* How many samplers the runtime tracks at the same time. The guest keeps its
 * samplers in a pool, so a handful is enough; a guest that wants more is told
 * so instead of being served silently. */
#define NVN_SAMPLER_MAX_SAMPLERS 32

typedef struct nvn_sampler_family_entry {
    const char *name;
    void *fn;
} nvn_sampler_family_entry;

size_t nvn_sampler_family_count(void);
const nvn_sampler_family_entry *nvn_sampler_family_entry_at(size_t index);

/* Statistics for the contract. */
typedef struct nvn_sampler_stats {
    uint64_t calls;         /* every entry point of this family */
    uint64_t initialized;   /* a sampler was made out of a builder */
    uint64_t finalized;     /* a sampler was finalized */
    uint64_t gets;          /* a getter ran */
    uint64_t compares;      /* two samplers were compared */
    uint64_t refused;       /* the call could not be answered */
    int live_samplers;      /* samplers the guest has initialized */
} nvn_sampler_stats;

void nvn_sampler_get_stats(nvn_sampler_stats *out);

#endif /* FFRS_NVN_SAMPLER_H */
