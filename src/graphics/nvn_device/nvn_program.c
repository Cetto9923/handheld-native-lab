/* The NVN program family implementation.
 *
 * See nvn_program.h for why the state lives on the host.
 *
 * The shader key is the SHA-256 of the control section followed by the code
 * area, and it is the same key the offline translator writes down, so that a
 * shader the guest asks for and a shader that was translated ahead of time
 * can be matched. The key of a stage whose code area cannot be read is not
 * computed: guessing it would produce a name that matches nothing.
 */
#include "nvn_program.h"

#include "nvn_device.h"
#include "nvn_memory_pool.h"
#include "nvn_sha256.h"

#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* How many stages a program can have and how large the control section of a
 * stage is. The control section of this generation of the SDK is 0x880 bytes
 * and its own length is written at its offset 0x20. */
#define NVN_PROGRAM_MAX_STAGES 6
#define NVN_PROGRAM_CONTROL_SIZE 0x880
#define NVN_PROGRAM_MAX_PROGRAMS 16

/* The offsets inside the control section. */
#define NVN_PROGRAM_CONTROL_LENGTH_OFFSET 0x20
#define NVN_PROGRAM_CONTROL_CODE_LENGTH_OFFSET 0x704

/* The code area length that is still believable. Anything larger is not a
 * shader. */
#define NVN_PROGRAM_MAX_CODE_LENGTH (1u << 24)

/* What the guest hands over for one stage of a program. */
typedef struct {
    uint64_t data;
    const void *control;
} nvn_shader_data;

/* What the guest hands over for one stage of a program when it uses the
 * extended entry: the same two fields and one more the runtime does not use. */
typedef struct {
    uint64_t data;
    const void *control;
    uint64_t extension;
} nvn_shader_data_ext;

typedef struct {
    void *program;
    int stage_count;
    struct {
        uint64_t data_address;
        uint64_t extension;
        uint32_t code_length;
        uint32_t control_length;
        int keyed;
        char key[NVN_SHA256_KEY_HEX_SIZE];
        unsigned char control[NVN_PROGRAM_CONTROL_SIZE];
    } stages[NVN_PROGRAM_MAX_STAGES];
    char debug_label[64];
    int in_use;
} nvn_program_state;

static pthread_mutex_t s_program_lock = PTHREAD_MUTEX_INITIALIZER;
static nvn_program_state s_programs[NVN_PROGRAM_MAX_PROGRAMS];
static nvn_program_stats s_stats;

static nvn_program_state *program_find(void *program, int create) {
    size_t i;
    nvn_program_state *free_slot = NULL;
    if (!program) return NULL;
    for (i = 0; i < NVN_PROGRAM_MAX_PROGRAMS; i++) {
        if (s_programs[i].in_use) {
            if (s_programs[i].program == program) return &s_programs[i];
        } else if (!free_slot) {
            free_slot = &s_programs[i];
        }
    }
    if (!create || !free_slot) return NULL;
    memset(free_slot, 0, sizeof(*free_slot));
    free_slot->program = program;
    free_slot->in_use = 1;
    return free_slot;
}

/* Take one stage over from the guest.
 *
 * The control section is copied right here, because it is only valid while
 * the guest is inside this call. The code area is read through the memory
 * pool, because that is where the guest put it and it stays valid as long as
 * the program is in use.
 *
 * The return value says whether the stage got a key. */
