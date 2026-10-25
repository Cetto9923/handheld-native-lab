/* The NVN sampler builder family implementation.
 *
 * See nvn_sampler_builder.h for why the state lives on the host.
 *
 * The guest configures a sampler by filling a builder and then handing that
 * builder to nvnSamplerInitialize. Every value the guest puts into the builder
 * is kept here, keyed by the address of the builder; the guest's own builder
 * memory is never touched, because its layout belongs to the Nintendo SDK.
 *
 * The evidence for the values:
 *
 *   - The argument order and the shape of the setters and getters come from
 *     the vendored NVN header (src/graphics/nvn/include/nvn/nvn.h), which is a
 *     reconstruction of NVN 53.105 taken from another game (Darksiders II).
 *     Its signatures are therefore NOT confirmed for this game, which is
 *     Unreal Engine 5.6.1.
 *   - The default values come from the reference host that runs this very
 *     game: .local/refs/nxnative/dm/gfx/nvn/hle_nvn.c, the
 *     nvnSamplerBuilderSetDefaults of nxnative (MIT, revision 7a029f66,
 *     dm/games/0100606026F12000). It sets the GL defaults: the minification
 *     filter NEAREST_MIPMAP_LINEAR, the magnification filter LINEAR, the wrap
 *     mode REPEAT on all three axes, the compare function LESS, the level of
 *     detail clamp at -1000 and 1000 and the anisotropy at 1. The enumeration
 *     values themselves are the ones of the vendored header.
 *   - The device survives the reset, because the reference tree keeps it
 *     across the reset as well (hle_nvn.c:533). The identity of the builder
 *     and the fact that its table slot is occupied are not part of the reset
 *     either: clearing those was the defect the render state family shipped,
 *     and it made the "the defaults really reset" check pass for the wrong
 *     reason.
 *
 * Five of the twenty five names have no signature in the vendored header, and
 * the reference tree lists them as well among the names it can only give a
 * stub (nvn_protos.h: "Names without a prototype in the header"). Those entry
 * points take their arguments as plain words, write them down as they came
 * and answer with zero; nothing is dereferenced, so a signature with fewer or
 * more arguments cannot do any harm here.
 */
#include "nvn_sampler_builder.h"

#include "nvn_device.h"

#include "../nvn/include/nvn/nvn.h"

#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include "nvn_object_table.h"

typedef struct {
    void *builder;
    void *device;
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
    float lod_snap;
    float max_anisotropy;
    float border_color[4];
    int border_colori[4];
    uint32_t border_colorui[4];
    int in_use;
} nvn_sampler_builder_state;

/* The names of this family, in the order of the family table. The index is
 * used for the "only the first call is logged" bookkeeping. */
enum {
    SB_SET_DEFAULTS,
    SB_SET_DEVICE,
    SB_SET_COMPARE,
    SB_SET_MIN_MAG_FILTER,
    SB_SET_REDUCTION_FILTER,
    SB_SET_WRAP_MODE,
    SB_SET_LOD_BIAS,
    SB_SET_LOD_CLAMP,
    SB_SET_LOD_SNAP,
    SB_SET_MAX_ANISOTROPY,
    SB_SET_BORDER_COLOR,
    SB_SET_BORDER_COLORI,
    SB_SET_BORDER_COLORUI,
    SB_GET_DEVICE,
    SB_GET_COMPARE,
    SB_GET_MIN_MAG_FILTER,
    SB_GET_REDUCTION_FILTER,
    SB_GET_WRAP_MODE,
    SB_GET_LOD_BIAS,
    SB_GET_LOD_CLAMP,
    SB_GET_LOD_SNAP,
    SB_GET_MAX_ANISOTROPY,
    SB_GET_BORDER_COLOR,
    SB_GET_BORDER_COLORI,
    SB_GET_BORDER_COLORUI,
    SB_NAME_COUNT
};

static pthread_mutex_t s_sampler_builder_lock = PTHREAD_MUTEX_INITIALIZER;
static nvn_object_table s_builders = { NULL, 0, 0, sizeof(nvn_sampler_builder_state) };
static nvn_sampler_builder_stats s_stats;
static int s_logged[SB_NAME_COUNT];

/* The defaults of the reference tree (hle_nvn.c:533). */
static nvn_sampler_builder_state default_state(void) {
    nvn_sampler_builder_state s;
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
    s.lod_snap = 0.0f;
    s.max_anisotropy = 1.0f;
    return s;
}

/* The defaults really reset every field they own.
 *
 * The identity of the builder, the device and the occupancy of the table slot
 * are kept: the device because the reference tree keeps it across the reset
 * (hle_nvn.c:533), the other two because they are not fields of the builder
 * but of this runtime's table. */
