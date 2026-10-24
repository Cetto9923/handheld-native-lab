/* The NVN sampler object family implementation.
 *
 * See nvn_sampler.h for why the state lives on the host and what a sampler is.
 *
 * The guest hands a sampler builder to nvnSamplerInitialize and from then on
 * the sampler holds a copy of the builder's parameters. The copy is taken at
 * the moment of the initialization, so a sampler is a snapshot: the builder
 * can change afterwards without touching the sampler that was already made.
 *
 * The evidence for the values:
 *
 *   - The signatures of the fifteen entry points are the ones of the vendored
 *     NVN header (src/graphics/nvn/include/nvn/nvn.h:1391-1406). That header
 *     is a reconstruction of NVN 53.105 taken from another game (Darksiders
 *     II), so its signatures are NOT confirmed for this game, which is Unreal
 *     Engine 5.6.1. The reference host that runs this very game agrees with
 *     them (.local/refs/nxnative/dm/gfx/nvn/nvn_protos.h:234-248).
 *   - The parameters that a sampler carries are the ones of the reference
 *     host's samp_par (nvn_obj.h:49): the minification and the magnification
 *     filter, the three wrap modes, the compare mode and function, the
 *     reduction filter, the level of detail range, the level of detail bias,
 *     the anisotropy and the border colour as four floats.
 *   - The defaults are the ones of that reference host (hle_nvn.c:533): the
 *     minification filter NEAREST_MIPMAP_LINEAR, the magnification filter
 *     LINEAR, the wrap mode REPEAT on all three axes, the compare function
 *     LESS, the level of detail clamp at -1000 and 1000 and the anisotropy at
 *     1. The enumeration values are the ones of the vendored header.
 *   - The comparison compares exactly the fields the reference compares
 *     (hle_nvn.c:1935): the two filters, the three wrap modes, the compare
 *     mode and function, the reduction filter, the level of detail range, the
 *     level of detail bias, the anisotropy and the border colour. The int and
 *     the unsigned int form of the border colour do NOT take part, because
 *     the reference has no such form: its sampler holds the colour as four
 *     floats and nothing else.
 *
 * The level of detail snap is not part of a sampler: the reference ignores it
 * (hle_nvn.c:548, "No Metal equivalent, ignored") and the sampler object has
 * no getter for it.
 */
#include "nvn_sampler.h"

#include "nvn_device.h"
#include "nvn_sampler_builder.h"

#include "../nvn/include/nvn/nvn.h"

#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    void *sampler;
    NVNminFilter min_filter;
    NVNmagFilter mag_filter;
    NVNwrapMode wrap_s;
    NVNwrapMode wrap_t;
    NVNwrapMode wrap_r;
    NVNcompareMode compare_mode;
    NVNcompareFunc compare_func;
    NVNsamplerReduction reduction_filter;
    float lod_min;
    float lod_max;
    float lod_bias;
    float max_anisotropy;
    float border_color[4];
    int border_colori[4];
    uint32_t border_colorui[4];
    uint64_t debug_id;
    char debug_label[64];
    int initialized;
    int in_use;
} nvn_sampler_state;

/* The names of this family, in the order of the family table. That order is
 * the order the guest itself resolves the names in (the trap slots 71 to 85 of
 * the device run of 2026-10-25). The index is used for the "only the first
 * call is logged" bookkeeping. */
enum {
    SP_INITIALIZE,
    SP_FINALIZE,
    SP_SET_DEBUG_LABEL,
    SP_GET_MIN_MAG_FILTER,
    SP_GET_WRAP_MODE,
    SP_GET_LOD_CLAMP,
    SP_GET_LOD_BIAS,
    SP_GET_COMPARE,
    SP_GET_BORDER_COLOR,
    SP_GET_BORDER_COLORI,
    SP_GET_BORDER_COLORUI,
    SP_GET_MAX_ANISOTROPY,
    SP_GET_REDUCTION_FILTER,
    SP_COMPARE,
    SP_GET_DEBUG_ID,
    SP_NAME_COUNT
};