static int capture_stage(nvn_program_state *p, int stage, uint64_t data, const void *control,
                          uint64_t extension) {
    uint32_t code_length = 0;
    uint32_t own_length = 0;
    size_t control_length = NVN_PROGRAM_CONTROL_SIZE;
    size_t available = 0;
    unsigned char *code;
    unsigned char digest[NVN_SHA256_DIGEST_SIZE];
    nvn_sha256_context ctx;

    p->stages[stage].data_address = data;
    p->stages[stage].extension = extension;
    p->stages[stage].keyed = 0;
    p->stages[stage].key[0] = '\0';
    p->stages[stage].code_length = 0;
    p->stages[stage].control_length = 0;

    if (!control) {
        fprintf(stderr, "[nvn] nvnProgramSetShaders: program=%p stage=%d has no control section; "
                        "there is nothing to describe this stage with\n",
                p->program, stage);
        return 0;
    }

    memcpy(&own_length, (const unsigned char *)control + NVN_PROGRAM_CONTROL_LENGTH_OFFSET, 4);
    memcpy(&code_length, (const unsigned char *)control + NVN_PROGRAM_CONTROL_CODE_LENGTH_OFFSET, 4);
    /* A control section of its own length is used as it is, a longer one only
     * by its first 0x880 bytes. */
    if (own_length >= 0x760 && own_length < NVN_PROGRAM_CONTROL_SIZE) control_length = own_length;

    memcpy(p->stages[stage].control, control, control_length);
    p->stages[stage].control_length = (uint32_t)control_length;
    p->stages[stage].code_length = code_length;

    if (code_length == 0 || code_length >= NVN_PROGRAM_MAX_CODE_LENGTH) {
        fprintf(stderr, "[nvn] nvnProgramSetShaders: program=%p stage=%d data=0x%" PRIx64
                        " control=%p code_length=%u; the control section names no code area, so no key "
                        "can be computed\n",
                p->program, stage, data, control, code_length);
        return 0;
    }

    code = (unsigned char *)nvn_memory_pool_resolve_address(data, &available);
    if (!code || available < code_length) {
        fprintf(stderr, "[nvn] nvnProgramSetShaders: program=%p stage=%d data=0x%" PRIx64
                        " control=%p code_length=%u; the code area is not inside a memory pool this runtime "
                        "knows (available=%zu), so no key can be computed\n",
                p->program, stage, data, control, code_length, available);
        return 0;
    }

    nvn_sha256_init(&ctx);
    nvn_sha256_update(&ctx, p->stages[stage].control, control_length);
    nvn_sha256_update(&ctx, code, code_length);
    nvn_sha256_final(&ctx, digest);
    nvn_sha256_hex(digest, p->stages[stage].key);
    p->stages[stage].keyed = 1;
    s_stats.keyed++;
    fprintf(stderr, "[nvn] nvnProgramSetShaders: program=%p stage=%d data=0x%" PRIx64 " control=%p"
                    " code_length=%u control_length=%zu key=%s\n",
            p->program, stage, data, control, code_length, control_length, p->stages[stage].key);
    return 1;
}

int nvn_program_initialize(void *program, void *device) {
    nvn_program_state *p;
    pthread_mutex_lock(&s_program_lock);
    s_stats.calls++;
    if (!nvn_device_is_ready(device)) {
        s_stats.refused++;
        pthread_mutex_unlock(&s_program_lock);
        fprintf(stderr, "[nvn] nvnProgramInitialize: program=%p device=%p; the device is not an "
                        "initialized device, so the program is refused rather than faked\n",
                program, device);
        return 0;
    }
    p = program_find(program, 1);
    if (!p) {
        s_stats.refused++;
        pthread_mutex_unlock(&s_program_lock);
        fprintf(stderr, "[nvn] nvnProgramInitialize: no room for another program\n");
        return 0;
    }
    p->stage_count = 0;
    s_stats.initialized++;
    pthread_mutex_unlock(&s_program_lock);
    fprintf(stderr, "[nvn] nvnProgramInitialize: program=%p device=%p -> true\n", program, device);
    return 1;
}

/* The two entry points that hand over the shaders differ only in the layout
 * of one stage: the extended one has one more field. */