static void apply_defaults(nvn_sampler_builder_state *s) {
    nvn_sampler_builder_state d = default_state();
    d.builder = s->builder;
    d.device = s->device;
    d.in_use = s->in_use;
    *s = d;
}

/* The builder state for one guest builder address. A builder that was never
 * prepared has no state, and the functions that need one say so instead of
 * inventing a default. */
static nvn_sampler_builder_state *builder_find(void *builder, int create) {
    size_t i;
    nvn_sampler_builder_state *free_slot = NULL;
    if (!builder) return NULL;
    for (i = 0; i < s_builders.count; i++) {
        nvn_sampler_builder_state *entry = (nvn_sampler_builder_state *)nvn_object_table_at(&s_builders, i);
        if (!entry) continue;
        if (entry->in_use) {
            if (entry->builder == builder) return entry;
        } else if (!free_slot) {
            free_slot = entry;
        }
    }
    if (!create) return NULL;
    if (!free_slot) {
        free_slot = (nvn_sampler_builder_state *)nvn_object_table_new_entry(&s_builders, NVN_SAMPLER_BUILDER_MAX_OBJECTS);
        if (!free_slot) return NULL;
    }
    *free_slot = default_state();
    free_slot->builder = builder;
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

/* ------------------------------------------------------------------ */
/* The setters                                                         */
/* ------------------------------------------------------------------ */

void nvnSamplerBuilderSetDefaults(void *builder) {
    nvn_sampler_builder_state *s;
    int first = 0;
    pthread_mutex_lock(&s_sampler_builder_lock);
    s_stats.calls++;
    s_stats.set_defaults++;
    s = builder_find(builder, 1);
    if (s) {
        apply_defaults(s);
    } else {
        s_stats.refused++;
    }
    first = mark_logged(SB_SET_DEFAULTS);
    pthread_mutex_unlock(&s_sampler_builder_lock);
    if (!s) {
        fprintf(stderr, "[nvn] nvnSamplerBuilderSetDefaults: no room for another sampler builder (%p); the "
                        "defaults were not applied\n",
                builder);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnSamplerBuilderSetDefaults: builder=%p\n", builder);
    }
    NVN_VOID_RETURN();
}

void nvnSamplerBuilderSetDevice(void *builder, void *device) {
    nvn_sampler_builder_state *s;
    int first = 0;
    pthread_mutex_lock(&s_sampler_builder_lock);
    s_stats.calls++;
    s_stats.sets++;
    s = builder_find(builder, 1);
    if (s) {
        s->device = device;
    } else {
        s_stats.refused++;
    }
    first = mark_logged(SB_SET_DEVICE);
    pthread_mutex_unlock(&s_sampler_builder_lock);
    if (!s) {
        fprintf(stderr, "[nvn] nvnSamplerBuilderSetDevice: no room for another sampler builder (%p); the device "
                        "was not recorded\n",
                builder);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnSamplerBuilderSetDevice: builder=%p device=%p\n", builder, device);
    }
    NVN_VOID_RETURN();
}

void nvnSamplerBuilderSetCompare(void *builder, NVNcompareMode mode, NVNcompareFunc func) {
    nvn_sampler_builder_state *s;
    int first = 0;
    pthread_mutex_lock(&s_sampler_builder_lock);
    s_stats.calls++;
    s_stats.sets++;
    s = builder_find(builder, 1);
    if (s) {
        s->compare_mode = mode;
        s->compare_func = func;
    } else {
        s_stats.refused++;
    }
    first = mark_logged(SB_SET_COMPARE);
    pthread_mutex_unlock(&s_sampler_builder_lock);
    if (!s) {
        fprintf(stderr, "[nvn] nvnSamplerBuilderSetCompare: no room for another sampler builder (%p); the "
                        "comparison was not recorded\n",
                builder);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnSamplerBuilderSetCompare: builder=%p mode=0x%x func=0x%x\n", builder,
                (unsigned)mode, (unsigned)func);
    }
    NVN_VOID_RETURN();
}

void nvnSamplerBuilderSetMinMagFilter(void *builder, NVNminFilter min_filter, NVNmagFilter mag_filter) {
    nvn_sampler_builder_state *s;
    int first = 0;
    pthread_mutex_lock(&s_sampler_builder_lock);
    s_stats.calls++;
    s_stats.sets++;
    s = builder_find(builder, 1);
    if (s) {
        s->min_filter = min_filter;
        s->mag_filter = mag_filter;
    } else {
        s_stats.refused++;
    }
    first = mark_logged(SB_SET_MIN_MAG_FILTER);
    pthread_mutex_unlock(&s_sampler_builder_lock);
    if (!s) {
        fprintf(stderr, "[nvn] nvnSamplerBuilderSetMinMagFilter: no room for another sampler builder (%p); the "
                        "filters were not recorded\n",
                builder);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnSamplerBuilderSetMinMagFilter: builder=%p min=0x%x mag=0x%x\n", builder,
                (unsigned)min_filter, (unsigned)mag_filter);
    }
    NVN_VOID_RETURN();
}