static pthread_mutex_t s_sampler_lock = PTHREAD_MUTEX_INITIALIZER;
static nvn_sampler_state s_samplers[NVN_SAMPLER_MAX_SAMPLERS];
static nvn_sampler_stats s_stats;
static int s_logged[SP_NAME_COUNT];

/* The defaults of the reference host (hle_nvn.c:533). */
static nvn_sampler_state default_state(void) {
    nvn_sampler_state s;
    memset(&s, 0, sizeof(s));
    s.min_filter = NVN_MIN_FILTER_NEAREST_MIPMAP_LINEAR;
    s.mag_filter = NVN_MAG_FILTER_LINEAR;
    s.wrap_s = NVN_WRAP_MODE_REPEAT;
    s.wrap_t = NVN_WRAP_MODE_REPEAT;
    s.wrap_r = NVN_WRAP_MODE_REPEAT;
    s.compare_mode = NVN_COMPARE_MODE_NONE;
    s.compare_func = NVN_COMPARE_FUNC_LESS;
    s.reduction_filter = NVN_SAMPLER_REDUCTION_AVERAGE;
    s.lod_min = -1000.0f;
    s.lod_max = 1000.0f;
    s.lod_bias = 0.0f;
    s.max_anisotropy = 1.0f;
    return s;
}

/* The sampler state for one guest sampler address. A sampler that was never
 * initialized has no state to answer with, and the functions that need one say
 * so instead of inventing one. */
static nvn_sampler_state *sampler_find(void *sampler, int create) {
    size_t i;
    nvn_sampler_state *free_slot = NULL;
    if (!sampler) return NULL;
    for (i = 0; i < NVN_SAMPLER_MAX_SAMPLERS; i++) {
        if (s_samplers[i].in_use) {
            if (s_samplers[i].sampler == sampler) return &s_samplers[i];
        } else if (!free_slot) {
            free_slot = &s_samplers[i];
        }
    }
    if (!create || !free_slot) return NULL;
    *free_slot = default_state();
    free_slot->sampler = sampler;
    free_slot->in_use = 1;
    return free_slot;
}

/* Mark a name as logged and say whether this call is the first one. Called
 * with the lock held. */
static int mark_logged(int index) {
    if (s_logged[index]) return 0;
    s_logged[index] = 1;
    return 1;
}

/* Take the parameters of the builder into the sampler. */
static void apply_parameters(nvn_sampler_state *s, const nvn_sampler_builder_parameters *p) {
    s->min_filter = (NVNminFilter)p->min_filter;
    s->mag_filter = (NVNmagFilter)p->mag_filter;
    s->wrap_s = (NVNwrapMode)p->wrap_s;
    s->wrap_t = (NVNwrapMode)p->wrap_t;
    s->wrap_r = (NVNwrapMode)p->wrap_r;
    s->compare_mode = (NVNcompareMode)p->compare_mode;
    s->compare_func = (NVNcompareFunc)p->compare_func;
    s->reduction_filter = (NVNsamplerReduction)p->reduction_filter;
    s->lod_min = p->lod_min;
    s->lod_max = p->lod_max;
    s->lod_bias = p->lod_bias;
    s->max_anisotropy = p->max_anisotropy;
    memcpy(s->border_color, p->border_color, sizeof(s->border_color));
    memcpy(s->border_colori, p->border_colori, sizeof(s->border_colori));
    memcpy(s->border_colorui, p->border_colorui, sizeof(s->border_colorui));
}

/* ------------------------------------------------------------------ */
/* nvnSamplerInitialize                                                 */
/* ------------------------------------------------------------------ */

/* nvnSamplerInitialize(sampler, builder) -> boolean.
 *
 * The reference host copies the parameters of the builder into the sampler
 * and answers true (hle_nvn.c:558). Here the copy goes into the host's own
 * table, keyed by the address of the sampler, because the guest's sampler
 * object has a layout this runtime does not know.
 *
 * A builder that was never prepared is refused: it has no parameters, and a
 * sampler made out of it would answer with values nobody ever set. */
