/* The NVN blend state family implementation.
 *
 * See nvn_blend_state.h for why the state lives on the host.
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

/* The blend function and equation values the NVN specification uses. Only
 * the ones the runtime needs to answer a query are named; the rest are
 * passed through as the numbers the guest gave. */
#define NVN_BLEND_FUNC_ZERO 0x0
#define NVN_BLEND_FUNC_ONE 0x1
#define NVN_BLEND_FUNC_SRC_COLOR 0x2
#define NVN_BLEND_FUNC_ONE_MINUS_SRC_COLOR 0x3
#define NVN_BLEND_FUNC_SRC_ALPHA 0x4
#define NVN_BLEND_FUNC_ONE_MINUS_SRC_ALPHA 0x5
#define NVN_BLEND_FUNC_DST_COLOR 0x6
#define NVN_BLEND_FUNC_ONE_MINUS_DST_COLOR 0x7
#define NVN_BLEND_FUNC_DST_ALPHA 0x8
#define NVN_BLEND_FUNC_ONE_MINUS_DST_ALPHA 0x9
#define NVN_BLEND_FUNC_SRC_ALPHA_SATURATE 0xA

#define NVN_BLEND_EQUATION_ADD 0x0
#define NVN_BLEND_EQUATION_SUBTRACT 0x1
#define NVN_BLEND_EQUATION_REVERSE_SUBTRACT 0x2
#define NVN_BLEND_EQUATION_MIN 0x3
#define NVN_BLEND_EQUATION_MAX 0x4

#define NVN_BLEND_ADVANCED_MODE_NONE 0x0
#define NVN_BLEND_ADVANCED_OVERLAP_NONE 0x0

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

#define NVN_BLEND_STATE_MAX_STATES 16

static pthread_mutex_t s_blend_lock = PTHREAD_MUTEX_INITIALIZER;
static nvn_blend_state_value s_states[NVN_BLEND_STATE_MAX_STATES];
static nvn_blend_state_stats s_stats;

static nvn_blend_state_value *state_find(void *state, int create) {
    size_t i;
    nvn_blend_state_value *free_slot = NULL;
    if (!state) return NULL;
    for (i = 0; i < NVN_BLEND_STATE_MAX_STATES; i++) {
        if (s_states[i].in_use) {
            if (s_states[i].state == state) return &s_states[i];
        } else if (!free_slot) {
            free_slot = &s_states[i];
        }
    }
    if (!create || !free_slot) return NULL;
    memset(free_slot, 0, sizeof(*free_slot));
    free_slot->state = state;
    free_slot->in_use = 1;
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
    for (i = 0; i < NVN_BLEND_STATE_MAX_STATES; i++) {
        if (s_states[i].in_use) out->live_states++;
    }
    pthread_mutex_unlock(&s_blend_lock);
}