static int set_shaders(void *program, int count, const void *shader_data, int extended) {
    nvn_program_state *p;
    int i;
    int keyed = 0;

    pthread_mutex_lock(&s_program_lock);
    s_stats.calls++;
    if (extended) {
        s_stats.shaders_ext_set++;
    } else {
        s_stats.shaders_set++;
    }
    if (count < 0 || count > NVN_PROGRAM_MAX_STAGES) {
        s_stats.refused++;
        pthread_mutex_unlock(&s_program_lock);
        fprintf(stderr, "[nvn] nvnProgramSetShaders: program=%p stage count %d; this runtime carries at "
                        "most %d stages, so the call is refused rather than truncated\n",
                program, count, NVN_PROGRAM_MAX_STAGES);
        return 0;
    }
    p = program_find(program, 1);
    if (!p) {
        s_stats.refused++;
        pthread_mutex_unlock(&s_program_lock);
        fprintf(stderr, "[nvn] nvnProgramSetShaders: no room for another program\n");
        return 0;
    }
    p->stage_count = count;
    for (i = 0; i < count; i++) {
        uint64_t data;
        const void *control;
        uint64_t extension = 0;
        if (extended) {
            const nvn_shader_data_ext *entry = &((const nvn_shader_data_ext *)shader_data)[i];
            data = entry->data;
            control = entry->control;
            extension = entry->extension;
        } else {
            const nvn_shader_data *entry = &((const nvn_shader_data *)shader_data)[i];
            data = entry->data;
            control = entry->control;
        }
        s_stats.stages++;
        if (capture_stage(p, i, data, control, extension)) keyed++;
    }
    pthread_mutex_unlock(&s_program_lock);
    fprintf(stderr, "[nvn] nvnProgramSetShaders%s: program=%p stages=%d keyed=%d\n",
            extended ? "Ext" : "", program, count, keyed);
    return 1;
}

int nvn_program_set_shaders(void *program, int count, const void *shader_data) {
    return set_shaders(program, count, shader_data, 0);
}

int nvn_program_set_shaders_ext(void *program, int count, const void *shader_data) {
    return set_shaders(program, count, shader_data, 1);
}

void nvn_program_finalize(void *program) {
    pthread_mutex_lock(&s_program_lock);
    s_stats.calls++;
    {
        nvn_program_state *p = program_find(program, 0);
        if (p) {
            p->stage_count = 0;
            p->in_use = 0;
            s_stats.finalized++;
        }
    }
    pthread_mutex_unlock(&s_program_lock);
    fprintf(stderr, "[nvn] nvnProgramFinalize: program=%p\n", program);
    NVN_VOID_RETURN();
}

void nvn_program_set_debug_label(void *program, const char *label) {
    pthread_mutex_lock(&s_program_lock);
    s_stats.calls++;
    {
        nvn_program_state *p = program_find(program, 1);
        if (p) snprintf(p->debug_label, sizeof(p->debug_label), "%s", label ? label : "");
    }
    pthread_mutex_unlock(&s_program_lock);
    NVN_VOID_RETURN();
}

/* The subroutine linkage. The guest hands over a map per stage; the runtime
 * records that it arrived and does not try to interpret it, because the
 * layout of a linkage map is a property of the SDK. */
int nvn_program_set_subroutine_linkage(void *program, int count, const void *linkage_map) {
    pthread_mutex_lock(&s_program_lock);
    s_stats.calls++;
    pthread_mutex_unlock(&s_program_lock);
    fprintf(stderr, "[nvn] nvnProgramSetSubroutineLinkage: program=%p stages=%d map=%p; the linkage map "
                    "was taken over, but its layout belongs to the SDK and is not interpreted here\n",
            program, count, linkage_map);
    return 1;
}

/* Sample shading.
 *
 * The signature of this entry point is not in the header this runtime was
 * built against, and the guest has not called it yet, so the arguments are
 * taken as plain words and written down as they are. Nothing is dereferenced,
 * so a signature with fewer or more arguments cannot do any harm here. */
void nvn_program_set_sample_shading(void *program, uint64_t first, uint64_t second) {
    pthread_mutex_lock(&s_program_lock);
    s_stats.calls++;
    pthread_mutex_unlock(&s_program_lock);
    fprintf(stderr, "[nvn] nvnProgramSetSampleShading: program=%p args=0x%" PRIx64 ",0x%" PRIx64
                    "; the signature of this entry point is not in the header this runtime was built "
                    "against, so the arguments are recorded as they came\n",
            program, first, second);
    NVN_VOID_RETURN();
}