int nvnSamplerInitialize(void *sampler, const void *builder) {
    nvn_sampler_builder_parameters params;
    nvn_sampler_state *slot = NULL;
    int ready = 0;
    int refused = 0;
    const char *why = NULL;
    int first = 0;

    /* The copy is taken before the lock of this family is held: the builder
     * family has its own lock, and the copy is the only thing this call
     * needs from it. */
    memset(&params, 0, sizeof(params));
    if (!nvn_sampler_builder_copy_parameters(builder, &params)) refused = 1;

    pthread_mutex_lock(&s_sampler_lock);
    s_stats.calls++;
    if (!sampler) {
        why = "there is no sampler to initialize";
    } else if (refused) {
        why = "the builder was never prepared, so it has no parameters";
    } else {
        slot = sampler_find(sampler, 1);
        if (!slot) {
            why = "no room for another sampler";
        } else {
            apply_parameters(slot, &params);
            /* The debug id comes from the device's own debug domain id
             * generator, the same one every other object of this runtime
             * uses. The generator hands out an id from the device's own
             * sequence and ignores its arguments. */
            slot->debug_id = nvn_device_generate_debug_domain_id(params.device, "nvnSampler");
            slot->initialized = 1;
            s_stats.initialized++;
            ready = 1;
        }
    }
    if (!ready) s_stats.refused++;
    first = mark_logged(SP_INITIALIZE);
    pthread_mutex_unlock(&s_sampler_lock);

    if (!ready) {
        fprintf(stderr, "[nvn] nvnSamplerInitialize: sampler=%p builder=%p was refused: %s\n", sampler, builder, why);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnSamplerInitialize: sampler=%p builder=%p -> the parameters of the builder are "
                        "copied into the sampler's own state\n",
                sampler, builder);
    }
    return ready ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* nvnSamplerFinalize                                                   */
/* ------------------------------------------------------------------ */

/* nvnSamplerFinalize(sampler).
 *
 * The reference host clears the magic of the object (hle_nvn.c:561). Here the
 * state of the sampler is dropped, so that a getter on that address afterwards
 * says that the sampler was never initialized instead of answering with the
 * values of a sampler that does not exist any more. */
void nvnSamplerFinalize(void *sampler) {
    nvn_sampler_state *found;
    int refused = 0;
    int first = 0;
    pthread_mutex_lock(&s_sampler_lock);
    s_stats.calls++;
    found = sampler_find(sampler, 0);
    if (found) {
        found->initialized = 0;
        found->in_use = 0;
        s_stats.finalized++;
    } else {
        s_stats.refused++;
        refused = 1;
    }
    first = mark_logged(SP_FINALIZE);
    pthread_mutex_unlock(&s_sampler_lock);
    if (refused) {
        fprintf(stderr, "[nvn] nvnSamplerFinalize: sampler=%p was never initialized, so there is nothing to "
                        "finalize\n",
                sampler);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnSamplerFinalize: sampler=%p\n", sampler);
    }
    NVN_VOID_RETURN();
}

/* ------------------------------------------------------------------ */
/* nvnSamplerSetDebugLabel                                              */
/* ------------------------------------------------------------------ */

/* nvnSamplerSetDebugLabel(sampler, label).
 *
 * The reference host throws the label away (hle_nvn.c:1924). Here it is kept
 * on the host with the rest of the state of the sampler, so that the log can
 * name the sampler the guest is working with.
 *
 * The label is only recorded for a sampler that exists, because a label on an
 * address the guest never initialized would take a slot of the table away from
 * a sampler that does exist. The reference drops the label in every case, so
 * nothing the guest can observe changes. */
