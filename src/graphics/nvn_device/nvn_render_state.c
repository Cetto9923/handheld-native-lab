/* The NVN render state family implementation.
 *
 * See nvn_render_state.h for why the state lives on the host.
 *
 * The defaults are the ones the NVN specification gives. The guest asks for
 * the defaults explicitly, and it expects the state to really be the defaults
 * afterwards, so SetDefaults resets every field rather than doing nothing.
 *
 * The names and the argument order are the ones the guest itself asks for.
 * The guest resolved 65 of these names on the device, and nxnative, which
 * runs this very game on another host, implements the same set with the same
 * signatures; where the two could disagree the guest's resolution is the
 * authority for the name and nxnative is the authority for the order.
 *
 * The boolean arguments are read from their low eight bits only. NVNboolean
 * is a byte, and the callers of these entry points do not extend what they
 * pass, so the upper bits of the register are not part of the value.
 */
#include "nvn_render_state.h"

#include "nvn_device.h"

#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The enumeration values the NVN specification uses. Only the ones the
 * runtime needs are named; the rest are passed through as the numbers the
 * guest gave. */
#define NVN_LOGIC_OP_COPY 0x3
#define NVN_ALPHA_FUNC_ALWAYS 0x8
#define NVN_COVERAGE_MODULATION_MODE_NONE 0x0
#define NVN_DEPTH_FUNC_LESS 0x2
#define NVN_STENCIL_FUNC_ALWAYS 0x8
#define NVN_STENCIL_OP_KEEP 0x1
#define NVN_FACE_NONE 0x0
#define NVN_FACE_FRONT 0x1
#define NVN_FACE_BACK 0x2
#define NVN_FRONT_FACE_CCW 0x1
#define NVN_POLYGON_MODE_FILL 0x2
#define NVN_POLYGON_OFFSET_ENABLE_NONE 0x0

/* The number of render targets a state can address. */
#define NVN_RS_TARGETS 8

/* The number of sample locations a multisample state can hold. */
#define NVN_RS_SAMPLE_LOCATIONS 16

/* A boolean argument carries its value in the low eight bits only. */
#define NVN_RS_BOOL(x) (((x) & 0xff) != 0)

/* The families. */
#define NVN_RS_COLOR 0
#define NVN_RS_CHANNEL_MASK 1
#define NVN_RS_MULTISAMPLE 2
#define NVN_RS_POLYGON 3
#define NVN_RS_DEPTH_STENCIL 4
#define NVN_RS_VERTEX_ATTRIB 5
#define NVN_RS_VERTEX_STREAM 6

typedef struct {
    void *object;
    int family;

    /* The colour state: which targets blend, the logic operation and the
     * alpha test. */
    uint32_t color_blend_enable;
    int color_logic_op;
    int color_alpha_test;

    /* The channel mask state: four bits per target, one per channel, a set
     * bit meaning the channel is written. */
    uint32_t channel_mask;

    /* The multisample state. */
    int ms_enable;
    int ms_samples;
    int ms_alpha_to_coverage_enable;
    int ms_alpha_to_coverage_dither;
    int ms_raster_samples;
    int ms_coverage_modulation_mode;
    int ms_coverage_to_color_enable;
    int ms_coverage_to_color_output;
    int ms_sample_locations_enable;
    int ms_sample_locations_grid_enable;
    int ms_sample_locations_grid[2];
    int ms_sample_locations_count;
    float ms_sample_locations[NVN_RS_SAMPLE_LOCATIONS][2];

    /* The polygon state. */
    int polygon_cull_face;
    int polygon_front_face;
    int polygon_mode;
    int polygon_offset_enables;

    /* The depth stencil state. The stencil half has a front face and a back
     * face. */
    int ds_depth_test_enable;
    int ds_depth_write_enable;
    int ds_depth_func;
    int ds_stencil_test_enable;
    int ds_stencil_func[2];
    int ds_stencil_op[2][3];

    /* The vertex attribute state. */
    int va_format;
    int64_t va_offset;
    int va_stream_index;

    /* The vertex stream state. */
    int64_t vs_stride;
    int vs_divisor;

    int in_use;
} nvn_rs_value;

#define NVN_RS_MAX_STATES 64

static pthread_mutex_t s_rs_lock = PTHREAD_MUTEX_INITIALIZER;
static nvn_rs_value s_states[NVN_RS_MAX_STATES];
static nvn_render_state_stats s_stats;

/* The defaults of the specification. The identity of the state and the fact
 * that the table slot is occupied are not part of the family, so they survive
 * the reset. */
