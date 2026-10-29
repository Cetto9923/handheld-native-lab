/* The NVN blend state family implementation.
 *
 * Packed value fields live in the guest object so ordinary copies retain them.
 *
 * The defaults are the ones the NVN specification gives: blending off, the
 * identity function on all four channels, the add equation, no advanced mode
 * and no advanced overlap. The guest asks for the defaults explicitly, and
 * it expects the state to really be the defaults afterwards, so SetDefaults
 * resets every field rather than doing nothing.
 */
#include "nvn_blend_state.h"

#include "nvn_device.h"

#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nvn_object_table.h"
#include "nvn_packed_state.h"
#include "../nvn/include/nvn/nvn.h"

#define NVN_BLEND_ADVANCED_OVERLAP_NONE NVN_BLEND_ADVANCED_OVERLAP_UNCORRELATED

typedef struct {
    void *state;
    int blend_target;
    int src_rgb;
    int src_alpha;
    int dst_rgb;
    int dst_alpha;
    int equation_rgb;
    int equation_alpha;
    int advanced_mode;
    int advanced_overlap;
    int advanced_premultiplied_src;
    int advanced_normalized_dst;
    int in_use;
} nvn_blend_state_value;

/* How many blend states the runtime tracks at the same time. The table grows
 * with the number of states the guest has, so this is a limit and not a guess. */
#define NVN_BLEND_STATE_MAX_OBJECTS 65536

static pthread_mutex_t s_blend_lock = PTHREAD_MUTEX_INITIALIZER;
static nvn_object_table s_states = { NULL, 0, 0, sizeof(nvn_blend_state_value) };
static nvn_blend_state_stats s_stats;

static void unpack_blend(nvn_blend_state_value *v) {
    ffrs_blend_st p;
    memcpy(&p, v->state, sizeof p);
    v->blend_target=p.target; v->src_rgb=p.src_rgb; v->dst_rgb=p.dst_rgb;
    v->src_alpha=p.src_a; v->dst_alpha=p.dst_a;
    v->equation_rgb=p.eq_rgb; v->equation_alpha=p.eq_a;
}
static void pack_blend(const nvn_blend_state_value *v) {
    if (!v) return;
    ffrs_blend_st p={v->blend_target,v->src_rgb,v->dst_rgb,v->src_alpha,v->dst_alpha,v->equation_rgb,v->equation_alpha};
    memcpy(v->state,&p,sizeof p);
}

static nvn_blend_state_value *state_find(void *state, int create) {
    (void)create;
    size_t i;
    nvn_blend_state_value *free_slot = NULL;
    if (!state) return NULL;
    for (i = 0; i < s_states.count; i++) {
        nvn_blend_state_value *entry = (nvn_blend_state_value *)nvn_object_table_at(&s_states, i);
        if (!entry) continue;
        if (entry->in_use) {
            if (entry->state == state) { unpack_blend(entry); return entry; }
        } else if (!free_slot) {
            free_slot = entry;
        }
    }
    if (!free_slot) {
        free_slot = (nvn_blend_state_value *)nvn_object_table_new_entry(&s_states, NVN_BLEND_STATE_MAX_OBJECTS);
        if (!free_slot) return NULL;
    }
    memset(free_slot, 0, sizeof(*free_slot));
    free_slot->state = state;
    free_slot->in_use = 1;
    unpack_blend(free_slot);
    return free_slot;
}

/* The defaults of the specification. */
static void apply_defaults(nvn_blend_state_value *v) {
    v->blend_target = 0;
    v->src_rgb = NVN_BLEND_FUNC_ONE;
    v->src_alpha = NVN_BLEND_FUNC_ONE;
    v->dst_rgb = NVN_BLEND_FUNC_ZERO;
    v->dst_alpha = NVN_BLEND_FUNC_ZERO;
    v->equation_rgb = NVN_BLEND_EQUATION_ADD;
    v->equation_alpha = NVN_BLEND_EQUATION_ADD;
    v->advanced_mode = NVN_BLEND_ADVANCED_MODE_NONE;
    v->advanced_overlap = NVN_BLEND_ADVANCED_OVERLAP_NONE;
    v->advanced_premultiplied_src = 0;
    v->advanced_normalized_dst = 0;
}

