/* The NVN program family of the FFRS RG477M native runtime.
 *
 * A program is the guest's handle on a set of shaders. This family is where
 * the guest's shader bytes enter the runtime: nvnProgramSetShaders hands over
 * one entry per stage, and every entry names the code area of that stage and
 * the control section that describes it.
 *
 * The control section is only guaranteed to be valid while the guest is
 * inside the call that hands it over, so the runtime copies it. The code area
 * lives in a memory pool the guest created and stays valid as long as the
 * program is in use, so the runtime reads it through the pool.
 *
 * The program object itself belongs to the Nintendo SDK, whose layout this
 * runtime does not have, so the state lives on the host and is keyed by the
 * guest's program address.
 */
#ifndef FFRS_NVN_PROGRAM_H
#define FFRS_NVN_PROGRAM_H

#include <stddef.h>
#include <stdint.h>

#include "nvn_sha256.h"

typedef struct nvn_program_family_entry {
    const char *name;
    void *fn;
} nvn_program_family_entry;

size_t nvn_program_family_count(void);
const nvn_program_family_entry *nvn_program_family_entry_at(size_t index);

/* Statistics for the contract. */
typedef struct nvn_program_stats {
    uint64_t calls;
    uint64_t initialized;
    uint64_t finalized;
    uint64_t shaders_set;
    uint64_t shaders_ext_set;
    uint64_t stages;
    uint64_t keyed;
    uint64_t refused;
    int live_programs;
} nvn_program_stats;

void nvn_program_get_stats(nvn_program_stats *out);

/* The key of one stage, and how large its code area and its control section
 * are.
 *
 * The return value says whether the stage has a key at all. A stage whose
 * code area could not be read has none, and a caller that was given a key
 * that does not exist would look up a shader that is not there. */
int nvn_program_get_stage_info(void *program, int stage, char key[NVN_SHA256_KEY_HEX_SIZE],
                               uint32_t *code_length, uint32_t *control_length);

/* A part of the control section the runtime copied.
 *
 * The control section is only valid while the guest is inside the call that
 * hands it over, so the runtime keeps a copy of it. This is how a caller can
 * check that the copy is really there: the guest may overwrite its own
 * control section right after the call, and the copy still holds what the
 * guest handed over. */
int nvn_program_get_stage_control(void *program, int stage, void *destination, size_t size);

#endif /* FFRS_NVN_PROGRAM_H */