static void apply_defaults(nvn_rs_value *v, int family) {
    void *object = v->object;
    int fam = v->family;
    int used = v->in_use;
    int i;
    int j;
    memset(v, 0, sizeof(*v));
    v->object = object;
    v->family = fam;
    v->in_use = used;
    switch (family) {
    case NVN_RS_COLOR:
        v->color_blend_enable = 0;
        v->color_logic_op = NVN_LOGIC_OP_COPY;
        v->color_alpha_test = NVN_ALPHA_FUNC_ALWAYS;
        break;
    case NVN_RS_CHANNEL_MASK:
        /* Every channel of every target is written. */
        v->channel_mask = 0xffffffffu;
        break;
    case NVN_RS_MULTISAMPLE:
        v->ms_enable = 1;
        break;
    case NVN_RS_POLYGON:
        v->polygon_cull_face = NVN_FACE_NONE;
        v->polygon_front_face = NVN_FRONT_FACE_CCW;
        v->polygon_mode = NVN_POLYGON_MODE_FILL;
        v->polygon_offset_enables = NVN_POLYGON_OFFSET_ENABLE_NONE;
        break;
    case NVN_RS_DEPTH_STENCIL:
        v->ds_depth_test_enable = 0;
        v->ds_depth_write_enable = 1;
        v->ds_depth_func = NVN_DEPTH_FUNC_LESS;
        v->ds_stencil_test_enable = 0;
        v->ds_stencil_func[0] = NVN_STENCIL_FUNC_ALWAYS;
        v->ds_stencil_func[1] = NVN_STENCIL_FUNC_ALWAYS;
        for (i = 0; i < 2; i++) {
            for (j = 0; j < 3; j++) v->ds_stencil_op[i][j] = NVN_STENCIL_OP_KEEP;
        }
        break;
    case NVN_RS_VERTEX_ATTRIB:
        v->va_format = 0;
        v->va_offset = 0;
        v->va_stream_index = 0;
        break;
    case NVN_RS_VERTEX_STREAM:
        v->vs_stride = 0;
        v->vs_divisor = 0;
        break;
    default:
        break;
    }
}

static nvn_rs_value *state_find(const void *object, int family, int create) {
    size_t i;
    nvn_rs_value *free_slot = NULL;
    if (!object) return NULL;
    for (i = 0; i < NVN_RS_MAX_STATES; i++) {
        if (s_states[i].in_use) {
            if (s_states[i].object == object && s_states[i].family == family) return &s_states[i];
        } else if (!free_slot) {
            free_slot = &s_states[i];
        }
    }
    if (!create || !free_slot) return NULL;
    memset(free_slot, 0, sizeof(*free_slot));
    free_slot->object = (void *)(uintptr_t)object;
    free_slot->family = family;
    apply_defaults(free_slot, family);
    free_slot->in_use = 1;
    return free_slot;
}

/* The boilerplate the setters share. The parameter is not called family:
 * a macro parameter is substituted everywhere in its body, and the state
 * record has a member of that name. */
#define RS_BEGIN_WRITE(rs_family)                                                     \
    nvn_rs_value *v;                                                                  \
    pthread_mutex_lock(&s_rs_lock);                                                   \
    s_stats.calls++;                                                                  \
    v = state_find(object, (rs_family), 1);                                           \
    if (!v)                                                                           \
        fprintf(stderr, "[nvn] the render state table is full; the state was not recorded\n");

/* The boilerplate the getters share. A state that was never prepared answers
 * with the defaults, because that is what the guest would have got if it had
 * asked for them. */
#define RS_BEGIN_READ(rs_family)                                                      \
    nvn_rs_value tmp;                                                                 \
    nvn_rs_value *v;                                                                  \
    pthread_mutex_lock(&s_rs_lock);                                                   \
    s_stats.calls++;                                                                  \
    v = state_find(object, (rs_family), 0);                                           \
    if (v) {                                                                          \
        tmp = *v;                                                                     \
    } else {                                                                          \
        memset(&tmp, 0, sizeof(tmp));                                                 \
        tmp.family = (rs_family);                                                     \
        apply_defaults(&tmp, (rs_family));                                            \
    }

#define RS_END() pthread_mutex_unlock(&s_rs_lock);

/* ------------------------------------------------------------------ */
/* The colour state                                                    */
/* ------------------------------------------------------------------ */

void nvn_render_state_color_set_defaults(void *object) {
    RS_BEGIN_WRITE(NVN_RS_COLOR)
    if (v) {
        apply_defaults(v, NVN_RS_COLOR);
        s_stats.defaults_applied++;
    }
    RS_END()
    NVN_VOID_RETURN();
}

void nvn_render_state_color_set_blend_enable(void *object, int target, uint32_t enable) {
    RS_BEGIN_WRITE(NVN_RS_COLOR)
    if (v) {
        if (target >= 0 && target < NVN_RS_TARGETS) {
            if (NVN_RS_BOOL(enable)) {
                v->color_blend_enable |= (1u << target);
            } else {
                v->color_blend_enable &= ~(1u << target);
            }
        }
        s_stats.sets++;
    }
    RS_END()
    NVN_VOID_RETURN();
}

void nvn_render_state_color_set_logic_op(void *object, int logic_op) {
    RS_BEGIN_WRITE(NVN_RS_COLOR)
    if (v) {
        v->color_logic_op = logic_op;
        s_stats.sets++;
    }
    RS_END()
    NVN_VOID_RETURN();
}

void nvn_render_state_color_set_alpha_test(void *object, int alpha_test) {
    RS_BEGIN_WRITE(NVN_RS_COLOR)
    if (v) {
        v->color_alpha_test = alpha_test;
        s_stats.sets++;
    }
    RS_END()
    NVN_VOID_RETURN();
}

int nvn_render_state_color_get_blend_enable(const void *object, int target) {
    int value = 0;
    RS_BEGIN_READ(NVN_RS_COLOR)
    if (target >= 0 && target < NVN_RS_TARGETS && ((tmp.color_blend_enable >> target) & 1u)) value = 1;
    RS_END()
    s_stats.gets++;
    return value;
}

int nvn_render_state_color_get_logic_op(const void *object) {
    int value = NVN_LOGIC_OP_COPY;
    RS_BEGIN_READ(NVN_RS_COLOR)
    value = tmp.color_logic_op;
    RS_END()
    s_stats.gets++;
    return value;
}

int nvn_render_state_color_get_alpha_test(const void *object) {
    int value = NVN_ALPHA_FUNC_ALWAYS;
    RS_BEGIN_READ(NVN_RS_COLOR)
    value = tmp.color_alpha_test;
    RS_END()
    s_stats.gets++;
    return value;
}