void nvnSamplerSetDebugLabel(void *sampler, const char *label) {
    nvn_sampler_state *found;
    int refused = 0;
    int first = 0;
    pthread_mutex_lock(&s_sampler_lock);
    s_stats.calls++;
    found = sampler_find(sampler, 0);
    if (found && found->initialized) {
        snprintf(found->debug_label, sizeof(found->debug_label), "%s", label ? label : "");
    } else {
        s_stats.refused++;
        refused = 1;
    }
    first = mark_logged(SP_SET_DEBUG_LABEL);
    pthread_mutex_unlock(&s_sampler_lock);
    if (refused) {
        fprintf(stderr, "[nvn] nvnSamplerSetDebugLabel: sampler=%p was never initialized, so the label was not "
                        "recorded\n",
                sampler);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnSamplerSetDebugLabel: sampler=%p \"%s\"\n", sampler, label ? label : "");
    }
    NVN_VOID_RETURN();
}

/* ------------------------------------------------------------------ */
/* The getters                                                          */
/* ------------------------------------------------------------------ */

/* Every getter answers with the sampler's own values. A sampler that was
 * never initialized has none, so the getter says so and answers with the
 * defaults: a defined answer that the guest cannot mistake for a value of
 * this sampler, because the log names the refusal. */

void nvnSamplerGetMinMagFilter(const void *sampler, NVNminFilter *min_filter, NVNmagFilter *mag_filter) {
    nvn_sampler_state s = default_state();
    nvn_sampler_state *found;
    int refused = 0;
    int first = 0;
    pthread_mutex_lock(&s_sampler_lock);
    s_stats.calls++;
    s_stats.gets++;
    found = sampler_find((void *)(uintptr_t)sampler, 0);
    if (found && found->initialized) {
        s = *found;
    } else {
        s_stats.refused++;
        refused = 1;
    }
    first = mark_logged(SP_GET_MIN_MAG_FILTER);
    pthread_mutex_unlock(&s_sampler_lock);
    if (min_filter) *min_filter = s.min_filter;
    if (mag_filter) *mag_filter = s.mag_filter;
    if (refused) {
        fprintf(stderr, "[nvn] nvnSamplerGetMinMagFilter: sampler=%p was never initialized; the answer is the "
                        "default, not a value of this sampler\n",
                sampler);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnSamplerGetMinMagFilter: sampler=%p min=0x%x mag=0x%x\n", sampler,
                (unsigned)s.min_filter, (unsigned)s.mag_filter);
    }
    NVN_VOID_RETURN();
}

void nvnSamplerGetWrapMode(const void *sampler, NVNwrapMode *s_wrap, NVNwrapMode *t_wrap, NVNwrapMode *r_wrap) {
    nvn_sampler_state s = default_state();
    nvn_sampler_state *found;
    int refused = 0;
    int first = 0;
    pthread_mutex_lock(&s_sampler_lock);
    s_stats.calls++;
    s_stats.gets++;
    found = sampler_find((void *)(uintptr_t)sampler, 0);
    if (found && found->initialized) {
        s = *found;
    } else {
        s_stats.refused++;
        refused = 1;
    }
    first = mark_logged(SP_GET_WRAP_MODE);
    pthread_mutex_unlock(&s_sampler_lock);
    if (s_wrap) *s_wrap = s.wrap_s;
    if (t_wrap) *t_wrap = s.wrap_t;
    if (r_wrap) *r_wrap = s.wrap_r;
    if (refused) {
        fprintf(stderr, "[nvn] nvnSamplerGetWrapMode: sampler=%p was never initialized; the answer is the default, "
                        "not a value of this sampler\n",
                sampler);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnSamplerGetWrapMode: sampler=%p s=0x%x t=0x%x r=0x%x\n", sampler, (unsigned)s.wrap_s,
                (unsigned)s.wrap_t, (unsigned)s.wrap_r);
    }
    NVN_VOID_RETURN();
}

