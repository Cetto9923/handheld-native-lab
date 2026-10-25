/* The NVN texture view family implementation.
 *
 * See nvn_texture_view.h for why the state lives on the host.
 *
 * A view describes which part of a texture a draw refers to. The guest sets
 * the levels, the layers, the format, the target, the swizzle and the depth
 * stencil mode of a view, and then reads them back through the getters of this
 * family. The getters answer with the value AND with whether the guest set
 * that field at all: that is the shape of the reference host (hle_nvn.c:504,
 * 1965, 2032, 2033, 2034), and it is what lets the guest tell a view that
 * describes one level from a view that describes all of them.
 *
 * The evidence for the values:
 *
 *   - The signatures of the fourteen entry points are the ones of the vendored
 *     NVN header (src/graphics/nvn/include/nvn/nvn.h:1300-1319). That header
 *     is a reconstruction of NVN 53.105 taken from another game (Darksiders
 *     II), so its signatures are NOT confirmed for this game, which is Unreal
 *     Engine 5.6.1. The reference host that runs this very game agrees with
 *     them (.local/refs/nxnative/dm/gfx/nvn/nvn_protos.h).
 *   - The fields of a view and the way they are set are the ones of the
 *     reference host (hle_nvn.c:499-507, 1965-1968, 2032-2034): the levels are
 *     a base level and a number of levels, the layers are a first layer and a
 *     number of layers, and every field carries a flag that says whether the
 *     guest set it.
 *   - The defaults are the ones of the reference host (hle_nvn.c:499): the
 *     whole view is cleared, so every field is unset and every value is zero.
 *   - The swizzle of a view that was never given one is the identity R, G, B,
 *     A (hle_nvn.c:2034).
 *
 * nvnTextureViewCompare has no counterpart in the reference host: the reference
 * does not have this entry point at all. Its meaning here is this runtime's
 * own: two views are the same when they carry the same description, that is
 * when the same fields are set and the values of those fields are the same.
 * This is the same shape as the comparison of the texture family, which
 * compares the whole description of a texture (hle_nvn.c:492). It is NOT
 * claimed to agree with the reference, because the reference does not say.
 */
#include "nvn_texture_view.h"
#include "nvn_obj_ffrs.h"

#include "nvn_device.h"

#include "../nvn/include/nvn/nvn.h"

#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include "nvn_object_table.h"

typedef struct {
    void *view;
    int set_levels;
    int base_level;
    int nlevels;
    int set_layers;
    int min_layer;
    int nlayers;
    int set_format;
    NVNformat format;
    int set_target;
    NVNtextureTarget target;
    int set_swizzle;
    NVNtextureSwizzle swizzle[4];
    int set_dsmode;
    NVNtextureDepthStencilMode dsmode;
    /* The generation of this view object. */
    uint64_t serial;
    int in_use;
} nvn_texture_view_state;

/* The names of this family, in the order of the family table. That order is
 * the order the guest itself resolves the names in (the trap slots 26 to 39 of
 * the device run of 2026-10-25). The index is used for the "only the first
 * call is logged" bookkeeping. */
enum {
    TV_SET_DEFAULTS,
    TV_SET_LEVELS,
    TV_SET_LAYERS,
    TV_SET_FORMAT,
    TV_SET_TARGET,
    TV_SET_SWIZZLE,
    TV_SET_DEPTH_STENCIL_MODE,
    TV_GET_LEVELS,
    TV_GET_LAYERS,
    TV_GET_FORMAT,
    TV_GET_TARGET,
    TV_GET_SWIZZLE,
    TV_GET_DEPTH_STENCIL_MODE,
    TV_COMPARE,
    TV_NAME_COUNT
};

static pthread_mutex_t s_view_lock = PTHREAD_MUTEX_INITIALIZER;
/* The generation of the views of this runtime. */
static uint64_t s_view_serial;
static nvn_object_table s_views = { NULL, 0, 0, sizeof(nvn_texture_view_state) };
static nvn_texture_view_stats s_stats;
static int s_logged[TV_NAME_COUNT];