/* ------------------------------------------------------------------ */
/* The channel mask state                                              */
/* ------------------------------------------------------------------ */

void nvn_render_state_channel_mask_set_defaults(void *object) {
    RS_BEGIN_WRITE(NVN_RS_CHANNEL_MASK)
    if (v) {
        apply_defaults(v, NVN_RS_CHANNEL_MASK);
        s_stats.defaults_applied++;
    }
    RS_END()
    NVN_VOID_RETURN();
}

void nvn_render_state_channel_mask_set_channel_mask(void *object, int target, uint32_t red,
                                                    uint32_t green, uint32_t blue, uint32_t alpha) {
    RS_BEGIN_WRITE(NVN_RS_CHANNEL_MASK)
    if (v) {
        if (target >= 0 && target < NVN_RS_TARGETS) {
            uint32_t m = (NVN_RS_BOOL(red) ? 1u : 0u) | (NVN_RS_BOOL(green) ? 2u : 0u) |
                         (NVN_RS_BOOL(blue) ? 4u : 0u) | (NVN_RS_BOOL(alpha) ? 8u : 0u);
            v->channel_mask = (v->channel_mask & ~(0xfu << (4 * target))) | (m << (4 * target));
        }
        s_stats.sets++;
    }
    RS_END()
    NVN_VOID_RETURN();
}

void nvn_render_state_channel_mask_get_channel_mask(const void *object, int target, int *red,
                                                     int *green, int *blue, int *alpha) {
    uint32_t m = 0;
    RS_BEGIN_READ(NVN_RS_CHANNEL_MASK)
    if (target >= 0 && target < NVN_RS_TARGETS) m = (tmp.channel_mask >> (4 * target)) & 0xfu;
    RS_END()
    if (red) *red = (m & 1u) != 0;
    if (green) *green = (m & 2u) != 0;
    if (blue) *blue = (m & 4u) != 0;
    if (alpha) *alpha = (m & 8u) != 0;
    s_stats.gets++;
    NVN_VOID_RETURN();
}

/* ------------------------------------------------------------------ */
/* The multisample state                                               */
/* ------------------------------------------------------------------ */

void nvn_render_state_multisample_set_defaults(void *object) {
    RS_BEGIN_WRITE(NVN_RS_MULTISAMPLE)
    if (v) {
        apply_defaults(v, NVN_RS_MULTISAMPLE);
        s_stats.defaults_applied++;
    }
    RS_END()
    NVN_VOID_RETURN();
}

void nvn_render_state_multisample_set_multisample_enable(void *object, uint32_t enable) {
    RS_BEGIN_WRITE(NVN_RS_MULTISAMPLE)
    if (v) {
        v->ms_enable = NVN_RS_BOOL(enable);
        s_stats.sets++;
    }
    RS_END()
    NVN_VOID_RETURN();
}

void nvn_render_state_multisample_set_samples(void *object, int samples) {
    RS_BEGIN_WRITE(NVN_RS_MULTISAMPLE)
    if (v) {
        v->ms_samples = samples;
        s_stats.sets++;
    }
    RS_END()
    NVN_VOID_RETURN();
}

void nvn_render_state_multisample_set_alpha_to_coverage_enable(void *object, uint32_t enable) {
    RS_BEGIN_WRITE(NVN_RS_MULTISAMPLE)
    if (v) {
        v->ms_alpha_to_coverage_enable = NVN_RS_BOOL(enable);
        s_stats.sets++;
    }
    RS_END()
    NVN_VOID_RETURN();
}

void nvn_render_state_multisample_set_alpha_to_coverage_dither(void *object, uint32_t enable) {
    RS_BEGIN_WRITE(NVN_RS_MULTISAMPLE)
    if (v) {
        v->ms_alpha_to_coverage_dither = NVN_RS_BOOL(enable);
        s_stats.sets++;
    }
    RS_END()
    NVN_VOID_RETURN();
}

void nvn_render_state_multisample_set_raster_samples(void *object, int samples) {
    RS_BEGIN_WRITE(NVN_RS_MULTISAMPLE)
    if (v) {
        v->ms_raster_samples = samples;
        s_stats.sets++;
    }
    RS_END()
    NVN_VOID_RETURN();
}

void nvn_render_state_multisample_set_coverage_modulation_mode(void *object, int mode) {
    RS_BEGIN_WRITE(NVN_RS_MULTISAMPLE)
    if (v) {
        v->ms_coverage_modulation_mode = mode;
        s_stats.sets++;
    }
    RS_END()
    NVN_VOID_RETURN();
}

void nvn_render_state_multisample_set_coverage_to_color_enable(void *object, uint32_t enable) {
    RS_BEGIN_WRITE(NVN_RS_MULTISAMPLE)
    if (v) {
        v->ms_coverage_to_color_enable = NVN_RS_BOOL(enable);
        s_stats.sets++;
    }
    RS_END()
    NVN_VOID_RETURN();
}

void nvn_render_state_multisample_set_coverage_to_color_output(void *object, int output) {
    RS_BEGIN_WRITE(NVN_RS_MULTISAMPLE)
    if (v) {
        v->ms_coverage_to_color_output = output;
        s_stats.sets++;
    }
    RS_END()
    NVN_VOID_RETURN();
}