void nvnSamplerGetLodClamp(const void *sampler, float *min_lod, float *max_lod) {
    nvn_sampler_state s = default_state();
    nvn_sampler_state *found;
    int refused = 0;
    int first = 0;
    pthread_mutex_lock(&s_sampler_lock);
    s_stats.calls++;
    s_stats.gets++;
    found = sampler_find((void *)(uintptr_t)sampler, 0);
    if (found && found->initialized) {
        s = *found;
    } else {
        s_stats.refused++;
        refused = 1;
    }
    first = mark_logged(SP_GET_LOD_CLAMP);
    pthread_mutex_unlock(&s_sampler_lock);
    if (min_lod) *min_lod = s.lod_min;
    if (max_lod) *max_lod = s.lod_max;
    if (refused) {
        fprintf(stderr, "[nvn] nvnSamplerGetLodClamp: sampler=%p was never initialized; the answer is the default, "
                        "not a value of this sampler\n",
                sampler);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnSamplerGetLodClamp: sampler=%p min=%g max=%g\n", sampler, (double)s.lod_min,
                (double)s.lod_max);
    }
    NVN_VOID_RETURN();
}

float nvnSamplerGetLodBias(const void *sampler) {
    nvn_sampler_state s = default_state();
    nvn_sampler_state *found;
    int refused = 0;
    int first = 0;
    float bias;
    pthread_mutex_lock(&s_sampler_lock);
    s_stats.calls++;
    s_stats.gets++;
    found = sampler_find((void *)(uintptr_t)sampler, 0);
    if (found && found->initialized) {
        s = *found;
    } else {
        s_stats.refused++;
        refused = 1;
    }
    bias = s.lod_bias;
    first = mark_logged(SP_GET_LOD_BIAS);
    pthread_mutex_unlock(&s_sampler_lock);
    if (refused) {
        fprintf(stderr, "[nvn] nvnSamplerGetLodBias: sampler=%p was never initialized; the answer is the default, "
                        "not a value of this sampler\n",
                sampler);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnSamplerGetLodBias: sampler=%p bias=%g\n", sampler, (double)bias);
    }
    return bias;
}

void nvnSamplerGetCompare(const void *sampler, NVNcompareMode *mode, NVNcompareFunc *func) {
    nvn_sampler_state s = default_state();
    nvn_sampler_state *found;
    int refused = 0;
    int first = 0;
    pthread_mutex_lock(&s_sampler_lock);
    s_stats.calls++;
    s_stats.gets++;
    found = sampler_find((void *)(uintptr_t)sampler, 0);
    if (found && found->initialized) {
        s = *found;
    } else {
        s_stats.refused++;
        refused = 1;
    }
    first = mark_logged(SP_GET_COMPARE);
    pthread_mutex_unlock(&s_sampler_lock);
    if (mode) *mode = s.compare_mode;
    if (func) *func = s.compare_func;
    if (refused) {
        fprintf(stderr, "[nvn] nvnSamplerGetCompare: sampler=%p was never initialized; the answer is the default, "
                        "not a value of this sampler\n",
                sampler);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnSamplerGetCompare: sampler=%p mode=0x%x func=0x%x\n", sampler,
                (unsigned)s.compare_mode, (unsigned)s.compare_func);
    }
    NVN_VOID_RETURN();
}

/* The border colour is four components. The float form is the one the
 * reference host keeps (nvn_obj.h:49); this runtime's builder also keeps the
 * int and the unsigned int form, because the guest can set any of the three,
 * and each getter answers with the form it belongs to. */
void nvnSamplerGetBorderColor(const void *sampler, float *color) {
    nvn_sampler_state s = default_state();
    nvn_sampler_state *found;
    int refused = 0;
    int first = 0;
    pthread_mutex_lock(&s_sampler_lock);
    s_stats.calls++;
    s_stats.gets++;
    found = sampler_find((void *)(uintptr_t)sampler, 0);
    if (found && found->initialized) {
        s = *found;
    } else {
        s_stats.refused++;
        refused = 1;
    }
    first = mark_logged(SP_GET_BORDER_COLOR);
    pthread_mutex_unlock(&s_sampler_lock);
    if (color) memcpy(color, s.border_color, sizeof(s.border_color));
    if (refused) {
        fprintf(stderr, "[nvn] nvnSamplerGetBorderColor: sampler=%p was never initialized; the answer is the "
                        "default, not a value of this sampler\n",
                sampler);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnSamplerGetBorderColor: sampler=%p\n", sampler);
    }
    NVN_VOID_RETURN();
}