void nvnSamplerBuilderSetReductionFilter(void *builder, NVNsamplerReduction reduction) {
    nvn_sampler_builder_state *s;
    int first = 0;
    pthread_mutex_lock(&s_sampler_builder_lock);
    s_stats.calls++;
    s_stats.sets++;
    s = builder_find(builder, 1);
    if (s) {
        s->reduction_filter = reduction;
    } else {
        s_stats.refused++;
    }
    first = mark_logged(SB_SET_REDUCTION_FILTER);
    pthread_mutex_unlock(&s_sampler_builder_lock);
    if (!s) {
        fprintf(stderr, "[nvn] nvnSamplerBuilderSetReductionFilter: no room for another sampler builder (%p); the "
                        "reduction filter was not recorded\n",
                builder);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnSamplerBuilderSetReductionFilter: builder=%p reduction=0x%x\n", builder,
                (unsigned)reduction);
    }
    NVN_VOID_RETURN();
}

void nvnSamplerBuilderSetWrapMode(void *builder, NVNwrapMode s_wrap, NVNwrapMode t_wrap, NVNwrapMode r_wrap) {
    nvn_sampler_builder_state *s;
    int first = 0;
    pthread_mutex_lock(&s_sampler_builder_lock);
    s_stats.calls++;
    s_stats.sets++;
    s = builder_find(builder, 1);
    if (s) {
        s->wrap_s = s_wrap;
        s->wrap_t = t_wrap;
        s->wrap_r = r_wrap;
    } else {
        s_stats.refused++;
    }
    first = mark_logged(SB_SET_WRAP_MODE);
    pthread_mutex_unlock(&s_sampler_builder_lock);
    if (!s) {
        fprintf(stderr, "[nvn] nvnSamplerBuilderSetWrapMode: no room for another sampler builder (%p); the wrap "
                        "modes were not recorded\n",
                builder);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnSamplerBuilderSetWrapMode: builder=%p s=0x%x t=0x%x r=0x%x\n", builder,
                (unsigned)s_wrap, (unsigned)t_wrap, (unsigned)r_wrap);
    }
    NVN_VOID_RETURN();
}

void nvnSamplerBuilderSetLodBias(void *builder, float bias) {
    nvn_sampler_builder_state *s;
    int first = 0;
    pthread_mutex_lock(&s_sampler_builder_lock);
    s_stats.calls++;
    s_stats.sets++;
    s = builder_find(builder, 1);
    if (s) {
        s->lod_bias = bias;
    } else {
        s_stats.refused++;
    }
    first = mark_logged(SB_SET_LOD_BIAS);
    pthread_mutex_unlock(&s_sampler_builder_lock);
    if (!s) {
        fprintf(stderr, "[nvn] nvnSamplerBuilderSetLodBias: no room for another sampler builder (%p); the level of "
                        "detail bias was not recorded\n",
                builder);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnSamplerBuilderSetLodBias: builder=%p bias=%g\n", builder, (double)bias);
    }
    NVN_VOID_RETURN();
}

void nvnSamplerBuilderSetLodClamp(void *builder, float min_lod, float max_lod) {
    nvn_sampler_builder_state *s;
    int first = 0;
    pthread_mutex_lock(&s_sampler_builder_lock);
    s_stats.calls++;
    s_stats.sets++;
    s = builder_find(builder, 1);
    if (s) {
        s->lod_min = min_lod;
        s->lod_max = max_lod;
    } else {
        s_stats.refused++;
    }
    first = mark_logged(SB_SET_LOD_CLAMP);
    pthread_mutex_unlock(&s_sampler_builder_lock);
    if (!s) {
        fprintf(stderr, "[nvn] nvnSamplerBuilderSetLodClamp: no room for another sampler builder (%p); the level "
                        "of detail clamp was not recorded\n",
                builder);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnSamplerBuilderSetLodClamp: builder=%p min=%g max=%g\n", builder, (double)min_lod,
                (double)max_lod);
    }
    NVN_VOID_RETURN();
}