void nvn_blend_state_set_defaults(void *state) {
    nvn_blend_state_value *v;
    pthread_mutex_lock(&s_blend_lock);
    s_stats.builder_calls++;
    v = state_find(state, 1);
    if (v) {
        apply_defaults(v);
        s_stats.defaults_applied++;
    }
    pack_blend(v);
    pthread_mutex_unlock(&s_blend_lock);
    if (!v) fprintf(stderr, "[nvn] nvnBlendStateSetDefaults: no room for another blend state (%p)\n", state);
    NVN_VOID_RETURN();
}

void nvn_blend_state_set_blend_target(void *state, int blend_target) {
    nvn_blend_state_value *v;
    pthread_mutex_lock(&s_blend_lock);
    s_stats.builder_calls++;
    v = state_find(state, 1);
    if (v) {
        v->blend_target = blend_target;
        s_stats.sets++;
    }
    pack_blend(v);
    pthread_mutex_unlock(&s_blend_lock);
    NVN_VOID_RETURN();
}

void nvn_blend_state_set_blend_func(void *state, int src_rgb, int dst_rgb, int src_alpha,
                                    int dst_alpha) {
    nvn_blend_state_value *v;
    pthread_mutex_lock(&s_blend_lock);
    s_stats.builder_calls++;
    v = state_find(state, 1);
    if (v) {
        v->src_rgb = src_rgb;
        v->dst_rgb = dst_rgb;
        v->src_alpha = src_alpha;
        v->dst_alpha = dst_alpha;
        s_stats.sets++;
    }
    pack_blend(v);
    pthread_mutex_unlock(&s_blend_lock);
    NVN_VOID_RETURN();
}

void nvn_blend_state_set_blend_equation(void *state, int equation_rgb, int equation_alpha) {
    nvn_blend_state_value *v;
    pthread_mutex_lock(&s_blend_lock);
    s_stats.builder_calls++;
    v = state_find(state, 1);
    if (v) {
        v->equation_rgb = equation_rgb;
        v->equation_alpha = equation_alpha;
        s_stats.sets++;
    }
    pack_blend(v);
    pthread_mutex_unlock(&s_blend_lock);
    NVN_VOID_RETURN();
}

void nvn_blend_state_set_advanced_mode(void *state, int mode) {
    nvn_blend_state_value *v;
    pthread_mutex_lock(&s_blend_lock);
    s_stats.builder_calls++;
    v = state_find(state, 1);
    if (v) {
        v->advanced_mode = mode;
        s_stats.sets++;
    }
    pack_blend(v);
    pthread_mutex_unlock(&s_blend_lock);
    NVN_VOID_RETURN();
}

void nvn_blend_state_set_advanced_overlap(void *state, int overlap) {
    nvn_blend_state_value *v;
    pthread_mutex_lock(&s_blend_lock);
    s_stats.builder_calls++;
    v = state_find(state, 1);
    if (v) {
        v->advanced_overlap = overlap;
        s_stats.sets++;
    }
    pack_blend(v);
    pthread_mutex_unlock(&s_blend_lock);
    NVN_VOID_RETURN();
}

void nvn_blend_state_set_advanced_premultiplied_src(void *state, int premultiplied) {
    nvn_blend_state_value *v;
    pthread_mutex_lock(&s_blend_lock);
    s_stats.builder_calls++;
    v = state_find(state, 1);
    if (v) {
        v->advanced_premultiplied_src = premultiplied ? 1 : 0;
        s_stats.sets++;
    }
    pack_blend(v);
    pthread_mutex_unlock(&s_blend_lock);
    NVN_VOID_RETURN();
}