void nvnSamplerGetBorderColori(const void *sampler, int *color) {
    nvn_sampler_state s = default_state();
    nvn_sampler_state *found;
    int refused = 0;
    int first = 0;
    pthread_mutex_lock(&s_sampler_lock);
    s_stats.calls++;
    s_stats.gets++;
    found = sampler_find((void *)(uintptr_t)sampler, 0);
    if (found && found->initialized) {
        s = *found;
    } else {
        s_stats.refused++;
        refused = 1;
    }
    first = mark_logged(SP_GET_BORDER_COLORI);
    pthread_mutex_unlock(&s_sampler_lock);
    if (color) memcpy(color, s.border_colori, sizeof(s.border_colori));
    if (refused) {
        fprintf(stderr, "[nvn] nvnSamplerGetBorderColori: sampler=%p was never initialized; the answer is the "
                        "default, not a value of this sampler\n",
                sampler);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnSamplerGetBorderColori: sampler=%p\n", sampler);
    }
    NVN_VOID_RETURN();
}

void nvnSamplerGetBorderColorui(const void *sampler, uint32_t *color) {
    nvn_sampler_state s = default_state();
    nvn_sampler_state *found;
    int refused = 0;
    int first = 0;
    pthread_mutex_lock(&s_sampler_lock);
    s_stats.calls++;
    s_stats.gets++;
    found = sampler_find((void *)(uintptr_t)sampler, 0);
    if (found && found->initialized) {
        s = *found;
    } else {
        s_stats.refused++;
        refused = 1;
    }
    first = mark_logged(SP_GET_BORDER_COLORUI);
    pthread_mutex_unlock(&s_sampler_lock);
    if (color) memcpy(color, s.border_colorui, sizeof(s.border_colorui));
    if (refused) {
        fprintf(stderr, "[nvn] nvnSamplerGetBorderColorui: sampler=%p was never initialized; the answer is the "
                        "default, not a value of this sampler\n",
                sampler);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnSamplerGetBorderColorui: sampler=%p\n", sampler);
    }
    NVN_VOID_RETURN();
}

float nvnSamplerGetMaxAnisotropy(const void *sampler) {
    nvn_sampler_state s = default_state();
    nvn_sampler_state *found;
    int refused = 0;
    int first = 0;
    float anisotropy;
    pthread_mutex_lock(&s_sampler_lock);
    s_stats.calls++;
    s_stats.gets++;
    found = sampler_find((void *)(uintptr_t)sampler, 0);
    if (found && found->initialized) {
        s = *found;
    } else {
        s_stats.refused++;
        refused = 1;
    }
    anisotropy = s.max_anisotropy;
    first = mark_logged(SP_GET_MAX_ANISOTROPY);
    pthread_mutex_unlock(&s_sampler_lock);
    if (refused) {
        fprintf(stderr, "[nvn] nvnSamplerGetMaxAnisotropy: sampler=%p was never initialized; the answer is the "
                        "default, not a value of this sampler\n",
                sampler);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnSamplerGetMaxAnisotropy: sampler=%p anisotropy=%g\n", sampler, (double)anisotropy);
    }
    return anisotropy;
}

NVNsamplerReduction nvnSamplerGetReductionFilter(const void *sampler) {
    nvn_sampler_state s = default_state();
    nvn_sampler_state *found;
    int refused = 0;
    int first = 0;
    NVNsamplerReduction reduction;
    pthread_mutex_lock(&s_sampler_lock);
    s_stats.calls++;
    s_stats.gets++;
    found = sampler_find((void *)(uintptr_t)sampler, 0);
    if (found && found->initialized) {
        s = *found;
    } else {
        s_stats.refused++;
        refused = 1;
    }
    reduction = s.reduction_filter;
    first = mark_logged(SP_GET_REDUCTION_FILTER);
    pthread_mutex_unlock(&s_sampler_lock);
    if (refused) {
        fprintf(stderr, "[nvn] nvnSamplerGetReductionFilter: sampler=%p was never initialized; the answer is the "
                        "default, not a value of this sampler\n",
                sampler);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnSamplerGetReductionFilter: sampler=%p reduction=0x%x\n", sampler, (unsigned)reduction);
    }
    return reduction;
}