/* The defaults of the reference tree (hle_nvn.c:499): the whole view is
 * cleared. */
static nvn_texture_view_state default_state(void) {
    nvn_texture_view_state v;
    memset(&v, 0, sizeof(v));
    return v;
}

/* The view state for one guest view address. A view that was never described
 * has no state, and the functions that need one say so instead of inventing
 * one. */
static nvn_texture_view_state *view_find(void *view, int create) {
    size_t i;
    nvn_texture_view_state *free_slot = NULL;
    if (!view) return NULL;
    for (i = 0; i < s_views.count; i++) {
        nvn_texture_view_state *entry = (nvn_texture_view_state *)nvn_object_table_at(&s_views, i);
        if (!entry) continue;
        if (entry->in_use) {
            if (entry->view == view) {
                if (create) entry->serial = ++s_view_serial;
                return entry;
            }
        } else if (!free_slot) {
            free_slot = entry;
        }
    }
    if (!create) return NULL;
    if (!free_slot) {
        free_slot = (nvn_texture_view_state *)nvn_object_table_new_entry(&s_views, NVN_TEXTURE_VIEW_MAX_OBJECTS);
        if (!free_slot) return NULL;
    }
    *free_slot = default_state();
    free_slot->view = view;
    free_slot->in_use = 1;
    /* A new generation: the guest is about to make this view say something
     * else, and the copy the replay layer holds of it is no longer the one
     * the guest means. */
    free_slot->serial = ++s_view_serial;
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
/* nvnTextureViewSetDefaults                                            */
/* ------------------------------------------------------------------ */

/* nvnTextureViewSetDefaults(view).
 *
 * The reference host clears the whole view (hle_nvn.c:499). Here the state of
 * the view on the host is cleared, and the guest's own view memory is left
 * alone, because the layout of an NVNtextureView belongs to the Nintendo SDK.
 * A view that was described before loses its description, exactly as it does
 * in the reference. */
void nvnTextureViewSetDefaults(void *view) {
    nvn_texture_view_state *v;
    int first = 0;
    pthread_mutex_lock(&s_view_lock);
    s_stats.calls++;
    s_stats.set_defaults++;
    v = view_find(view, 1);
    if (v) {
        *v = default_state();
        v->view = view;
        v->in_use = 1;
    } else {
        s_stats.refused++;
    }
    first = mark_logged(TV_SET_DEFAULTS);
    pthread_mutex_unlock(&s_view_lock);
    if (!v) {
        fprintf(stderr, "[nvn] nvnTextureViewSetDefaults: no room for another texture view (%p); the defaults were "
                        "not applied\n",
                view);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnTextureViewSetDefaults: view=%p\n", view);
    }
    NVN_VOID_RETURN();
}

/* ------------------------------------------------------------------ */
/* The setters                                                          */
/* ------------------------------------------------------------------ */

/* Every setter records the value and says that the field is set. The reference
 * host does the same (hle_nvn.c:500-503, 1967, 1968), and the flag is what
 * the getters answer with. */

void nvnTextureViewSetLevels(void *view, int base, int n) {
    nvn_texture_view_state *v;
    int first = 0;
    pthread_mutex_lock(&s_view_lock);
    s_stats.calls++;
    s_stats.sets++;
    v = view_find(view, 1);
    if (v) {
        v->set_levels = 1;
        v->base_level = base;
        v->nlevels = n;
    } else {
        s_stats.refused++;
    }
    first = mark_logged(TV_SET_LEVELS);
    pthread_mutex_unlock(&s_view_lock);
    if (!v) {
        fprintf(stderr, "[nvn] nvnTextureViewSetLevels: no room for another texture view (%p); the levels were not "
                        "recorded\n",
                view);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnTextureViewSetLevels: view=%p base=%d n=%d\n", view, base, n);
    }
    NVN_VOID_RETURN();
}

void nvnTextureViewSetLayers(void *view, int min, int n) {
    nvn_texture_view_state *v;
    int first = 0;
    pthread_mutex_lock(&s_view_lock);
    s_stats.calls++;
    s_stats.sets++;
    v = view_find(view, 1);
    if (v) {
        v->set_layers = 1;
        v->min_layer = min;
        v->nlayers = n;
    } else {
        s_stats.refused++;
    }
    first = mark_logged(TV_SET_LAYERS);
    pthread_mutex_unlock(&s_view_lock);
    if (!v) {
        fprintf(stderr, "[nvn] nvnTextureViewSetLayers: no room for another texture view (%p); the layers were not "
                        "recorded\n",
                view);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnTextureViewSetLayers: view=%p min=%d n=%d\n", view, min, n);
    }
    NVN_VOID_RETURN();
}

void nvnTextureViewSetFormat(void *view, NVNformat format) {
    nvn_texture_view_state *v;
    int first = 0;
    pthread_mutex_lock(&s_view_lock);
    s_stats.calls++;
    s_stats.sets++;
    v = view_find(view, 1);
    if (v) {
        v->set_format = 1;
        v->format = format;
    } else {
        s_stats.refused++;
    }
    first = mark_logged(TV_SET_FORMAT);
    pthread_mutex_unlock(&s_view_lock);
    if (!v) {
        fprintf(stderr, "[nvn] nvnTextureViewSetFormat: no room for another texture view (%p); the format was not "
                        "recorded\n",
                view);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnTextureViewSetFormat: view=%p format=0x%x\n", view, (unsigned)format);
    }
    NVN_VOID_RETURN();
}

void nvnTextureViewSetTarget(void *view, NVNtextureTarget target) {
    nvn_texture_view_state *v;
    int first = 0;
    pthread_mutex_lock(&s_view_lock);
    s_stats.calls++;
    s_stats.sets++;
    v = view_find(view, 1);
    if (v) {
        v->set_target = 1;
        v->target = target;
    } else {
        s_stats.refused++;
    }
    first = mark_logged(TV_SET_TARGET);
    pthread_mutex_unlock(&s_view_lock);
    if (!v) {
        fprintf(stderr, "[nvn] nvnTextureViewSetTarget: no room for another texture view (%p); the target was not "
                        "recorded\n",
                view);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnTextureViewSetTarget: view=%p target=0x%x\n", view, (unsigned)target);
    }
    NVN_VOID_RETURN();
}

/* nvnTextureViewSetSwizzle(view, red, green, blue, alpha).
 *
 * The reference host records the four channels and says that the swizzle is
 * set (hle_nvn.c:1968). */
void nvnTextureViewSetSwizzle(void *view, NVNtextureSwizzle red, NVNtextureSwizzle green, NVNtextureSwizzle blue,
                              NVNtextureSwizzle alpha) {
    nvn_texture_view_state *v;
    int first = 0;
    pthread_mutex_lock(&s_view_lock);
    s_stats.calls++;
    s_stats.sets++;
    v = view_find(view, 1);
    if (v) {
        v->set_swizzle = 1;
        v->swizzle[0] = red;
        v->swizzle[1] = green;
        v->swizzle[2] = blue;
        v->swizzle[3] = alpha;
    } else {
        s_stats.refused++;
    }
    first = mark_logged(TV_SET_SWIZZLE);
    pthread_mutex_unlock(&s_view_lock);
    if (!v) {
        fprintf(stderr, "[nvn] nvnTextureViewSetSwizzle: no room for another texture view (%p); the swizzle was not "
                        "recorded\n",
                view);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnTextureViewSetSwizzle: view=%p swizzle=0x%x,0x%x,0x%x,0x%x\n", view, (unsigned)red,
                (unsigned)green, (unsigned)blue, (unsigned)alpha);
    }
    NVN_VOID_RETURN();
}

