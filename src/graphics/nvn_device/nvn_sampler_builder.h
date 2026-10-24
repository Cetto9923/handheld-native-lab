/* The NVN sampler builder family of the FFRS RG477M native runtime.
 *
 * The guest fills a sampler builder and then hands that builder to
 * nvnSamplerInitialize. The builder is a piece of the guest's own memory and
 * its layout belongs to the Nintendo SDK, which this runtime does not have, so
 * every value the guest puts into it is kept on the host and keyed by the
 * address of the builder. Nothing is ever written into the guest's builder.
 *
 * The same shape as the queue, the window, the memory pool, the render state
 * and the blend state families: a fixed table of states, a defaults routine
 * that really resets the fields it owns, and setters and getters that answer
 * with exactly what the guest put in.
 *
 * Where the evidence comes from:
 *
 *   - the argument order of the setters and getters is the one of the vendored
 *     NVN header (src/graphics/nvn/include/nvn/nvn.h). That header is a
 *     reconstruction of NVN 53.105 taken from another game (Darksiders II),
 *     so its signatures are not confirmed for this game.
 *   - the default values are the ones of the reference host that runs this
 *     very game: .local/refs/nxnative/dm/gfx/nvn/hle_nvn.c, the
 *     nvnSamplerBuilderSetDefaults of nxnative (MIT).
 *   - the five names the vendored header has no signature for are recorded
 *     argument by argument and are not answered with an invented value; see
 *     nvn_sampler_builder.c.
 */
#ifndef FFRS_NVN_SAMPLER_BUILDER_H
#define FFRS_NVN_SAMPLER_BUILDER_H

#include <stddef.h>
#include <stdint.h>

/* How many sampler builders the runtime tracks at the same time. The guest
 * reuses its builders, so a handful is enough; a guest that wants more is told
 * so instead of being served silently. */
#define NVN_SAMPLER_BUILDER_MAX_BUILDERS 32

typedef struct nvn_sampler_builder_family_entry {
    const char *name;
    void *fn;
} nvn_sampler_builder_family_entry;

/* The shape of the entry points whose signature is not in the vendored header.
 * They take their arguments as plain words, write them down as they came and
 * answer with zero; nothing is dereferenced. */
typedef void (*nvn_sampler_builder_raw_proc)(uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t arg3,
                                             uint64_t arg4, uint64_t arg5, uint64_t arg6, uint64_t arg7);

size_t nvn_sampler_builder_family_count(void);
const nvn_sampler_builder_family_entry *nvn_sampler_builder_family_entry_at(size_t index);

/* Statistics for the contract. */
typedef struct nvn_sampler_builder_stats {
    uint64_t calls;          /* every entry point of this family */
    uint64_t set_defaults;   /* the defaults were asked for */
    uint64_t sets;           /* a setter ran */
    uint64_t gets;           /* a getter ran */
    uint64_t refused;        /* the call could not be answered */
    uint64_t raw_calls;      /* names with no signature in the vendored header */
    int live_builders;       /* builders the guest has prepared */
} nvn_sampler_builder_stats;

void nvn_sampler_builder_get_stats(nvn_sampler_builder_stats *out);

/* The device the guest put into a builder.
 *
 * The guest visible getter nvnSamplerBuilderGetDevice has no signature in the
 * vendored header, so it stays an argument recorder and answers with zero.
 * The value itself is kept here, where the families that build a sampler from
 * a builder can read it without guessing an ABI. */
void *nvn_sampler_builder_device_of(const void *builder);

#endif /* FFRS_NVN_SAMPLER_BUILDER_H */