/* The subtile size.
 *
 * The signature of this entry point is not in the header this runtime was
 * built against. The answer is zero, and the second result register is zero
 * as well, so that an answer that is wider than one word does not hand the
 * guest a leftover. */
int nvn_program_get_subtile_size(void *program) {
    pthread_mutex_lock(&s_program_lock);
    s_stats.calls++;
    pthread_mutex_unlock(&s_program_lock);
    fprintf(stderr, "[nvn] nvnProgramGetSubtileSize: program=%p -> 0; the signature of this entry point "
                    "is not in the header this runtime was built against, so no size is invented\n",
            program);
    __asm__ volatile("mov x1, xzr");
    return 0;
}

/* ------------------------------------------------------------------ */
/* The names                                                           */
/* ------------------------------------------------------------------ */

static const nvn_program_family_entry s_program_family[] = {
    { "nvnProgramInitialize", (void *)(uintptr_t)nvn_program_initialize },
    { "nvnProgramFinalize", (void *)(uintptr_t)nvn_program_finalize },
    { "nvnProgramSetDebugLabel", (void *)(uintptr_t)nvn_program_set_debug_label },
    { "nvnProgramSetShaders", (void *)(uintptr_t)nvn_program_set_shaders },
    { "nvnProgramSetShadersExt", (void *)(uintptr_t)nvn_program_set_shaders_ext },
    { "nvnProgramSetSubroutineLinkage", (void *)(uintptr_t)nvn_program_set_subroutine_linkage },
    { "nvnProgramSetSampleShading", (void *)(uintptr_t)nvn_program_set_sample_shading },
    { "nvnProgramGetSubtileSize", (void *)(uintptr_t)nvn_program_get_subtile_size },
};

size_t nvn_program_family_count(void) {
    return sizeof(s_program_family) / sizeof(s_program_family[0]);
}

const nvn_program_family_entry *nvn_program_family_entry_at(size_t index) {
    if (index >= nvn_program_family_count()) return NULL;
    return &s_program_family[index];
}

void nvn_program_get_stats(nvn_program_stats *out) {
    size_t i;
    if (!out) return;
    pthread_mutex_lock(&s_program_lock);
    *out = s_stats;
    out->live_programs = 0;
    for (i = 0; i < NVN_PROGRAM_MAX_PROGRAMS; i++) {
        if (s_programs[i].in_use) out->live_programs++;
    }
    pthread_mutex_unlock(&s_program_lock);
}

int nvn_program_get_stage_info(void *program, int stage, char key[NVN_SHA256_KEY_HEX_SIZE],
                                uint32_t *code_length, uint32_t *control_length) {
    int found = 0;
    if (code_length) *code_length = 0;
    if (control_length) *control_length = 0;
    if (!key) return 0;
    key[0] = '\0';
    pthread_mutex_lock(&s_program_lock);
    {
        nvn_program_state *p = program_find(program, 0);
        if (p && stage >= 0 && stage < NVN_PROGRAM_MAX_STAGES && p->stages[stage].keyed) {
            snprintf(key, NVN_SHA256_KEY_HEX_SIZE, "%s", p->stages[stage].key);
            if (code_length) *code_length = p->stages[stage].code_length;
            if (control_length) *control_length = p->stages[stage].control_length;
            found = 1;
        }
    }
    pthread_mutex_unlock(&s_program_lock);
    return found;
}

int nvn_program_get_stage_control(void *program, int stage, void *destination, size_t size) {
    int found = 0;
    if (!destination || size == 0) return 0;
    pthread_mutex_lock(&s_program_lock);
    {
        nvn_program_state *p = program_find(program, 0);
        if (p && stage >= 0 && stage < NVN_PROGRAM_MAX_STAGES && p->stages[stage].control_length > 0) {
            size_t take = p->stages[stage].control_length;
            if (take > size) take = size;
            memcpy(destination, p->stages[stage].control, take);
            found = 1;
        }
    }
    pthread_mutex_unlock(&s_program_lock);
    return found;
}