/* nvnTextureViewSetDepthStencilMode(view, mode).
 *
 * The reference host records the mode and says that it is set (hle_nvn.c:1967). */
void nvnTextureViewSetDepthStencilMode(void *view, NVNtextureDepthStencilMode mode) {
    nvn_texture_view_state *v;
    int first = 0;
    pthread_mutex_lock(&s_view_lock);
    s_stats.calls++;
    s_stats.sets++;
    v = view_find(view, 1);
    if (v) {
        v->set_dsmode = 1;
        v->dsmode = mode;
    } else {
        s_stats.refused++;
    }
    first = mark_logged(TV_SET_DEPTH_STENCIL_MODE);
    pthread_mutex_unlock(&s_view_lock);
    if (!v) {
        fprintf(stderr, "[nvn] nvnTextureViewSetDepthStencilMode: no room for another texture view (%p); the mode was "
                        "not recorded\n",
                view);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnTextureViewSetDepthStencilMode: view=%p mode=0x%x\n", view, (unsigned)mode);
    }
    NVN_VOID_RETURN();
}

/* ------------------------------------------------------------------ */
/* The getters                                                          */
/* ------------------------------------------------------------------ */

/* Every getter answers with the value of the field and with whether the guest
 * set that field. A view that was never described has no fields, so the getter
 * says so and answers with the default: a defined answer that the guest cannot
 * mistake for a value of this view, because the log names the refusal.
 *
 * The state of a view is copied out under the lock, so that the answer is a
 * value and not a pointer into the table. */