void nvnSamplerBuilderSetLodSnap(void *builder, float snap) {
    nvn_sampler_builder_state *s;
    int first = 0;
    pthread_mutex_lock(&s_sampler_builder_lock);
    s_stats.calls++;
    s_stats.sets++;
    s = builder_find(builder, 1);
    if (s) {
        s->lod_snap = snap;
    } else {
        s_stats.refused++;
    }
    first = mark_logged(SB_SET_LOD_SNAP);
    pthread_mutex_unlock(&s_sampler_builder_lock);
    if (!s) {
        fprintf(stderr, "[nvn] nvnSamplerBuilderSetLodSnap: no room for another sampler builder (%p); the level "
                        "of detail snap was not recorded\n",
                builder);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnSamplerBuilderSetLodSnap: builder=%p snap=%g\n", builder, (double)snap);
    }
    NVN_VOID_RETURN();
}

void nvnSamplerBuilderSetMaxAnisotropy(void *builder, float anisotropy) {
    nvn_sampler_builder_state *s;
    int first = 0;
    pthread_mutex_lock(&s_sampler_builder_lock);
    s_stats.calls++;
    s_stats.sets++;
    s = builder_find(builder, 1);
    if (s) {
        s->max_anisotropy = anisotropy;
    } else {
        s_stats.refused++;
    }
    first = mark_logged(SB_SET_MAX_ANISOTROPY);
    pthread_mutex_unlock(&s_sampler_builder_lock);
    if (!s) {
        fprintf(stderr, "[nvn] nvnSamplerBuilderSetMaxAnisotropy: no room for another sampler builder (%p); the "
                        "anisotropy was not recorded\n",
                builder);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnSamplerBuilderSetMaxAnisotropy: builder=%p anisotropy=%g\n", builder,
                (double)anisotropy);
    }
    NVN_VOID_RETURN();
}

/* The border colour is four components: the reference tree copies sixteen
 * bytes out of the pointer the guest hands over (hle_nvn.c:546) and keeps the
 * colour as four floats (nvn_obj.h:49). */
void nvnSamplerBuilderSetBorderColor(void *builder, const float *color) {
    nvn_sampler_builder_state *s;
    int first = 0;
    int refused = 0;
    pthread_mutex_lock(&s_sampler_builder_lock);
    s_stats.calls++;
    s_stats.sets++;
    s = builder_find(builder, 1);
    if (!s) {
        s_stats.refused++;
        refused = 1;
    } else if (!color) {
        s_stats.refused++;
        refused = 1;
    } else {
        memcpy(s->border_color, color, sizeof(s->border_color));
    }
    first = mark_logged(SB_SET_BORDER_COLOR);
    pthread_mutex_unlock(&s_sampler_builder_lock);
    if (refused) {
        fprintf(stderr, "[nvn] nvnSamplerBuilderSetBorderColor: builder=%p color=%p; the border colour was not "
                        "recorded\n",
                builder, (const void *)color);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnSamplerBuilderSetBorderColor: builder=%p color=%p\n", builder,
                (const void *)color);
    }
    NVN_VOID_RETURN();
}

void nvnSamplerBuilderSetBorderColori(void *builder, const int *color) {
    nvn_sampler_builder_state *s;
    int first = 0;
    int refused = 0;
    pthread_mutex_lock(&s_sampler_builder_lock);
    s_stats.calls++;
    s_stats.sets++;
    s = builder_find(builder, 1);
    if (!s) {
        s_stats.refused++;
        refused = 1;
    } else if (!color) {
        s_stats.refused++;
        refused = 1;
    } else {
        memcpy(s->border_colori, color, sizeof(s->border_colori));
    }
    first = mark_logged(SB_SET_BORDER_COLORI);
    pthread_mutex_unlock(&s_sampler_builder_lock);
    if (refused) {
        fprintf(stderr, "[nvn] nvnSamplerBuilderSetBorderColori: builder=%p color=%p; the border colour was not "
                        "recorded\n",
                builder, (const void *)color);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnSamplerBuilderSetBorderColori: builder=%p color=%p\n", builder,
                (const void *)color);
    }
    NVN_VOID_RETURN();
}

void nvnSamplerBuilderSetBorderColorui(void *builder, const uint32_t *color) {
    nvn_sampler_builder_state *s;
    int first = 0;
    int refused = 0;
    pthread_mutex_lock(&s_sampler_builder_lock);
    s_stats.calls++;
    s_stats.sets++;
    s = builder_find(builder, 1);
    if (!s) {
        s_stats.refused++;
        refused = 1;
    } else if (!color) {
        s_stats.refused++;
        refused = 1;
    } else {
        memcpy(s->border_colorui, color, sizeof(s->border_colorui));
    }
    first = mark_logged(SB_SET_BORDER_COLORUI);
    pthread_mutex_unlock(&s_sampler_builder_lock);
    if (refused) {
        fprintf(stderr, "[nvn] nvnSamplerBuilderSetBorderColorui: builder=%p color=%p; the border colour was not "
                        "recorded\n",
                builder, (const void *)color);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnSamplerBuilderSetBorderColorui: builder=%p color=%p\n", builder,
                (const void *)color);
    }
    NVN_VOID_RETURN();
}