void nvn_blend_state_set_advanced_normalized_dst(void *state, int normalized) {
    nvn_blend_state_value *v;
    pthread_mutex_lock(&s_blend_lock);
    s_stats.builder_calls++;
    v = state_find(state, 1);
    if (v) {
        v->advanced_normalized_dst = normalized ? 1 : 0;
        s_stats.sets++;
    }
    pack_blend(v);
    pthread_mutex_unlock(&s_blend_lock);
    NVN_VOID_RETURN();
}

int nvn_blend_state_get_blend_target(const void *state) {
    nvn_blend_state_value *v;
    int value = 0;
    pthread_mutex_lock(&s_blend_lock);
    s_stats.builder_calls++;
    v = state_find((void *)(uintptr_t)state, 0);
    if (v) {
        value = v->blend_target;
        s_stats.gets++;
    }
    pthread_mutex_unlock(&s_blend_lock);
    return value;
}

/* The getters that write through their arguments. A state that was never
 * prepared answers with the defaults, because that is what the guest would
 * have got if it had asked for them.
 *
 * The order of the blend functions is the order the NVN specification uses:
 * the source of the colour channels, the destination of the colour channels,
 * the source of the alpha channel and the destination of the alpha channel.
 * The getter answers in the same order the setter takes them. */
void nvn_blend_state_get_blend_func(const void *state, int *src_rgb, int *dst_rgb, int *src_alpha,
                                    int *dst_alpha) {
    nvn_blend_state_value v;
    nvn_blend_state_value *found;
    pthread_mutex_lock(&s_blend_lock);
    s_stats.builder_calls++;
    found = state_find((void *)(uintptr_t)state, 0);
    if (found) {
        v = *found;
    } else {
        memset(&v, 0, sizeof(v));
        apply_defaults(&v);
    }
    pthread_mutex_unlock(&s_blend_lock);
    if (src_rgb) *src_rgb = v.src_rgb;
    if (dst_rgb) *dst_rgb = v.dst_rgb;
    if (src_alpha) *src_alpha = v.src_alpha;
    if (dst_alpha) *dst_alpha = v.dst_alpha;
    s_stats.gets++;
    NVN_VOID_RETURN();
}

void nvn_blend_state_get_blend_equation(const void *state, int *equation_rgb, int *equation_alpha) {
    nvn_blend_state_value v;
    nvn_blend_state_value *found;
    pthread_mutex_lock(&s_blend_lock);
    s_stats.builder_calls++;
    found = state_find((void *)(uintptr_t)state, 0);
    if (found) {
        v = *found;
    } else {
        memset(&v, 0, sizeof(v));
        apply_defaults(&v);
    }
    pthread_mutex_unlock(&s_blend_lock);
    if (equation_rgb) *equation_rgb = v.equation_rgb;
    if (equation_alpha) *equation_alpha = v.equation_alpha;
    s_stats.gets++;
    NVN_VOID_RETURN();
}

int nvn_blend_state_get_advanced_mode(const void *state) {
    nvn_blend_state_value *v;
    int value = NVN_BLEND_ADVANCED_MODE_NONE;
    pthread_mutex_lock(&s_blend_lock);
    s_stats.builder_calls++;
    v = state_find((void *)(uintptr_t)state, 0);
    if (v) {
        value = v->advanced_mode;
        s_stats.gets++;
    }
    pthread_mutex_unlock(&s_blend_lock);
    return value;
}

int nvn_blend_state_get_advanced_overlap(const void *state) {
    nvn_blend_state_value *v;
    int value = NVN_BLEND_ADVANCED_OVERLAP_NONE;
    pthread_mutex_lock(&s_blend_lock);
    s_stats.builder_calls++;
    v = state_find((void *)(uintptr_t)state, 0);
    if (v) {
        value = v->advanced_overlap;
        s_stats.gets++;
    }
    pthread_mutex_unlock(&s_blend_lock);
    return value;
}