static int view_state_locked(const void *view, nvn_texture_view_state *out) {
    nvn_texture_view_state *found = view_find((void *)(uintptr_t)view, 0);
    if (found && found->in_use) {
        if (out) *out = *found;
        return 1;
    }
    if (out) *out = default_state();
    return 0;
}

NVNboolean nvnTextureViewGetLevels(const void *view, int *base, int *n) {
    nvn_texture_view_state v = default_state();
    int refused = 0;
    int first = 0;
    pthread_mutex_lock(&s_view_lock);
    s_stats.calls++;
    s_stats.gets++;
    refused = !view_state_locked(view, &v);
    first = mark_logged(TV_GET_LEVELS);
    pthread_mutex_unlock(&s_view_lock);
    if (base) *base = v.base_level;
    if (n) *n = v.nlevels;
    if (refused) {
        fprintf(stderr, "[nvn] nvnTextureViewGetLevels: view=%p was never described; the answer is the default, not "
                        "a value of this view\n",
                view);
        return 0;
    }
    if (first) {
        fprintf(stderr, "[nvn] nvnTextureViewGetLevels: view=%p base=%d n=%d set=%d\n", view, v.base_level, v.nlevels,
                v.set_levels);
    }
    return (NVNboolean)(v.set_levels ? 1 : 0);
}

NVNboolean nvnTextureViewGetLayers(const void *view, int *min, int *n) {
    nvn_texture_view_state v = default_state();
    int refused = 0;
    int first = 0;
    pthread_mutex_lock(&s_view_lock);
    s_stats.calls++;
    s_stats.gets++;
    refused = !view_state_locked(view, &v);
    first = mark_logged(TV_GET_LAYERS);
    pthread_mutex_unlock(&s_view_lock);
    if (min) *min = v.min_layer;
    if (n) *n = v.nlayers;
    if (refused) {
        fprintf(stderr, "[nvn] nvnTextureViewGetLayers: view=%p was never described; the answer is the default, not a "
                        "value of this view\n",
                view);
        return 0;
    }
    if (first) {
        fprintf(stderr, "[nvn] nvnTextureViewGetLayers: view=%p min=%d n=%d set=%d\n", view, v.min_layer, v.nlayers,
                v.set_layers);
    }
    return (NVNboolean)(v.set_layers ? 1 : 0);
}

