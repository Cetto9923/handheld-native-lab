/* The consumer of the NVN command packages of this runtime.
 *
 * The recorder (nvn_command_buffer.c) writes the commands of the guest into
 * an immutable package. This file is what reads such a package and turns it
 * into the things the device can do.
 *
 * It does three things and stops there:
 *
 *   - it decodes the records with the structures of the reference tree, so a
 *     record written here is read with the reference's own structures. The
 *     comparison against the reference is scripts/graphics/compare_nvn_record_format.py
 *     and it says the fifty opcodes and the thirty-three payload structures
 *     are the same;
 *   - it keeps the state that a command stream carries from one command to
 *     the next: the pools the stream bound, the program it bound and the
 *     targets it draws into. This is the same table the reference's replay
 *     keeps;
 *   - it resolves the addresses the records carry into the host memory behind
 *     them, and refuses the ones that cannot be resolved. An address that
 *     belongs to no pool of the guest is not a number that can be turned into
 *     a pointer, and a length that runs past the end of the pool is not
 *     something the driver can be trusted to catch.
 *
 * What it does not do: it issues no Vulkan command and it presents nothing.
 * The device, the queue and the window are elsewhere, and a replay that
 * created a second device would not be able to share a single image with the
 * guest's own render target. That is the next step.
 */
#ifndef FFRS_NVN_COMMAND_REPLAY_H
#define FFRS_NVN_COMMAND_REPLAY_H

#include <stddef.h>
#include <stdint.h>

#include "nvn_command_buffer.h"

/* The state a command stream carries from one command to the next.
 *
 * The reference's replay keeps the pools a command buffer bound last, and
 * this is that table. The numbering bases of the descriptor pools are what
 * turn a texture handle into a texture, so a command that uses a handle
 * without a pool bound is refused by name. */
typedef struct nvn_replay_state {
    uint64_t tex_pool;       /* the texture pool the stream bound last */
    uint64_t samp_pool;      /* the sampler pool the stream bound last */
    uint64_t program;        /* the program the stream bound last */
    uint32_t render_targets; /* how many colour targets the stream draws into */
    uint64_t render_target[8];
} nvn_replay_state;

/* What the replay did, so that the contract and the log can say it. */
typedef struct nvn_replay_stats {
    uint64_t packages;              /* packages the replay was given */
    uint64_t records;               /* records it decoded */
    uint64_t opcodes[NVN_CB_OP_COUNT]; /* how many of each opcode */
    uint64_t refused_unknown_handle;/* handles the registry does not know */
    uint64_t refused_bad_record;    /* a record whose header does not hold */
    uint64_t refused_no_pool;       /* an address that belongs to no pool */
    uint64_t refused_out_of_range;  /* a range that runs past the pool */
    uint64_t refused_foreign_pool;  /* a handle of a pool that is not bound */
    uint64_t resolved_addresses;    /* addresses that were resolved */
    uint64_t resolved_bytes;        /* bytes they stand for */
    uint64_t draws;                 /* draw commands the stream holds */
    uint64_t dispatches;            /* dispatch commands the stream holds */
    uint64_t clears;                /* clear commands the stream holds */
    uint64_t textures_bound;        /* texture handles the stream binds */
    uint64_t samplers_bound;        /* sampler handles the stream binds */
} nvn_replay_stats;

/* Decode one package.
 *
 * The handle is the one the recorder handed out. It goes through the
 * registry of the recorder and is never dereferenced: a handle the registry
 * does not know is refused and counted.
 *
 * Returns 1 when every record of the package was understood, and 0 when at
 * least one record was refused. A refusal never stops the walk: the rest of
 * the package is still decoded, so that the log says everything that is
 * wrong with the stream and not only the first thing. */
int nvn_command_replay_run(uint64_t handle);

/* The state the last run left behind. */
void nvn_command_replay_get_state(nvn_replay_state *out);

void nvn_command_replay_get_stats(nvn_replay_stats *out);

/* Forget the state and the counters. The packages of the recorder are not
 * touched: they belong to the recorder and to the guest. */
void nvn_command_replay_reset(void);

#endif /* FFRS_NVN_COMMAND_REPLAY_H */