/* ------------------------------------------------------------------ */
/* nvnSamplerCompare                                                    */
/* ------------------------------------------------------------------ */

/* nvnSamplerCompare(a, b) -> boolean.
 *
 * Whether two samplers have the same state. The guest uses it to keep its
 * sampler pool free of duplicates (the reference host says so at
 * hle_nvn.c:1934).
 *
 * The fields that take part are exactly the ones the reference compares
 * (hle_nvn.c:1935): the minification and the magnification filter, the three
 * wrap modes, the compare mode and the compare function, the reduction
 * filter, the level of detail range, the level of detail bias, the anisotropy
 * and the border colour as four floats.
 *
 * Two fields of this runtime do NOT take part, and that is the reference's
 * doing and not an oversight here: the int and the unsigned int form of the
 * border colour (the reference's sampler has no such form), and the level of
 * detail snap (the reference ignores it and the sampler has no getter for
 * it). The debug label and the debug id are not part of the state of a
 * sampler either. */
int nvnSamplerCompare(const void *a, const void *b) {
    nvn_sampler_state x = default_state();
    nvn_sampler_state y = default_state();
    int have_x = 0;
    int have_y = 0;
    int equal = 0;
    int first = 0;
    pthread_mutex_lock(&s_sampler_lock);
    s_stats.calls++;
    s_stats.compares++;
    {
        nvn_sampler_state *found_a = sampler_find((void *)(uintptr_t)a, 0);
        nvn_sampler_state *found_b = sampler_find((void *)(uintptr_t)b, 0);
        if (found_a && found_a->initialized) {
            x = *found_a;
            have_x = 1;
        }
        if (found_b && found_b->initialized) {
            y = *found_b;
            have_y = 1;
        }
    }
    if (!have_x || !have_y) {
        s_stats.refused++;
    } else {
        equal = x.min_filter == y.min_filter && x.mag_filter == y.mag_filter && x.wrap_s == y.wrap_s &&
                x.wrap_t == y.wrap_t && x.wrap_r == y.wrap_r && x.compare_mode == y.compare_mode &&
                x.compare_func == y.compare_func && x.reduction_filter == y.reduction_filter &&
                x.lod_min == y.lod_min && x.lod_max == y.lod_max && x.lod_bias == y.lod_bias &&
                x.max_anisotropy == y.max_anisotropy && !memcmp(x.border_color, y.border_color, sizeof(x.border_color));
    }
    first = mark_logged(SP_COMPARE);
    pthread_mutex_unlock(&s_sampler_lock);

    if (!have_x || !have_y) {
        fprintf(stderr, "[nvn] nvnSamplerCompare: %s was never initialized; two samplers can only be compared "
                        "when both of them exist, so the answer is false\n",
                !have_x ? "the first sampler" : "the second sampler");
        return 0;
    }
    if (first) {
        fprintf(stderr, "[nvn] nvnSamplerCompare: a=%p b=%p -> %s\n", a, b, equal ? "the same" : "not the same");
    }
    return equal;
}

/* ------------------------------------------------------------------ */
/* nvnSamplerGetDebugID                                                 */
/* ------------------------------------------------------------------ */

/* nvnSamplerGetDebugID(sampler) -> uint64_t.
 *
 * The card for this family said that the signature of this entry point is not
 * known. It is known: the vendored NVN header has it at
 * src/graphics/nvn/include/nvn/nvn.h:1406
 *
 *     typedef uint64_t (*PFNNVNSAMPLERGETDEBUGIDPROC)(const NVNsampler*);
 *
 * and the reference host that runs this very game has the same one at
 * .local/refs/nxnative/dm/gfx/nvn/nvn_protos.h:248. The reference host does
 * not implement it: it is in its names.txt and in its thunks, but not in
 * hle_nvn.c, so the reference gives no value for it.
 *
 * The answer here is the id this runtime hands out for the object, taken from
 * the device's debug domain id generator (nvnDeviceGenerateDebugDomainId),
 * which is the same source every other object of this runtime uses for its
 * debug ids. A sampler that was never initialized has no id, so the answer is
 * zero and the log says that the sampler does not exist. */