/* ------------------------------------------------------------------ */
/* The getters                                                         */
/* ------------------------------------------------------------------ */

/* The signature of nvnSamplerBuilderGetDevice is not in the vendored header
 * this runtime was built against, and the reference tree lists it among the
 * names it can only give a stub (nvn_protos.h: "Names without a prototype in
 * the header"). The arguments are therefore taken as plain words and written
 * down as they came. Nothing is dereferenced, so a signature with fewer or
 * more arguments cannot do any harm here.
 *
 * The answer is the device the guest put into this builder, not zero. The
 * signature is not confirmed, but a getter that hands back a null pointer is
 * the one answer that can crash the guest if it dereferences the result, and
 * the device the guest itself set is the only value this can give that is
 * both safe and true. The log says that the signature is not confirmed. */
void nvnSamplerBuilderGetDevice(uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4,
                                uint64_t arg5, uint64_t arg6, uint64_t arg7) {
    int first = 0;
    void *device = NULL;
    nvn_sampler_builder_state *found;
    pthread_mutex_lock(&s_sampler_builder_lock);
    s_stats.calls++;
    s_stats.gets++;
    s_stats.raw_calls++;
    found = builder_find((void *)(uintptr_t)arg0, 0);
    if (found) device = found->device;
    first = mark_logged(SB_GET_DEVICE);
    pthread_mutex_unlock(&s_sampler_builder_lock);
    if (first) {
        fprintf(stderr, "[nvn] nvnSamplerBuilderGetDevice: args=0x%" PRIx64 ",0x%" PRIx64 ",0x%" PRIx64 ",0x%" PRIx64
                        ",0x%" PRIx64 ",0x%" PRIx64 ",0x%" PRIx64 ",0x%" PRIx64
                        "; the signature of this entry point is not in the header this runtime was built against, "
                        "so the arguments are recorded as they came and the device of the builder is returned\n",
                arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7);
    }
    NVN_SET_RESULT((uint64_t)(uintptr_t)device);
}

void nvnSamplerBuilderGetCompare(void *builder, NVNcompareMode *mode, NVNcompareFunc *func) {
    nvn_sampler_builder_state s = default_state();
    nvn_sampler_builder_state *found;
    int first = 0;
    pthread_mutex_lock(&s_sampler_builder_lock);
    s_stats.calls++;
    s_stats.gets++;
    found = builder_find(builder, 0);
    if (found) s = *found;
    if (mode) *mode = s.compare_mode;
    if (func) *func = s.compare_func;
    first = mark_logged(SB_GET_COMPARE);
    pthread_mutex_unlock(&s_sampler_builder_lock);
    if (first) {
        fprintf(stderr, "[nvn] nvnSamplerBuilderGetCompare: builder=%p mode=0x%x func=0x%x\n", builder,
                (unsigned)s.compare_mode, (unsigned)s.compare_func);
    }
    NVN_VOID_RETURN();
}

void nvnSamplerBuilderGetMinMagFilter(void *builder, NVNminFilter *min_filter, NVNmagFilter *mag_filter) {
    nvn_sampler_builder_state s = default_state();
    nvn_sampler_builder_state *found;
    int first = 0;
    pthread_mutex_lock(&s_sampler_builder_lock);
    s_stats.calls++;
    s_stats.gets++;
    found = builder_find(builder, 0);
    if (found) s = *found;
    if (min_filter) *min_filter = s.min_filter;
    if (mag_filter) *mag_filter = s.mag_filter;
    first = mark_logged(SB_GET_MIN_MAG_FILTER);
    pthread_mutex_unlock(&s_sampler_builder_lock);
    if (first) {
        fprintf(stderr, "[nvn] nvnSamplerBuilderGetMinMagFilter: builder=%p min=0x%x mag=0x%x\n", builder,
                (unsigned)s.min_filter, (unsigned)s.mag_filter);
    }
    NVN_VOID_RETURN();
}

NVNsamplerReduction nvnSamplerBuilderGetReductionFilter(void *builder) {
    nvn_sampler_builder_state s = default_state();
    nvn_sampler_builder_state *found;
    NVNsamplerReduction reduction;
    int first = 0;
    pthread_mutex_lock(&s_sampler_builder_lock);
    s_stats.calls++;
    s_stats.gets++;
    found = builder_find(builder, 0);
    if (found) s = *found;
    reduction = s.reduction_filter;
    first = mark_logged(SB_GET_REDUCTION_FILTER);
    pthread_mutex_unlock(&s_sampler_builder_lock);
    if (first) {
        fprintf(stderr, "[nvn] nvnSamplerBuilderGetReductionFilter: builder=%p reduction=0x%x\n", builder,
                (unsigned)reduction);
    }
    return reduction;
}