void nvn_render_state_multisample_set_sample_locations_enable(void *object, uint32_t enable) {
    RS_BEGIN_WRITE(NVN_RS_MULTISAMPLE)
    if (v) {
        v->ms_sample_locations_enable = NVN_RS_BOOL(enable);
        s_stats.sets++;
    }
    RS_END()
    NVN_VOID_RETURN();
}

void nvn_render_state_multisample_set_sample_locations_grid_enable(void *object, uint32_t enable) {
    RS_BEGIN_WRITE(NVN_RS_MULTISAMPLE)
    if (v) {
        v->ms_sample_locations_grid_enable = NVN_RS_BOOL(enable);
        s_stats.sets++;
    }
    RS_END()
    NVN_VOID_RETURN();
}

void nvn_render_state_multisample_set_sample_locations(void *object, int start, int count,
                                                        const float *locations) {
    RS_BEGIN_WRITE(NVN_RS_MULTISAMPLE)
    if (v) {
        int i;
        int n = count;
        if (n < 0) n = 0;
        if (n > NVN_RS_SAMPLE_LOCATIONS) n = NVN_RS_SAMPLE_LOCATIONS;
        if (start < 0) start = 0;
        if (start > NVN_RS_SAMPLE_LOCATIONS) start = NVN_RS_SAMPLE_LOCATIONS;
        if (start + n > NVN_RS_SAMPLE_LOCATIONS) n = NVN_RS_SAMPLE_LOCATIONS - start;
        for (i = 0; i < n; i++) {
            if (locations) {
                v->ms_sample_locations[start + i][0] = locations[2 * i];
                v->ms_sample_locations[start + i][1] = locations[2 * i + 1];
            } else {
                v->ms_sample_locations[start + i][0] = 0.0f;
                v->ms_sample_locations[start + i][1] = 0.0f;
            }
        }
        if (start + n > v->ms_sample_locations_count) v->ms_sample_locations_count = start + n;
        s_stats.sets++;
    }
    RS_END()
    NVN_VOID_RETURN();
}

int nvn_render_state_multisample_get_multisample_enable(const void *object) {
    int value = 0;
    RS_BEGIN_READ(NVN_RS_MULTISAMPLE)
    value = tmp.ms_enable;
    RS_END()
    s_stats.gets++;
    return value;
}

int nvn_render_state_multisample_get_samples(const void *object) {
    int value = 0;
    RS_BEGIN_READ(NVN_RS_MULTISAMPLE)
    value = tmp.ms_samples;
    RS_END()
    s_stats.gets++;
    return value;
}

int nvn_render_state_multisample_get_alpha_to_coverage_enable(const void *object) {
    int value = 0;
    RS_BEGIN_READ(NVN_RS_MULTISAMPLE)
    value = tmp.ms_alpha_to_coverage_enable;
    RS_END()
    s_stats.gets++;
    return value;
}

int nvn_render_state_multisample_get_alpha_to_coverage_dither(const void *object) {
    int value = 0;
    RS_BEGIN_READ(NVN_RS_MULTISAMPLE)
    value = tmp.ms_alpha_to_coverage_dither;
    RS_END()
    s_stats.gets++;
    return value;
}

int nvn_render_state_multisample_get_raster_samples(const void *object) {
    int value = 0;
    RS_BEGIN_READ(NVN_RS_MULTISAMPLE)
    value = tmp.ms_raster_samples;
    RS_END()
    s_stats.gets++;
    return value;
}

int nvn_render_state_multisample_get_coverage_modulation_mode(const void *object) {
    int value = NVN_COVERAGE_MODULATION_MODE_NONE;
    RS_BEGIN_READ(NVN_RS_MULTISAMPLE)
    value = tmp.ms_coverage_modulation_mode;
    RS_END()
    s_stats.gets++;
    return value;
}

int nvn_render_state_multisample_get_coverage_to_color_enable(const void *object) {
    int value = 0;
    RS_BEGIN_READ(NVN_RS_MULTISAMPLE)
    value = tmp.ms_coverage_to_color_enable;
    RS_END()
    s_stats.gets++;
    return value;
}

int nvn_render_state_multisample_get_coverage_to_color_output(const void *object) {
    int value = 0;
    RS_BEGIN_READ(NVN_RS_MULTISAMPLE)
    value = tmp.ms_coverage_to_color_output;
    RS_END()
    s_stats.gets++;
    return value;
}

int nvn_render_state_multisample_get_sample_locations_enable(const void *object) {
    int value = 0;
    RS_BEGIN_READ(NVN_RS_MULTISAMPLE)
    value = tmp.ms_sample_locations_enable;
    RS_END()
    s_stats.gets++;
    return value;
}

int nvn_render_state_multisample_get_sample_locations_grid_enable(const void *object) {
    int value = 0;
    RS_BEGIN_READ(NVN_RS_MULTISAMPLE)
    value = tmp.ms_sample_locations_grid_enable;
    RS_END()
    s_stats.gets++;
    return value;
}

void nvn_render_state_multisample_get_sample_locations_grid(const void *object, int *width,
                                                             int *height) {
    int w = 0;
    int h = 0;
    RS_BEGIN_READ(NVN_RS_MULTISAMPLE)
    w = tmp.ms_sample_locations_grid[0];
    h = tmp.ms_sample_locations_grid[1];
    RS_END()
    if (width) *width = w;
    if (height) *height = h;
    s_stats.gets++;
    NVN_VOID_RETURN();
}

/* ------------------------------------------------------------------ */
/* The polygon state                                                   */
/* ------------------------------------------------------------------ */