NVNboolean nvnTextureViewGetFormat(const void *view, NVNformat *format) {
    nvn_texture_view_state v = default_state();
    int refused = 0;
    int first = 0;
    pthread_mutex_lock(&s_view_lock);
    s_stats.calls++;
    s_stats.gets++;
    refused = !view_state_locked(view, &v);
    first = mark_logged(TV_GET_FORMAT);
    pthread_mutex_unlock(&s_view_lock);
    if (format) *format = v.format;
    if (refused) {
        fprintf(stderr, "[nvn] nvnTextureViewGetFormat: view=%p was never described; the answer is the default, not a "
                        "value of this view\n",
                view);
        return 0;
    }
    if (first) {
        fprintf(stderr, "[nvn] nvnTextureViewGetFormat: view=%p format=0x%x set=%d\n", view, (unsigned)v.format,
                v.set_format);
    }
    return (NVNboolean)(v.set_format ? 1 : 0);
}

NVNboolean nvnTextureViewGetTarget(const void *view, NVNtextureTarget *target) {
    nvn_texture_view_state v = default_state();
    int refused = 0;
    int first = 0;
    pthread_mutex_lock(&s_view_lock);
    s_stats.calls++;
    s_stats.gets++;
    refused = !view_state_locked(view, &v);
    first = mark_logged(TV_GET_TARGET);
    pthread_mutex_unlock(&s_view_lock);
    if (target) *target = v.target;
    if (refused) {
        fprintf(stderr, "[nvn] nvnTextureViewGetTarget: view=%p was never described; the answer is the default, not a "
                        "value of this view\n",
                view);
        return 0;
    }
    if (first) {
        fprintf(stderr, "[nvn] nvnTextureViewGetTarget: view=%p target=0x%x set=%d\n", view, (unsigned)v.target,
                v.set_target);
    }
    return (NVNboolean)(v.set_target ? 1 : 0);
}

/* nvnTextureViewGetSwizzle(view, red, green, blue, alpha) -> boolean.
 *
 * The reference host answers with the identity R, G, B, A when the guest never
 * set a swizzle on the view (hle_nvn.c:2034), and says with the answer
 * whether the guest set one. */
NVNboolean nvnTextureViewGetSwizzle(const void *view, NVNtextureSwizzle *red, NVNtextureSwizzle *green,
                                    NVNtextureSwizzle *blue, NVNtextureSwizzle *alpha) {
    nvn_texture_view_state v = default_state();
    int refused = 0;
    int first = 0;
    pthread_mutex_lock(&s_view_lock);
    s_stats.calls++;
    s_stats.gets++;
    refused = !view_state_locked(view, &v);
    first = mark_logged(TV_GET_SWIZZLE);
    pthread_mutex_unlock(&s_view_lock);
    if (red) *red = v.set_swizzle ? v.swizzle[0] : NVN_TEXTURE_SWIZZLE_R;
    if (green) *green = v.set_swizzle ? v.swizzle[1] : NVN_TEXTURE_SWIZZLE_G;
    if (blue) *blue = v.set_swizzle ? v.swizzle[2] : NVN_TEXTURE_SWIZZLE_B;
    if (alpha) *alpha = v.set_swizzle ? v.swizzle[3] : NVN_TEXTURE_SWIZZLE_A;
    if (refused) {
        fprintf(stderr, "[nvn] nvnTextureViewGetSwizzle: view=%p was never described; the answer is the default, not "
                        "a value of this view\n",
                view);
        return 0;
    }
    if (first) {
        fprintf(stderr, "[nvn] nvnTextureViewGetSwizzle: view=%p swizzle=0x%x,0x%x,0x%x,0x%x set=%d\n", view,
                (unsigned)(v.set_swizzle ? v.swizzle[0] : NVN_TEXTURE_SWIZZLE_R),
                (unsigned)(v.set_swizzle ? v.swizzle[1] : NVN_TEXTURE_SWIZZLE_G),
                (unsigned)(v.set_swizzle ? v.swizzle[2] : NVN_TEXTURE_SWIZZLE_B),
                (unsigned)(v.set_swizzle ? v.swizzle[3] : NVN_TEXTURE_SWIZZLE_A), v.set_swizzle);
    }
    return (NVNboolean)(v.set_swizzle ? 1 : 0);
}