void nvnSamplerBuilderGetWrapMode(void *builder, NVNwrapMode *s_wrap, NVNwrapMode *t_wrap, NVNwrapMode *r_wrap) {
    nvn_sampler_builder_state s = default_state();
    nvn_sampler_builder_state *found;
    int first = 0;
    pthread_mutex_lock(&s_sampler_builder_lock);
    s_stats.calls++;
    s_stats.gets++;
    found = builder_find(builder, 0);
    if (found) s = *found;
    if (s_wrap) *s_wrap = s.wrap_s;
    if (t_wrap) *t_wrap = s.wrap_t;
    if (r_wrap) *r_wrap = s.wrap_r;
    first = mark_logged(SB_GET_WRAP_MODE);
    pthread_mutex_unlock(&s_sampler_builder_lock);
    if (first) {
        fprintf(stderr, "[nvn] nvnSamplerBuilderGetWrapMode: builder=%p s=0x%x t=0x%x r=0x%x\n", builder,
                (unsigned)s.wrap_s, (unsigned)s.wrap_t, (unsigned)s.wrap_r);
    }
    NVN_VOID_RETURN();
}

float nvnSamplerBuilderGetLodBias(void *builder) {
    nvn_sampler_builder_state s = default_state();
    nvn_sampler_builder_state *found;
    float bias;
    int first = 0;
    pthread_mutex_lock(&s_sampler_builder_lock);
    s_stats.calls++;
    s_stats.gets++;
    found = builder_find(builder, 0);
    if (found) s = *found;
    bias = s.lod_bias;
    first = mark_logged(SB_GET_LOD_BIAS);
    pthread_mutex_unlock(&s_sampler_builder_lock);
    if (first) {
        fprintf(stderr, "[nvn] nvnSamplerBuilderGetLodBias: builder=%p bias=%g\n", builder, (double)bias);
    }
    return bias;
}

void nvnSamplerBuilderGetLodClamp(void *builder, float *min_lod, float *max_lod) {
    nvn_sampler_builder_state s = default_state();
    nvn_sampler_builder_state *found;
    int first = 0;
    pthread_mutex_lock(&s_sampler_builder_lock);
    s_stats.calls++;
    s_stats.gets++;
    found = builder_find(builder, 0);
    if (found) s = *found;
    if (min_lod) *min_lod = s.lod_min;
    if (max_lod) *max_lod = s.lod_max;
    first = mark_logged(SB_GET_LOD_CLAMP);
    pthread_mutex_unlock(&s_sampler_builder_lock);
    if (first) {
        fprintf(stderr, "[nvn] nvnSamplerBuilderGetLodClamp: builder=%p min=%g max=%g\n", builder, (double)s.lod_min,
                (double)s.lod_max);
    }
    NVN_VOID_RETURN();
}

float nvnSamplerBuilderGetLodSnap(void *builder) {
    nvn_sampler_builder_state s = default_state();
    nvn_sampler_builder_state *found;
    float snap;
    int first = 0;
    pthread_mutex_lock(&s_sampler_builder_lock);
    s_stats.calls++;
    s_stats.gets++;
    found = builder_find(builder, 0);
    if (found) s = *found;
    snap = s.lod_snap;
    first = mark_logged(SB_GET_LOD_SNAP);
    pthread_mutex_unlock(&s_sampler_builder_lock);
    if (first) {
        fprintf(stderr, "[nvn] nvnSamplerBuilderGetLodSnap: builder=%p snap=%g\n", builder, (double)snap);
    }
    return snap;
}

float nvnSamplerBuilderGetMaxAnisotropy(void *builder) {
    nvn_sampler_builder_state s = default_state();
    nvn_sampler_builder_state *found;
    float anisotropy;
    int first = 0;
    pthread_mutex_lock(&s_sampler_builder_lock);
    s_stats.calls++;
    s_stats.gets++;
    found = builder_find(builder, 0);
    if (found) s = *found;
    anisotropy = s.max_anisotropy;
    first = mark_logged(SB_GET_MAX_ANISOTROPY);
    pthread_mutex_unlock(&s_sampler_builder_lock);
    if (first) {
        fprintf(stderr, "[nvn] nvnSamplerBuilderGetMaxAnisotropy: builder=%p anisotropy=%g\n", builder,
                (double)anisotropy);
    }
    return anisotropy;
}