void nvn_render_state_polygon_set_defaults(void *object) {
    RS_BEGIN_WRITE(NVN_RS_POLYGON)
    if (v) {
        apply_defaults(v, NVN_RS_POLYGON);
        s_stats.defaults_applied++;
    }
    RS_END()
    NVN_VOID_RETURN();
}

void nvn_render_state_polygon_set_cull_face(void *object, int face) {
    RS_BEGIN_WRITE(NVN_RS_POLYGON)
    if (v) {
        v->polygon_cull_face = face;
        s_stats.sets++;
    }
    RS_END()
    NVN_VOID_RETURN();
}

void nvn_render_state_polygon_set_front_face(void *object, int front_face) {
    RS_BEGIN_WRITE(NVN_RS_POLYGON)
    if (v) {
        v->polygon_front_face = front_face;
        s_stats.sets++;
    }
    RS_END()
    NVN_VOID_RETURN();
}

void nvn_render_state_polygon_set_polygon_mode(void *object, int mode) {
    RS_BEGIN_WRITE(NVN_RS_POLYGON)
    if (v) {
        v->polygon_mode = mode;
        s_stats.sets++;
    }
    RS_END()
    NVN_VOID_RETURN();
}

void nvn_render_state_polygon_set_polygon_offset_enables(void *object, int enables) {
    RS_BEGIN_WRITE(NVN_RS_POLYGON)
    if (v) {
        v->polygon_offset_enables = enables;
        s_stats.sets++;
    }
    RS_END()
    NVN_VOID_RETURN();
}

int nvn_render_state_polygon_get_cull_face(const void *object) {
    int value = NVN_FACE_NONE;
    RS_BEGIN_READ(NVN_RS_POLYGON)
    value = tmp.polygon_cull_face;
    RS_END()
    s_stats.gets++;
    return value;
}

int nvn_render_state_polygon_get_front_face(const void *object) {
    int value = NVN_FRONT_FACE_CCW;
    RS_BEGIN_READ(NVN_RS_POLYGON)
    value = tmp.polygon_front_face;
    RS_END()
    s_stats.gets++;
    return value;
}

int nvn_render_state_polygon_get_polygon_mode(const void *object) {
    int value = NVN_POLYGON_MODE_FILL;
    RS_BEGIN_READ(NVN_RS_POLYGON)
    value = tmp.polygon_mode;
    RS_END()
    s_stats.gets++;
    return value;
}

int nvn_render_state_polygon_get_polygon_offset_enables(const void *object) {
    int value = NVN_POLYGON_OFFSET_ENABLE_NONE;
    RS_BEGIN_READ(NVN_RS_POLYGON)
    value = tmp.polygon_offset_enables;
    RS_END()
    s_stats.gets++;
    return value;
}

/* ------------------------------------------------------------------ */
/* The depth stencil state                                             */
/* ------------------------------------------------------------------ */

void nvn_render_state_depth_stencil_set_defaults(void *object) {
    RS_BEGIN_WRITE(NVN_RS_DEPTH_STENCIL)
    if (v) {
        apply_defaults(v, NVN_RS_DEPTH_STENCIL);
        s_stats.defaults_applied++;
    }
    RS_END()
    NVN_VOID_RETURN();
}

void nvn_render_state_depth_stencil_set_depth_test_enable(void *object, uint32_t enable) {
    RS_BEGIN_WRITE(NVN_RS_DEPTH_STENCIL)
    if (v) {
        v->ds_depth_test_enable = NVN_RS_BOOL(enable);
        s_stats.sets++;
    }
    RS_END()
    NVN_VOID_RETURN();
}

void nvn_render_state_depth_stencil_set_depth_write_enable(void *object, uint32_t enable) {
    RS_BEGIN_WRITE(NVN_RS_DEPTH_STENCIL)
    if (v) {
        v->ds_depth_write_enable = NVN_RS_BOOL(enable);
        s_stats.sets++;
    }
    RS_END()
    NVN_VOID_RETURN();
}

void nvn_render_state_depth_stencil_set_depth_func(void *object, int func) {
    RS_BEGIN_WRITE(NVN_RS_DEPTH_STENCIL)
    if (v) {
        v->ds_depth_func = func;
        s_stats.sets++;
    }
    RS_END()
    NVN_VOID_RETURN();
}

void nvn_render_state_depth_stencil_set_stencil_test_enable(void *object, uint32_t enable) {
    RS_BEGIN_WRITE(NVN_RS_DEPTH_STENCIL)
    if (v) {
        v->ds_stencil_test_enable = NVN_RS_BOOL(enable);
        s_stats.sets++;
    }
    RS_END()
    NVN_VOID_RETURN();
}

void nvn_render_state_depth_stencil_set_stencil_func(void *object, int face, int func) {
    RS_BEGIN_WRITE(NVN_RS_DEPTH_STENCIL)
    if (v) {
        if (face & NVN_FACE_FRONT) v->ds_stencil_func[0] = func;
        if (face & NVN_FACE_BACK) v->ds_stencil_func[1] = func;
        s_stats.sets++;
    }
    RS_END()
    NVN_VOID_RETURN();
}

void nvn_render_state_depth_stencil_set_stencil_op(void *object, int face, int fail, int zfail,
                                                    int zpass) {
    RS_BEGIN_WRITE(NVN_RS_DEPTH_STENCIL)
    if (v) {
        if (face & NVN_FACE_FRONT) {
            v->ds_stencil_op[0][0] = fail;
            v->ds_stencil_op[0][1] = zfail;
            v->ds_stencil_op[0][2] = zpass;
        }
        if (face & NVN_FACE_BACK) {
            v->ds_stencil_op[1][0] = fail;
            v->ds_stencil_op[1][1] = zfail;
            v->ds_stencil_op[1][2] = zpass;
        }
        s_stats.sets++;
    }
    RS_END()
    NVN_VOID_RETURN();
}