NVNboolean nvnTextureViewGetDepthStencilMode(const void *view, NVNtextureDepthStencilMode *mode) {
    nvn_texture_view_state v = default_state();
    int refused = 0;
    int first = 0;
    pthread_mutex_lock(&s_view_lock);
    s_stats.calls++;
    s_stats.gets++;
    refused = !view_state_locked(view, &v);
    first = mark_logged(TV_GET_DEPTH_STENCIL_MODE);
    pthread_mutex_unlock(&s_view_lock);
    if (mode) *mode = v.dsmode;
    if (refused) {
        fprintf(stderr, "[nvn] nvnTextureViewGetDepthStencilMode: view=%p was never described; the answer is the "
                        "default, not a value of this view\n",
                view);
        return 0;
    }
    if (first) {
        fprintf(stderr, "[nvn] nvnTextureViewGetDepthStencilMode: view=%p mode=0x%x set=%d\n", view, (unsigned)v.dsmode,
                v.set_dsmode);
    }
    return (NVNboolean)(v.set_dsmode ? 1 : 0);
}

/* ------------------------------------------------------------------ */
/* nvnTextureViewCompare                                                */
/* ------------------------------------------------------------------ */

/* nvnTextureViewCompare(a, b) -> boolean.
 *
 * The reference host does not have this entry point, so the meaning here is
 * this runtime's own and is NOT claimed to agree with the reference: two views
 * are the same when they carry the same description, that is when the same
 * fields are set and the values of those fields are the same. This is the same
 * shape as the comparison of the texture family, which compares the whole
 * description of a texture (hle_nvn.c:492).
 *
 * Two views that were never described carry the same (empty) description, so
 * they are the same view as far as this comparison is concerned. A view that
 * was never described and a view that was described are not the same. */
NVNboolean nvnTextureViewCompare(const void *a, const void *b) {
    nvn_texture_view_state x = default_state();
    nvn_texture_view_state y = default_state();
    int equal = 0;
    int first = 0;
    pthread_mutex_lock(&s_view_lock);
    s_stats.calls++;
    s_stats.compares++;
    view_state_locked(a, &x);
    view_state_locked(b, &y);
    first = mark_logged(TV_COMPARE);
    pthread_mutex_unlock(&s_view_lock);

    equal = x.set_levels == y.set_levels && x.base_level == y.base_level && x.nlevels == y.nlevels &&
            x.set_layers == y.set_layers && x.min_layer == y.min_layer && x.nlayers == y.nlayers &&
            x.set_format == y.set_format && x.format == y.format && x.set_target == y.set_target && x.target == y.target &&
            x.set_swizzle == y.set_swizzle && !memcmp(x.swizzle, y.swizzle, sizeof(x.swizzle)) &&
            x.set_dsmode == y.set_dsmode && x.dsmode == y.dsmode;
    if (first) {
        fprintf(stderr, "[nvn] nvnTextureViewCompare: a=%p b=%p -> %s\n", a, b, equal ? "the same" : "not the same");
    }
    return (NVNboolean)(equal ? 1 : 0);
}

/* ------------------------------------------------------------------ */
/* The names                                                            */
/* ------------------------------------------------------------------ */

/* The fourteen names the guest itself resolves, in the order the guest
 * resolves them in. */