/* The border colour is four components, as the setter takes it. */
void nvnSamplerBuilderGetBorderColor(void *builder, float *color) {
    nvn_sampler_builder_state s = default_state();
    nvn_sampler_builder_state *found;
    int first = 0;
    pthread_mutex_lock(&s_sampler_builder_lock);
    s_stats.calls++;
    s_stats.gets++;
    found = builder_find(builder, 0);
    if (found) s = *found;
    if (color) memcpy(color, s.border_color, sizeof(s.border_color));
    first = mark_logged(SB_GET_BORDER_COLOR);
    pthread_mutex_unlock(&s_sampler_builder_lock);
    if (first) {
        fprintf(stderr, "[nvn] nvnSamplerBuilderGetBorderColor: builder=%p\n", builder);
    }
    NVN_VOID_RETURN();
}

void nvnSamplerBuilderGetBorderColori(void *builder, int *color) {
    nvn_sampler_builder_state s = default_state();
    nvn_sampler_builder_state *found;
    int first = 0;
    pthread_mutex_lock(&s_sampler_builder_lock);
    s_stats.calls++;
    s_stats.gets++;
    found = builder_find(builder, 0);
    if (found) s = *found;
    if (color) memcpy(color, s.border_colori, sizeof(s.border_colori));
    first = mark_logged(SB_GET_BORDER_COLORI);
    pthread_mutex_unlock(&s_sampler_builder_lock);
    if (first) {
        fprintf(stderr, "[nvn] nvnSamplerBuilderGetBorderColori: builder=%p\n", builder);
    }
    NVN_VOID_RETURN();
}

void nvnSamplerBuilderGetBorderColorui(void *builder, uint32_t *color) {
    nvn_sampler_builder_state s = default_state();
    nvn_sampler_builder_state *found;
    int first = 0;
    pthread_mutex_lock(&s_sampler_builder_lock);
    s_stats.calls++;
    s_stats.gets++;
    found = builder_find(builder, 0);
    if (found) s = *found;
    if (color) memcpy(color, s.border_colorui, sizeof(s.border_colorui));
    first = mark_logged(SB_GET_BORDER_COLORUI);
    pthread_mutex_unlock(&s_sampler_builder_lock);
    if (first) {
        fprintf(stderr, "[nvn] nvnSamplerBuilderGetBorderColorui: builder=%p\n", builder);
    }
    NVN_VOID_RETURN();
}

/* ------------------------------------------------------------------ */
/* The names                                                           */
/* ------------------------------------------------------------------ */

static const nvn_sampler_builder_family_entry s_sampler_builder_family[] = {
    { "nvnSamplerBuilderSetDefaults", (void *)(uintptr_t)nvnSamplerBuilderSetDefaults },
    { "nvnSamplerBuilderSetDevice", (void *)(uintptr_t)nvnSamplerBuilderSetDevice },
    { "nvnSamplerBuilderSetCompare", (void *)(uintptr_t)nvnSamplerBuilderSetCompare },
    { "nvnSamplerBuilderSetMinMagFilter", (void *)(uintptr_t)nvnSamplerBuilderSetMinMagFilter },
    { "nvnSamplerBuilderSetReductionFilter", (void *)(uintptr_t)nvnSamplerBuilderSetReductionFilter },
    { "nvnSamplerBuilderSetWrapMode", (void *)(uintptr_t)nvnSamplerBuilderSetWrapMode },
    { "nvnSamplerBuilderSetLodBias", (void *)(uintptr_t)nvnSamplerBuilderSetLodBias },
    { "nvnSamplerBuilderSetLodClamp", (void *)(uintptr_t)nvnSamplerBuilderSetLodClamp },
    { "nvnSamplerBuilderSetLodSnap", (void *)(uintptr_t)nvnSamplerBuilderSetLodSnap },
    { "nvnSamplerBuilderSetMaxAnisotropy", (void *)(uintptr_t)nvnSamplerBuilderSetMaxAnisotropy },
    { "nvnSamplerBuilderSetBorderColor", (void *)(uintptr_t)nvnSamplerBuilderSetBorderColor },
    { "nvnSamplerBuilderSetBorderColori", (void *)(uintptr_t)nvnSamplerBuilderSetBorderColori },
    { "nvnSamplerBuilderSetBorderColorui", (void *)(uintptr_t)nvnSamplerBuilderSetBorderColorui },
    { "nvnSamplerBuilderGetDevice", (void *)(uintptr_t)nvnSamplerBuilderGetDevice },
    { "nvnSamplerBuilderGetCompare", (void *)(uintptr_t)nvnSamplerBuilderGetCompare },
    { "nvnSamplerBuilderGetMinMagFilter", (void *)(uintptr_t)nvnSamplerBuilderGetMinMagFilter },
    { "nvnSamplerBuilderGetReductionFilter", (void *)(uintptr_t)nvnSamplerBuilderGetReductionFilter },
    { "nvnSamplerBuilderGetWrapMode", (void *)(uintptr_t)nvnSamplerBuilderGetWrapMode },
    { "nvnSamplerBuilderGetLodBias", (void *)(uintptr_t)nvnSamplerBuilderGetLodBias },
    { "nvnSamplerBuilderGetLodClamp", (void *)(uintptr_t)nvnSamplerBuilderGetLodClamp },
    { "nvnSamplerBuilderGetLodSnap", (void *)(uintptr_t)nvnSamplerBuilderGetLodSnap },
    { "nvnSamplerBuilderGetMaxAnisotropy", (void *)(uintptr_t)nvnSamplerBuilderGetMaxAnisotropy },
    { "nvnSamplerBuilderGetBorderColor", (void *)(uintptr_t)nvnSamplerBuilderGetBorderColor },
    { "nvnSamplerBuilderGetBorderColori", (void *)(uintptr_t)nvnSamplerBuilderGetBorderColori },
    { "nvnSamplerBuilderGetBorderColorui", (void *)(uintptr_t)nvnSamplerBuilderGetBorderColorui },
};