int nvn_render_state_depth_stencil_get_depth_test_enable(const void *object) {
    int value = 0;
    RS_BEGIN_READ(NVN_RS_DEPTH_STENCIL)
    value = tmp.ds_depth_test_enable;
    RS_END()
    s_stats.gets++;
    return value;
}

int nvn_render_state_depth_stencil_get_depth_write_enable(const void *object) {
    int value = 0;
    RS_BEGIN_READ(NVN_RS_DEPTH_STENCIL)
    value = tmp.ds_depth_write_enable;
    RS_END()
    s_stats.gets++;
    return value;
}

int nvn_render_state_depth_stencil_get_depth_func(const void *object) {
    int value = NVN_DEPTH_FUNC_LESS;
    RS_BEGIN_READ(NVN_RS_DEPTH_STENCIL)
    value = tmp.ds_depth_func;
    RS_END()
    s_stats.gets++;
    return value;
}

int nvn_render_state_depth_stencil_get_stencil_test_enable(const void *object) {
    int value = 0;
    RS_BEGIN_READ(NVN_RS_DEPTH_STENCIL)
    value = tmp.ds_stencil_test_enable;
    RS_END()
    s_stats.gets++;
    return value;
}

int nvn_render_state_depth_stencil_get_stencil_func(const void *object, int face) {
    int value = NVN_STENCIL_FUNC_ALWAYS;
    RS_BEGIN_READ(NVN_RS_DEPTH_STENCIL)
    value = tmp.ds_stencil_func[(face & NVN_FACE_BACK) ? 1 : 0];
    RS_END()
    s_stats.gets++;
    return value;
}

void nvn_render_state_depth_stencil_get_stencil_op(const void *object, int face, int *fail,
                                                    int *zfail, int *zpass) {
    int f = NVN_STENCIL_OP_KEEP;
    int zf = NVN_STENCIL_OP_KEEP;
    int zp = NVN_STENCIL_OP_KEEP;
    RS_BEGIN_READ(NVN_RS_DEPTH_STENCIL)
    {
        int index = (face & NVN_FACE_BACK) ? 1 : 0;
        f = tmp.ds_stencil_op[index][0];
        zf = tmp.ds_stencil_op[index][1];
        zp = tmp.ds_stencil_op[index][2];
    }
    RS_END()
    if (fail) *fail = f;
    if (zfail) *zfail = zf;
    if (zpass) *zpass = zp;
    s_stats.gets++;
    NVN_VOID_RETURN();
}

/* ------------------------------------------------------------------ */
/* The vertex attribute state                                          */
/* ------------------------------------------------------------------ */

void nvn_render_state_vertex_attrib_set_defaults(void *object) {
    RS_BEGIN_WRITE(NVN_RS_VERTEX_ATTRIB)
    if (v) {
        apply_defaults(v, NVN_RS_VERTEX_ATTRIB);
        s_stats.defaults_applied++;
    }
    RS_END()
    NVN_VOID_RETURN();
}

void nvn_render_state_vertex_attrib_set_format(void *object, int format, int64_t offset) {
    RS_BEGIN_WRITE(NVN_RS_VERTEX_ATTRIB)
    if (v) {
        v->va_format = format;
        v->va_offset = offset;
        s_stats.sets++;
    }
    RS_END()
    NVN_VOID_RETURN();
}

void nvn_render_state_vertex_attrib_set_stream_index(void *object, int index) {
    RS_BEGIN_WRITE(NVN_RS_VERTEX_ATTRIB)
    if (v) {
        v->va_stream_index = index;
        s_stats.sets++;
    }
    RS_END()
    NVN_VOID_RETURN();
}

void nvn_render_state_vertex_attrib_get_format(const void *object, int *format, int64_t *offset) {
    int f = 0;
    int64_t o = 0;
    RS_BEGIN_READ(NVN_RS_VERTEX_ATTRIB)
    f = tmp.va_format;
    o = tmp.va_offset;
    RS_END()
    if (format) *format = f;
    if (offset) *offset = o;
    s_stats.gets++;
    NVN_VOID_RETURN();
}

int nvn_render_state_vertex_attrib_get_stream_index(const void *object) {
    int value = 0;
    RS_BEGIN_READ(NVN_RS_VERTEX_ATTRIB)
    value = tmp.va_stream_index;
    RS_END()
    s_stats.gets++;
    return value;
}

/* ------------------------------------------------------------------ */
/* The vertex stream state                                             */
/* ------------------------------------------------------------------ */

void nvn_render_state_vertex_stream_set_defaults(void *object) {
    RS_BEGIN_WRITE(NVN_RS_VERTEX_STREAM)
    if (v) {
        apply_defaults(v, NVN_RS_VERTEX_STREAM);
        s_stats.defaults_applied++;
    }
    RS_END()
    NVN_VOID_RETURN();
}

void nvn_render_state_vertex_stream_set_stride(void *object, int64_t stride) {
    RS_BEGIN_WRITE(NVN_RS_VERTEX_STREAM)
    if (v) {
        v->vs_stride = stride;
        s_stats.sets++;
    }
    RS_END()
    NVN_VOID_RETURN();
}

void nvn_render_state_vertex_stream_set_divisor(void *object, int divisor) {
    RS_BEGIN_WRITE(NVN_RS_VERTEX_STREAM)
    if (v) {
        v->vs_divisor = divisor;
        s_stats.sets++;
    }
    RS_END()
    NVN_VOID_RETURN();
}