uint64_t nvnSamplerGetDebugID(const void *sampler) {
    nvn_sampler_state *found;
    uint64_t debug_id = 0;
    int refused = 0;
    int first = 0;
    pthread_mutex_lock(&s_sampler_lock);
    s_stats.calls++;
    s_stats.gets++;
    found = sampler_find((void *)(uintptr_t)sampler, 0);
    if (found && found->initialized) {
        debug_id = found->debug_id;
    } else {
        s_stats.refused++;
        refused = 1;
    }
    first = mark_logged(SP_GET_DEBUG_ID);
    pthread_mutex_unlock(&s_sampler_lock);
    if (refused) {
        fprintf(stderr, "[nvn] nvnSamplerGetDebugID: sampler=%p was never initialized, so it has no debug id; the "
                        "answer is 0\n",
                sampler);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnSamplerGetDebugID: sampler=%p debug_id=0x%" PRIx64 "\n", sampler, debug_id);
    }
    return debug_id;
}

/* ------------------------------------------------------------------ */
/* The names                                                            */
/* ------------------------------------------------------------------ */

/* The fifteen names the guest itself resolves, in the order the guest
 * resolves them in. */
static const nvn_sampler_family_entry s_sampler_family[] = {
    { "nvnSamplerInitialize", (void *)(uintptr_t)nvnSamplerInitialize },
    { "nvnSamplerFinalize", (void *)(uintptr_t)nvnSamplerFinalize },
    { "nvnSamplerSetDebugLabel", (void *)(uintptr_t)nvnSamplerSetDebugLabel },
    { "nvnSamplerGetMinMagFilter", (void *)(uintptr_t)nvnSamplerGetMinMagFilter },
    { "nvnSamplerGetWrapMode", (void *)(uintptr_t)nvnSamplerGetWrapMode },
    { "nvnSamplerGetLodClamp", (void *)(uintptr_t)nvnSamplerGetLodClamp },
    { "nvnSamplerGetLodBias", (void *)(uintptr_t)nvnSamplerGetLodBias },
    { "nvnSamplerGetCompare", (void *)(uintptr_t)nvnSamplerGetCompare },
    { "nvnSamplerGetBorderColor", (void *)(uintptr_t)nvnSamplerGetBorderColor },
    { "nvnSamplerGetBorderColori", (void *)(uintptr_t)nvnSamplerGetBorderColori },
    { "nvnSamplerGetBorderColorui", (void *)(uintptr_t)nvnSamplerGetBorderColorui },
    { "nvnSamplerGetMaxAnisotropy", (void *)(uintptr_t)nvnSamplerGetMaxAnisotropy },
    { "nvnSamplerGetReductionFilter", (void *)(uintptr_t)nvnSamplerGetReductionFilter },
    { "nvnSamplerCompare", (void *)(uintptr_t)nvnSamplerCompare },
    { "nvnSamplerGetDebugID", (void *)(uintptr_t)nvnSamplerGetDebugID },
};

size_t nvn_sampler_family_count(void) {
    return sizeof(s_sampler_family) / sizeof(s_sampler_family[0]);
}

const nvn_sampler_family_entry *nvn_sampler_family_entry_at(size_t index) {
    if (index >= nvn_sampler_family_count()) return NULL;
    return &s_sampler_family[index];
}

void nvn_sampler_get_stats(nvn_sampler_stats *out) {
    size_t i;
    if (!out) return;
    pthread_mutex_lock(&s_sampler_lock);
    *out = s_stats;
    out->live_samplers = 0;
    for (i = 0; i < NVN_SAMPLER_MAX_SAMPLERS; i++) {
        if (s_samplers[i].in_use && s_samplers[i].initialized) out->live_samplers++;
    }
    pthread_mutex_unlock(&s_sampler_lock);
}