size_t nvn_sampler_builder_family_count(void) {
    return sizeof(s_sampler_builder_family) / sizeof(s_sampler_builder_family[0]);
}

const nvn_sampler_builder_family_entry *nvn_sampler_builder_family_entry_at(size_t index) {
    if (index >= nvn_sampler_builder_family_count()) return NULL;
    return &s_sampler_builder_family[index];
}

void nvn_sampler_builder_get_stats(nvn_sampler_builder_stats *out) {
    size_t i;
    if (!out) return;
    pthread_mutex_lock(&s_sampler_builder_lock);
    *out = s_stats;
    out->live_builders = 0;
    for (i = 0; i < s_builders.count; i++) {
        if (((nvn_sampler_builder_state *)nvn_object_table_at(&s_builders, i))->in_use) out->live_builders++;
    }
    pthread_mutex_unlock(&s_sampler_builder_lock);
}

void *nvn_sampler_builder_device_of(const void *builder) {
    nvn_sampler_builder_state *found;
    void *device = NULL;
    pthread_mutex_lock(&s_sampler_builder_lock);
    found = builder_find((void *)(uintptr_t)builder, 0);
    if (found) device = found->device;
    pthread_mutex_unlock(&s_sampler_builder_lock);
    return device;
}

/* The parameters of a builder, copied out for the family that makes a sampler
 * from a builder.
 *
 * Until now the parameters of a builder could not be read from outside this
 * family: the sampler family has to take a snapshot of them when the guest
 * hands the builder over to nvnSamplerInitialize, and it cannot ask the
 * builder's own getters for them one by one, because a getter answers with
 * the value the builder holds now and not with the value it held when the
 * sampler was made. So this is the one way out: one copy, at the moment the
 * sampler is made.
 *
 * The level of detail snap is not copied: the reference host that runs this
 * very game ignores it (hle_nvn.c:548), and the sampler object has no getter
 * for it. */
int nvn_sampler_builder_copy_parameters(const void *builder, nvn_sampler_builder_parameters *out) {
    nvn_sampler_builder_state *found;
    int copied = 0;
    if (!out) return 0;
    pthread_mutex_lock(&s_sampler_builder_lock);
    found = builder_find((void *)(uintptr_t)builder, 0);
    if (found) {
        out->device = found->device;
        out->min_filter = (uint32_t)found->min_filter;
        out->mag_filter = (uint32_t)found->mag_filter;
        out->wrap_s = (uint32_t)found->wrap_s;
        out->wrap_t = (uint32_t)found->wrap_t;
        out->wrap_r = (uint32_t)found->wrap_r;
        out->compare_mode = (uint32_t)found->compare_mode;
        out->compare_func = (uint32_t)found->compare_func;
        out->reduction_filter = (uint32_t)found->reduction_filter;
        out->lod_min = found->lod_min;
        out->lod_max = found->lod_max;
        out->lod_bias = found->lod_bias;
        out->max_anisotropy = found->max_anisotropy;
        memcpy(out->border_color, found->border_color, sizeof(out->border_color));
        memcpy(out->border_colori, found->border_colori, sizeof(out->border_colori));
        memcpy(out->border_colorui, found->border_colorui, sizeof(out->border_colorui));
        copied = 1;
    }
    pthread_mutex_unlock(&s_sampler_builder_lock);
    if (!copied) {
        fprintf(stderr, "[nvn] sampler builder %p was never prepared, so it has no parameters to copy\n", builder);
    }
    return copied;
}