int nvn_blend_state_get_advanced_premultiplied_src(const void *state) {
    nvn_blend_state_value *v;
    int value = 0;
    pthread_mutex_lock(&s_blend_lock);
    s_stats.builder_calls++;
    v = state_find((void *)(uintptr_t)state, 0);
    if (v) {
        value = v->advanced_premultiplied_src;
        s_stats.gets++;
    }
    pthread_mutex_unlock(&s_blend_lock);
    return value;
}

int nvn_blend_state_get_advanced_normalized_dst(const void *state) {
    nvn_blend_state_value *v;
    int value = 0;
    pthread_mutex_lock(&s_blend_lock);
    s_stats.builder_calls++;
    v = state_find((void *)(uintptr_t)state, 0);
    if (v) {
        value = v->advanced_normalized_dst;
        s_stats.gets++;
    }
    pthread_mutex_unlock(&s_blend_lock);
    return value;
}

/* ------------------------------------------------------------------ */
/* The names                                                           */
/* ------------------------------------------------------------------ */

static const nvn_blend_state_family_entry s_blend_state_family[] = {
    { "nvnBlendStateSetDefaults", (void *)(uintptr_t)nvn_blend_state_set_defaults },
    { "nvnBlendStateSetBlendTarget", (void *)(uintptr_t)nvn_blend_state_set_blend_target },
    { "nvnBlendStateSetBlendFunc", (void *)(uintptr_t)nvn_blend_state_set_blend_func },
    { "nvnBlendStateSetBlendEquation", (void *)(uintptr_t)nvn_blend_state_set_blend_equation },
    { "nvnBlendStateSetAdvancedMode", (void *)(uintptr_t)nvn_blend_state_set_advanced_mode },
    { "nvnBlendStateSetAdvancedOverlap", (void *)(uintptr_t)nvn_blend_state_set_advanced_overlap },
    { "nvnBlendStateSetAdvancedPremultipliedSrc",
      (void *)(uintptr_t)nvn_blend_state_set_advanced_premultiplied_src },
    { "nvnBlendStateSetAdvancedNormalizedDst",
      (void *)(uintptr_t)nvn_blend_state_set_advanced_normalized_dst },
    { "nvnBlendStateGetBlendTarget", (void *)(uintptr_t)nvn_blend_state_get_blend_target },
    { "nvnBlendStateGetBlendFunc", (void *)(uintptr_t)nvn_blend_state_get_blend_func },
    { "nvnBlendStateGetBlendEquation", (void *)(uintptr_t)nvn_blend_state_get_blend_equation },
    { "nvnBlendStateGetAdvancedMode", (void *)(uintptr_t)nvn_blend_state_get_advanced_mode },
    { "nvnBlendStateGetAdvancedOverlap", (void *)(uintptr_t)nvn_blend_state_get_advanced_overlap },
    { "nvnBlendStateGetAdvancedPremultipliedSrc",
      (void *)(uintptr_t)nvn_blend_state_get_advanced_premultiplied_src },
    { "nvnBlendStateGetAdvancedNormalizedDst",
      (void *)(uintptr_t)nvn_blend_state_get_advanced_normalized_dst },
};

size_t nvn_blend_state_family_count(void) {
    return sizeof(s_blend_state_family) / sizeof(s_blend_state_family[0]);
}

const nvn_blend_state_family_entry *nvn_blend_state_family_entry_at(size_t index) {
    if (index >= nvn_blend_state_family_count()) return NULL;
    return &s_blend_state_family[index];
}

void nvn_blend_state_get_stats(nvn_blend_state_stats *out) {
    size_t i;
    if (!out) return;
    pthread_mutex_lock(&s_blend_lock);
    *out = s_stats;
    out->live_states = 0;
    for (i = 0; i < s_states.count; i++) {
        if (((nvn_blend_state_value *)nvn_object_table_at(&s_states, i))->in_use) out->live_states++;
    }
    pthread_mutex_unlock(&s_blend_lock);
}