static const nvn_texture_view_family_entry s_texture_view_family[] = {
    { "nvnTextureViewSetDefaults", (void *)(uintptr_t)nvnTextureViewSetDefaults },
    { "nvnTextureViewSetLevels", (void *)(uintptr_t)nvnTextureViewSetLevels },
    { "nvnTextureViewSetLayers", (void *)(uintptr_t)nvnTextureViewSetLayers },
    { "nvnTextureViewSetFormat", (void *)(uintptr_t)nvnTextureViewSetFormat },
    { "nvnTextureViewSetTarget", (void *)(uintptr_t)nvnTextureViewSetTarget },
    { "nvnTextureViewSetSwizzle", (void *)(uintptr_t)nvnTextureViewSetSwizzle },
    { "nvnTextureViewSetDepthStencilMode", (void *)(uintptr_t)nvnTextureViewSetDepthStencilMode },
    { "nvnTextureViewGetLevels", (void *)(uintptr_t)nvnTextureViewGetLevels },
    { "nvnTextureViewGetLayers", (void *)(uintptr_t)nvnTextureViewGetLayers },
    { "nvnTextureViewGetFormat", (void *)(uintptr_t)nvnTextureViewGetFormat },
    { "nvnTextureViewGetTarget", (void *)(uintptr_t)nvnTextureViewGetTarget },
    { "nvnTextureViewGetSwizzle", (void *)(uintptr_t)nvnTextureViewGetSwizzle },
    { "nvnTextureViewGetDepthStencilMode", (void *)(uintptr_t)nvnTextureViewGetDepthStencilMode },
    { "nvnTextureViewCompare", (void *)(uintptr_t)nvnTextureViewCompare },
};

size_t nvn_texture_view_family_count(void) {
    return sizeof(s_texture_view_family) / sizeof(s_texture_view_family[0]);
}

const nvn_texture_view_family_entry *nvn_texture_view_family_entry_at(size_t index) {
    if (index >= nvn_texture_view_family_count()) return NULL;
    return &s_texture_view_family[index];
}

void nvn_texture_view_get_stats(nvn_texture_view_stats *out) {
    size_t i;
    if (!out) return;
    pthread_mutex_lock(&s_view_lock);
    *out = s_stats;
    out->live_views = 0;
    for (i = 0; i < s_views.count; i++) {
        if (((nvn_texture_view_state *)nvn_object_table_at(&s_views, i))->in_use) out->live_views++;
    }
    pthread_mutex_unlock(&s_view_lock);
}

/* What this family knows about one view.
 *
 * The view is packed the way the newer SDK packs it: the flags are bits in
 * the first byte and the numbers are as small as they can be, so that the
 * whole view fits in the sixteen bytes the guest's own view object has. */
int nvn_texture_view_info_of(const void *view, ffrs_view *out, uint64_t *serial) {
    nvn_texture_view_state *slot;
    int i;
    if (!view || !out) return 0;
    pthread_mutex_lock(&s_view_lock);
    slot = view_find((void *)(uintptr_t)view, 0);
    if (!slot || !slot->in_use) {
        pthread_mutex_unlock(&s_view_lock);
        return 0;
    }
    memset(out, 0, sizeof(*out));
    out->set_levels = slot->set_levels ? 1 : 0;
    out->set_layers = slot->set_layers ? 1 : 0;
    out->set_format = slot->set_format ? 1 : 0;
    out->set_target = slot->set_target ? 1 : 0;
    out->set_swizzle = slot->set_swizzle ? 1 : 0;
    out->set_dsmode = slot->set_dsmode ? 1 : 0;
    out->dsmode = (uint8_t)(slot->dsmode & 0x3);
    out->target = (uint8_t)slot->target;
    out->base_level = (uint8_t)slot->base_level;
    out->nlevels = (uint8_t)slot->nlevels;
    out->format = (uint16_t)slot->format;
    out->min_layer = (uint16_t)slot->min_layer;
    out->nlayers = (uint16_t)slot->nlayers;
    for (i = 0; i < 4; i++) out->swz[i] = (uint8_t)slot->swizzle[i];
    /* The guest may set the same view object again; the copy has to know
     * that it is a new view and not the one it already holds. */
    if (serial) *serial = slot->serial;
    pthread_mutex_unlock(&s_view_lock);
    return 1;
}