int64_t nvn_render_state_vertex_stream_get_stride(const void *object) {
    int64_t value = 0;
    RS_BEGIN_READ(NVN_RS_VERTEX_STREAM)
    value = tmp.vs_stride;
    RS_END()
    s_stats.gets++;
    return value;
}

int nvn_render_state_vertex_stream_get_divisor(const void *object) {
    int value = 0;
    RS_BEGIN_READ(NVN_RS_VERTEX_STREAM)
    value = tmp.vs_divisor;
    RS_END()
    s_stats.gets++;
    return value;
}

/* ------------------------------------------------------------------ */
/* The names                                                           */
/* ------------------------------------------------------------------ */

static const nvn_render_state_family_entry s_render_state_family[] = {
    { "nvnColorStateSetDefaults", (void *)(uintptr_t)nvn_render_state_color_set_defaults },
    { "nvnColorStateSetBlendEnable", (void *)(uintptr_t)nvn_render_state_color_set_blend_enable },
    { "nvnColorStateSetLogicOp", (void *)(uintptr_t)nvn_render_state_color_set_logic_op },
    { "nvnColorStateSetAlphaTest", (void *)(uintptr_t)nvn_render_state_color_set_alpha_test },
    { "nvnColorStateGetBlendEnable", (void *)(uintptr_t)nvn_render_state_color_get_blend_enable },
    { "nvnColorStateGetLogicOp", (void *)(uintptr_t)nvn_render_state_color_get_logic_op },
    { "nvnColorStateGetAlphaTest", (void *)(uintptr_t)nvn_render_state_color_get_alpha_test },
    { "nvnChannelMaskStateSetDefaults", (void *)(uintptr_t)nvn_render_state_channel_mask_set_defaults },
    { "nvnChannelMaskStateSetChannelMask",
      (void *)(uintptr_t)nvn_render_state_channel_mask_set_channel_mask },
    { "nvnChannelMaskStateGetChannelMask",
      (void *)(uintptr_t)nvn_render_state_channel_mask_get_channel_mask },
    { "nvnMultisampleStateSetDefaults", (void *)(uintptr_t)nvn_render_state_multisample_set_defaults },
    { "nvnMultisampleStateSetMultisampleEnable",
      (void *)(uintptr_t)nvn_render_state_multisample_set_multisample_enable },
    { "nvnMultisampleStateSetSamples", (void *)(uintptr_t)nvn_render_state_multisample_set_samples },
    { "nvnMultisampleStateSetAlphaToCoverageEnable",
      (void *)(uintptr_t)nvn_render_state_multisample_set_alpha_to_coverage_enable },
    { "nvnMultisampleStateSetAlphaToCoverageDither",
      (void *)(uintptr_t)nvn_render_state_multisample_set_alpha_to_coverage_dither },
    { "nvnMultisampleStateGetMultisampleEnable",
      (void *)(uintptr_t)nvn_render_state_multisample_get_multisample_enable },
    { "nvnMultisampleStateGetSamples", (void *)(uintptr_t)nvn_render_state_multisample_get_samples },
    { "nvnMultisampleStateGetAlphaToCoverageEnable",
      (void *)(uintptr_t)nvn_render_state_multisample_get_alpha_to_coverage_enable },
    { "nvnMultisampleStateGetAlphaToCoverageDither",
      (void *)(uintptr_t)nvn_render_state_multisample_get_alpha_to_coverage_dither },
    { "nvnMultisampleStateSetRasterSamples",
      (void *)(uintptr_t)nvn_render_state_multisample_set_raster_samples },
    { "nvnMultisampleStateGetRasterSamples",
      (void *)(uintptr_t)nvn_render_state_multisample_get_raster_samples },
    { "nvnMultisampleStateSetCoverageModulationMode",
      (void *)(uintptr_t)nvn_render_state_multisample_set_coverage_modulation_mode },
    { "nvnMultisampleStateGetCoverageModulationMode",
      (void *)(uintptr_t)nvn_render_state_multisample_get_coverage_modulation_mode },
    { "nvnMultisampleStateSetCoverageToColorEnable",
      (void *)(uintptr_t)nvn_render_state_multisample_set_coverage_to_color_enable },
    { "nvnMultisampleStateGetCoverageToColorEnable",
      (void *)(uintptr_t)nvn_render_state_multisample_get_coverage_to_color_enable },
    { "nvnMultisampleStateSetCoverageToColorOutput",
      (void *)(uintptr_t)nvn_render_state_multisample_set_coverage_to_color_output },
    { "nvnMultisampleStateGetCoverageToColorOutput",
      (void *)(uintptr_t)nvn_render_state_multisample_get_coverage_to_color_output },
    { "nvnMultisampleStateSetSampleLocationsEnable",
      (void *)(uintptr_t)nvn_render_state_multisample_set_sample_locations_enable },
    { "nvnMultisampleStateGetSampleLocationsEnable",
      (void *)(uintptr_t)nvn_render_state_multisample_get_sample_locations_enable },
    { "nvnMultisampleStateGetSampleLocationsGrid",
      (void *)(uintptr_t)nvn_render_state_multisample_get_sample_locations_grid },
    { "nvnMultisampleStateSetSampleLocationsGridEnable",
      (void *)(uintptr_t)nvn_render_state_multisample_set_sample_locations_grid_enable },
    { "nvnMultisampleStateGetSampleLocationsGridEnable",
      (void *)(uintptr_t)nvn_render_state_multisample_get_sample_locations_grid_enable },
    { "nvnMultisampleStateSetSampleLocations",
      (void *)(uintptr_t)nvn_render_state_multisample_set_sample_locations },
    { "nvnPolygonStateSetDefaults", (void *)(uintptr_t)nvn_render_state_polygon_set_defaults },
    { "nvnPolygonStateSetCullFace", (void *)(uintptr_t)nvn_render_state_polygon_set_cull_face },
    { "nvnPolygonStateSetFrontFace", (void *)(uintptr_t)nvn_render_state_polygon_set_front_face },
    { "nvnPolygonStateSetPolygonMode", (void *)(uintptr_t)nvn_render_state_polygon_set_polygon_mode },
    { "nvnPolygonStateSetPolygonOffsetEnables",
      (void *)(uintptr_t)nvn_render_state_polygon_set_polygon_offset_enables },
    { "nvnPolygonStateGetCullFace", (void *)(uintptr_t)nvn_render_state_polygon_get_cull_face },
    { "nvnPolygonStateGetFrontFace", (void *)(uintptr_t)nvn_render_state_polygon_get_front_face },
    { "nvnPolygonStateGetPolygonMode", (void *)(uintptr_t)nvn_render_state_polygon_get_polygon_mode },
    { "nvnPolygonStateGetPolygonOffsetEnables",
      (void *)(uintptr_t)nvn_render_state_polygon_get_polygon_offset_enables },
    { "nvnDepthStencilStateSetDefaults", (void *)(uintptr_t)nvn_render_state_depth_stencil_set_defaults },
    { "nvnDepthStencilStateSetDepthTestEnable",
      (void *)(uintptr_t)nvn_render_state_depth_stencil_set_depth_test_enable },
    { "nvnDepthStencilStateSetDepthWriteEnable",
      (void *)(uintptr_t)nvn_render_state_depth_stencil_set_depth_write_enable },
    { "nvnDepthStencilStateSetDepthFunc", (void *)(uintptr_t)nvn_render_state_depth_stencil_set_depth_func },
    { "nvnDepthStencilStateSetStencilTestEnable",
      (void *)(uintptr_t)nvn_render_state_depth_stencil_set_stencil_test_enable },
    { "nvnDepthStencilStateSetStencilFunc",
      (void *)(uintptr_t)nvn_render_state_depth_stencil_set_stencil_func },
    { "nvnDepthStencilStateSetStencilOp", (void *)(uintptr_t)nvn_render_state_depth_stencil_set_stencil_op },
    { "nvnDepthStencilStateGetDepthTestEnable",
      (void *)(uintptr_t)nvn_render_state_depth_stencil_get_depth_test_enable },
    { "nvnDepthStencilStateGetDepthWriteEnable",
      (void *)(uintptr_t)nvn_render_state_depth_stencil_get_depth_write_enable },
    { "nvnDepthStencilStateGetDepthFunc", (void *)(uintptr_t)nvn_render_state_depth_stencil_get_depth_func },
    { "nvnDepthStencilStateGetStencilTestEnable",
      (void *)(uintptr_t)nvn_render_state_depth_stencil_get_stencil_test_enable },
    { "nvnDepthStencilStateGetStencilFunc",
      (void *)(uintptr_t)nvn_render_state_depth_stencil_get_stencil_func },
    { "nvnDepthStencilStateGetStencilOp", (void *)(uintptr_t)nvn_render_state_depth_stencil_get_stencil_op },
    { "nvnVertexAttribStateSetDefaults", (void *)(uintptr_t)nvn_render_state_vertex_attrib_set_defaults },
    { "nvnVertexAttribStateSetFormat", (void *)(uintptr_t)nvn_render_state_vertex_attrib_set_format },
    { "nvnVertexAttribStateSetStreamIndex",
      (void *)(uintptr_t)nvn_render_state_vertex_attrib_set_stream_index },
    { "nvnVertexAttribStateGetFormat", (void *)(uintptr_t)nvn_render_state_vertex_attrib_get_format },
    { "nvnVertexAttribStateGetStreamIndex",
      (void *)(uintptr_t)nvn_render_state_vertex_attrib_get_stream_index },
    { "nvnVertexStreamStateSetDefaults", (void *)(uintptr_t)nvn_render_state_vertex_stream_set_defaults },
    { "nvnVertexStreamStateSetStride", (void *)(uintptr_t)nvn_render_state_vertex_stream_set_stride },
    { "nvnVertexStreamStateSetDivisor", (void *)(uintptr_t)nvn_render_state_vertex_stream_set_divisor },
    { "nvnVertexStreamStateGetStride", (void *)(uintptr_t)nvn_render_state_vertex_stream_get_stride },
    { "nvnVertexStreamStateGetDivisor", (void *)(uintptr_t)nvn_render_state_vertex_stream_get_divisor },
};

size_t nvn_render_state_family_count(void) {
    return sizeof(s_render_state_family) / sizeof(s_render_state_family[0]);
}

const nvn_render_state_family_entry *nvn_render_state_family_entry_at(size_t index) {
    if (index >= nvn_render_state_family_count()) return NULL;
    return &s_render_state_family[index];
}

void nvn_render_state_get_stats(nvn_render_state_stats *out) {
    size_t i;
    if (!out) return;
    pthread_mutex_lock(&s_rs_lock);
    *out = s_stats;
    out->live_states = 0;
    for (i = 0; i < NVN_RS_MAX_STATES; i++) {
        if (s_states[i].in_use) out->live_states++;
    }
    pthread_mutex_unlock(&s_rs_lock);
}
