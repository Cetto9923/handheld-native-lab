/* The NVN command buffer family of the FFRS RG477M native runtime.
 *
 * An NVN command buffer is a recorder in the real SDK as well: the guest
 * records the commands it wants into the buffer's own memory, and the device
 * executes them later, when the buffer is submitted. Recording the calls
 * faithfully is therefore the whole of this layer; the replay comes later.
 *
 * The format of the records is not this runtime's own. The command stream of
 * the game this port runs is known, because the reference tree that runs this
 * same game carries it (MIT, .local/refs/nxnative, revision 7a029f66):
 *
 *     .local/refs/nxnative/dm/gfx/nvn/nvn_obj.h
 *         typedef struct { uint32_t op, size; } op_hdr;
 *         enum { OP_END, OP_JUMP, OP_BEGIN, OP_CALL, ... }   49 opcodes
 *         p_set_rt / p_clear_color / p_clear_ds / p_viewports / p_scissor /
 *         p_bind_buf / p_bind_vbuf / p_bind_handles / p_bind_tex1 / ...
 *
 * The opcode numbering below is that tree's numbering, unchanged: the enum
 * NVN_CB_OP_* is the reference's OP_* one for one, in the same order, so a
 * record written here can be read with the reference's own structures.
 * The payload structures below are the reference's p_* structures, field for
 * field and width for width. The only difference is that a field the
 * reference fills with a pointer to one of its own host side objects holds
 * here a pointer to an immutable copy of the guest's object, because the
 * layout of an NVN object is a property of the Nintendo SDK, which this
 * runtime does not have.
 *
 * As with the queue, the window, the memory pool and the two state families,
 * the state lives on the host and is keyed by the guest's command buffer
 * address. Nothing is written into the guest's command buffer object, because
 * the layout of an NVNcommandBuffer is a property of the Nintendo SDK, which
 * this runtime does not have.
 */
#ifndef FFRS_NVN_COMMAND_BUFFER_H
#define FFRS_NVN_COMMAND_BUFFER_H

#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* The record format of the reference tree                             */
/* ------------------------------------------------------------------ */

/* The header of every record. The size counts the header itself and is
 * aligned to eight bytes, as the reference writes it. */
typedef struct nvn_cb_op_hdr {
    uint32_t op;
    uint32_t size;
} nvn_cb_op_hdr;

/* The opcodes of the reference tree, unchanged and in its order. The two
 * that the reference itself does not record are marked. */
enum nvn_cb_opcode {
    NVN_CB_OP_END = 0,          /* the end of a recording */
    NVN_CB_OP_JUMP = 1,         /* a switch to another command memory segment */
    NVN_CB_OP_BEGIN = 2,        /* not recorded by the reference either */
    NVN_CB_OP_CALL = 3,         /* the call of another recording */
    NVN_CB_OP_SIGNAL_EVENT = 4,
    NVN_CB_OP_REPORT_COUNTER = 5,
    NVN_CB_OP_COPY_B2B = 6,
    NVN_CB_OP_UPDATE_UB = 7,
    NVN_CB_OP_SET_RT = 8,
    NVN_CB_OP_CLEAR_COLOR = 9,
    NVN_CB_OP_CLEAR_DS = 10,
    NVN_CB_OP_VIEWPORTS = 11,
    NVN_CB_OP_SCISSOR = 12,
    NVN_CB_OP_DEPTH_RANGE = 13,
    NVN_CB_OP_BIND_PROGRAM = 14,
    NVN_CB_OP_BIND_STATE = 15,
    NVN_CB_OP_BIND_VATTR = 16,
    NVN_CB_OP_BIND_VSTREAM = 17,
    NVN_CB_OP_BIND_VBUF = 18,
    NVN_CB_OP_BIND_UBO = 19,
    NVN_CB_OP_BIND_SSBO = 20,
    NVN_CB_OP_BIND_TEX = 21,
    NVN_CB_OP_BIND_SAMP = 22,
    NVN_CB_OP_STENCIL = 23,
    NVN_CB_OP_POLY_OFFSET = 24,
    NVN_CB_OP_DRAW = 25,
    NVN_CB_OP_DRAW_IDX = 26,
    NVN_CB_OP_DISPATCH = 27,
    NVN_CB_OP_BARRIER = 28,
    NVN_CB_OP_SET_TEX_POOL = 29,
    NVN_CB_OP_SET_SAMP_POOL = 30,
    NVN_CB_OP_COPY_B2T = 31,
    NVN_CB_OP_COPY_STRIDE = 32,
    NVN_CB_OP_BLEND_COLOR = 33,
    NVN_CB_OP_COPY_T2B = 34,
    NVN_CB_OP_BIND_IMG = 35,
    NVN_CB_OP_COPY_T2T = 36,
    NVN_CB_OP_RESET_COUNTER = 37,
    NVN_CB_OP_CLEAR_TEX = 38,
    NVN_CB_OP_CLEAR_BUF = 39,
    NVN_CB_OP_DEPTH_CLAMP = 40,
    NVN_CB_OP_RENDER_COND = 41,
    NVN_CB_OP_FENCE_SYNC = 42,
    NVN_CB_OP_TF_BIND = 43,
    NVN_CB_OP_TF_STATE = 44,
    NVN_CB_OP_RAST_DISCARD = 45,
    NVN_CB_OP_VP_SWIZZLE = 46,
    NVN_CB_OP_BIND_TEX1 = 47,
    NVN_CB_OP_PRIM_RESTART = 48,
    NVN_CB_OP_COUNT = 49
};

/* The name of an opcode, for the log and for the tests. */
const char *nvn_command_buffer_opcode_name(uint32_t op);

/* The payloads. They are the reference's p_* structures, field for field.
 *
 * A field the reference fills with a pointer to one of its own host side
 * objects (a texture, a program, a descriptor pool) holds here a pointer to
 * an immutable copy of the guest's object. The copy is made at the moment of
 * the call, so the record no longer depends on the guest's object: the guest
 * may finalize it, free it or write over it before the command is run. The
 * bytes of the copy are the guest's own bytes; what they mean is the
 * replay's business. */
typedef struct nvn_cb_view {
    uint8_t set_levels : 1, set_layers : 1, set_format : 1, set_target : 1,
            set_swizzle : 1, set_dsmode : 1, dsmode : 2;
    uint8_t target, base_level, nlevels;
    uint16_t format, min_layer, nlayers;
    uint8_t swz[4];
    uint16_t reserved;
} nvn_cb_view;

typedef struct nvn_cb_vs_st {
    uint32_t stride, divisor;
} nvn_cb_vs_st;

typedef struct nvn_cb_buffer_range {
    uint64_t address;
    uint64_t size;
} nvn_cb_buffer_range;

typedef struct nvn_cb_copy_region {
    int32_t xoffset, yoffset, zoffset, width, height, depth;
} nvn_cb_copy_region;

typedef struct nvn_cb_p_set_rt {
    int32_t n;
    const void *color[8];
    nvn_cb_view cview[8];
    uint8_t has_cview[8];
    const void *depth;
    nvn_cb_view dview;
    uint8_t has_dview;
} nvn_cb_p_set_rt;

typedef struct nvn_cb_p_clear_color {
    int32_t index, mask;
    float rgba[4];
} nvn_cb_p_clear_color;

typedef struct nvn_cb_p_render_cond {
    int32_t mode, pad;
    uint64_t addr;
} nvn_cb_p_render_cond;

typedef struct nvn_cb_p_clear_ds {
    float depth;
    int32_t depth_mask, stencil, stencil_mask;
} nvn_cb_p_clear_ds;

typedef struct nvn_cb_p_viewports {
    int32_t first, n;
    float v[16][6];
} nvn_cb_p_viewports;

typedef struct nvn_cb_p_scissor {
    int32_t x, y, w, h;
} nvn_cb_p_scissor;

typedef struct nvn_cb_p_depth_range {
    float n, f;
} nvn_cb_p_depth_range;

typedef struct nvn_cb_p_prim_restart {
    uint32_t on, index;
} nvn_cb_p_prim_restart;

typedef struct nvn_cb_p_depth_clamp {
    int32_t on;
} nvn_cb_p_depth_clamp;

typedef struct nvn_cb_p_vp_swizzle {
    int32_t first, n;
    uint8_t sw[16][4];
} nvn_cb_p_vp_swizzle;

typedef struct nvn_cb_p_bind_program {
    const void *prog;
    int32_t stages;
} nvn_cb_p_bind_program;

typedef struct nvn_cb_p_bind_state {
    int32_t kind;
    uint8_t bytes[0x18];
} nvn_cb_p_bind_state;

typedef struct nvn_cb_p_bind_vattr {
    int32_t first, n;
    uint32_t st[16];
} nvn_cb_p_bind_vattr;

typedef struct nvn_cb_p_bind_vstream {
    int32_t first, n;
    nvn_cb_vs_st st[16];
} nvn_cb_p_bind_vstream;

typedef struct nvn_cb_p_bind_vbuf {
    int32_t first, n;
    nvn_cb_buffer_range r[16];
} nvn_cb_p_bind_vbuf;

typedef struct nvn_cb_p_bind_buf {
    int32_t stage, index;
    uint64_t addr, size;
} nvn_cb_p_bind_buf;

typedef struct nvn_cb_p_bind_handles {
    int32_t stage, first, n, defer;
    uint64_t src;
    uint64_t h[32];
} nvn_cb_p_bind_handles;

typedef struct nvn_cb_p_bind_tex1 {
    int32_t stage, slot;
    uint64_t h;
    int32_t tbase, sbase;
} nvn_cb_p_bind_tex1;

typedef struct nvn_cb_p_stencil {
    int32_t face, value_mask, mask, ref, which;
} nvn_cb_p_stencil;

typedef struct nvn_cb_p_poly_offset {
    float factor, units, clamp;
} nvn_cb_p_poly_offset;

typedef struct nvn_cb_p_draw {
    int32_t prim, first, count, instances, base_instance;
    uint64_t ind;
    uint64_t mdi_cnt;
    int32_t mdi_i, pad;
} nvn_cb_p_draw;

typedef struct nvn_cb_p_draw_idx {
    int32_t prim, itype, count, base_vertex, base_instance, instances;
    uint64_t ibuf, ind;
    uint64_t mdi_cnt;
    int32_t mdi_i, pad;
} nvn_cb_p_draw_idx;

typedef struct nvn_cb_p_dispatch {
    int32_t x, y, z;
    uint64_t caller, ind;
} nvn_cb_p_dispatch;

typedef struct nvn_cb_p_set_pool {
    const void *pool;
} nvn_cb_p_set_pool;

typedef struct nvn_cb_p_copy_b2t {
    uint64_t src;
    const void *tex;
    nvn_cb_view view;
    uint8_t has_view;
    nvn_cb_copy_region r;
    int32_t flags;
} nvn_cb_p_copy_b2t;

typedef struct nvn_cb_p_copy_t2t {
    const void *src, *dst;
    nvn_cb_view sv, dv;
    uint8_t has_sv, has_dv;
    nvn_cb_copy_region sr, dr;
    int32_t flags;
} nvn_cb_p_copy_t2t;

typedef struct nvn_cb_p_clear_tex {
    const void *tex;
    nvn_cb_view view;
    uint8_t has_view, is_uint;
    nvn_cb_copy_region r;
    int32_t mask;
    union {
        float f[4];
        uint32_t u[4];
    } c;
} nvn_cb_p_clear_tex;

typedef struct nvn_cb_p_copy_stride {
    int64_t stride;
    int32_t image;
} nvn_cb_p_copy_stride;

typedef struct nvn_cb_p_clear_buf {
    uint64_t addr, size;
    uint32_t value, pad;
} nvn_cb_p_clear_buf;

typedef struct nvn_cb_p_blend_color {
    float rgba[4];
} nvn_cb_p_blend_color;

typedef struct nvn_cb_p_tf_bind {
    int32_t index, pad;
    uint64_t addr, size;
} nvn_cb_p_tf_bind;

typedef struct nvn_cb_p_tf_state {
    int32_t what, pad;
    uint64_t ctl;
} nvn_cb_p_tf_state;

typedef struct nvn_cb_p_rast_discard {
    int32_t on;
} nvn_cb_p_rast_discard;

/* ------------------------------------------------------------------ */
/* The family table                                                    */
/* ------------------------------------------------------------------ */

/* Whether the signature of an entry point is known.
 *
 * A signature that is in the vendored NVN header is confirmed: the call is
 * taken with the header's types and recorded in the reference's format. A
 * signature that is not in the header is not guessed: the call is taken as
 * plain words, nothing is dereferenced, and the call is recorded in the
 * diagnostic channel only. The guest calls those entry points already, so
 * they must not trap, but they must not be reported as implemented either. */
typedef enum nvn_command_buffer_entry_status {
    NVN_CB_ENTRY_SIGNATURE_CONFIRMED = 0,
    NVN_CB_ENTRY_DIAGNOSTIC_ONLY = 1
} nvn_command_buffer_entry_status;

typedef struct nvn_command_buffer_family_entry {
    const char *name;
    void *fn;
    nvn_command_buffer_entry_status status;
} nvn_command_buffer_family_entry;

size_t nvn_command_buffer_family_count(void);
const nvn_command_buffer_family_entry *nvn_command_buffer_family_entry_at(size_t index);

/* ------------------------------------------------------------------ */
/* Reading the records                                                 */
/* ------------------------------------------------------------------ */

/* One record of the current recording, seen from the outside.
 *
 * The op is the opcode of the reference's numbering and the size is the size
 * of the whole record, header included. The payload points into the command
 * memory of the recording, after the header.
 *
 * The segment is the segment of the command memory the record is in: the
 * command memory the guest handed over is the segment zero, and the segments
 * the host lent are the ones after it. A record therefore knows which memory
 * it is in, and the guest's memory is never written to. */
typedef struct nvn_recorded_command {
    uint32_t op;
    uint32_t size;
    const uint8_t *payload;
    uint32_t segment;
} nvn_recorded_command;

/* The records of the recording that is running right now. The list belongs
 * to the runtime and stays valid until the next command is recorded into the
 * same buffer, so a caller reads it before it records more.
 * Returns 1 when the buffer has records. */
int nvn_command_buffer_get_records(void *buffer, const nvn_recorded_command **out, size_t *count);

/* A command of the diagnostic channel: a call that the reference's format
 * has no record for.
 *
 * The arguments are the words the guest passed, as they came. An argument
 * that is a pointer whose size is not known is never dereferenced: it is
 * named in the unresolved mask, so that whoever reads this record knows that
 * the pointer was not followed and that the data behind it is not in the
 * record. */
typedef struct nvn_diagnostic_command {
    const char *name;
    uint32_t op_index;     /* the index of the entry point in the family table */
    uint32_t arg_count;
    uint64_t args[8];
    uint32_t unresolved_count;
    uint8_t unresolved[8];  /* which arguments were pointers that were not followed */
} nvn_diagnostic_command;

int nvn_command_buffer_get_diagnostics(void *buffer, const nvn_diagnostic_command **out,
                                       size_t *count);

/* ------------------------------------------------------------------ */
/* The command package                                                 */
/* ------------------------------------------------------------------ */

/* One segment of the command memory of a package.
 *
 * The records of a package are in the order they were made. The first
 * segment is the command memory the guest handed over; the ones after it are
 * the segments of the host's own memory that the host lent when the guest
 * could not add enough. */
typedef struct nvn_command_package_segment {
    size_t offset;        /* where the records of the segment start in the package */
    size_t bytes;         /* how many bytes they hold */
    size_t record_count;  /* how many records they are */
    int host;             /* 1 = the host lent this memory */
} nvn_command_package_segment;

/* An immutable package of commands.
 *
 * The handle of a recording is the address of its first record in the host
 * side command memory of that recording. The package is that memory: it is
 * written once, at the end of the recording, and never written again. A
 * recording that is made later into the same buffer, a finalization and a
 * reuse of the guest's buffer address all leave the package of an earlier
 * handle untouched.
 *
 * The records of a recording that ran out of the guest's command memory are
 * in the segments after the first one, and the records of the segments are
 * copied into the package in the order they were made, so the package is one
 * piece of memory and a reader that walks it sees every record. */
typedef struct nvn_command_package {
    uint64_t handle;        /* the address of the first record */
    const uint8_t *records; /* the command memory of the recording */
    size_t bytes;           /* how many bytes it holds */
    size_t record_count;    /* how many records it holds */
    size_t segment_count;   /* how many segments the recording used */
    const nvn_command_package_segment *segments; /* the segments, in order */
} nvn_command_package;

/* The package a handle names. Returns 0 when the registry does not know the
 * handle; the handle is never dereferenced. */
int nvn_command_buffer_get_package(uint64_t handle, nvn_command_package *out);

/* ------------------------------------------------------------------ */
/* Statistics for the contract                                         */
/* ------------------------------------------------------------------ */

typedef struct nvn_command_buffer_stats {
    uint64_t calls;           /* calls into the family */
    uint64_t recorded;        /* commands that entered the command stream */
    uint64_t recorded_args;   /* argument words recorded in total */
    uint64_t dropped;         /* commands that were refused (overflow or failure) */
    uint64_t refused;         /* calls the runtime could not serve at all */
    uint64_t initialized;
    uint64_t finalized;
    uint64_t raw_calls;       /* calls to the entry points of unknown signature */
    uint64_t diagnostic_only; /* calls that were only diagnosed, never recorded */
    uint64_t honest_answers;  /* answers that are a stated approximation */
    uint64_t handles;         /* the command handles the runtime handed out */
    uint64_t outside_recording; /* commands recorded outside a recording window */
    uint64_t unmapped;        /* commands the reference's format has no record for */
    uint64_t void_recordings; /* recordings that ended without a handle */
    uint64_t overflow;        /* records that did not fit into the command memory */
    uint64_t copies_bytes;    /* bytes copied at call time */
    uint64_t unresolved;      /* pointers that were recorded without being followed */
    uint64_t copy_refused;    /* copies that were refused for being too large */
    uint64_t unknown_handles; /* handles that the registry did not know */
    uint64_t callback_calls;  /* memory callbacks invoked through the guest boundary */
    uint64_t callback_failures; /* memory callbacks that could not be made */
    uint64_t callback_unavailable; /* memory callbacks refused: no boundary is registered */
    uint64_t callback_absent;  /* memory callbacks that were not there: the guest set none */
    uint64_t callback_refused_thread; /* refused: the caller is not the guest's own thread */
    uint64_t callback_refused_pointer; /* refused: the guest's thread pointer is not bound */
    uint64_t callback_refused_address; /* refused: the address is not in the module's text */
    uint64_t host_segments;   /* segments of the host's memory the recordings used */
    uint64_t host_segments_released; /* segments the guest gave back */
    uint64_t host_segment_refused; /* requests for a segment that the bound refused */
    uint64_t host_segment_bytes; /* the bytes of the host's memory the recordings hold */
    uint64_t host_segment_bound; /* the bound of the host's lent memory, in bytes */
    const char *void_reason; /* why the last recording could not be completed */
    uint64_t packages;        /* the immutable packages the registry holds */
    uint64_t package_bytes;   /* the bytes the packages hold */
    int live_buffers;
    int records;              /* the records the runtime holds right now */
} nvn_command_buffer_stats;

void nvn_command_buffer_get_stats(nvn_command_buffer_stats *out);

/* ------------------------------------------------------------------ */
/* The guest thread boundary                                            */
/* ------------------------------------------------------------------ */

/* The call of the guest from the host, as the guest thread boundary does it.
 *
 * The boundary component registers its call here when it comes up. This layer
 * does not link the boundary, so a build that has no boundary still links:
 * the memory callback of the guest is then refused, said by name and counted,
 * instead of being called. The guest's own callback is never called
 * directly, and no pointer to it is left behind.
 *
 * The hook is called with the address of the guest's callback, the register
 * values the guest is called with (the command buffer, the event, the size
 * the guest is asked for and the callback data), and the register values it
 * comes back with. It returns one of the values below. */
typedef int (*nvn_command_buffer_guest_call)(void *entry, const uint64_t *x, uint64_t *out_x);

/* What the hook of the guest thread boundary answers.
 *
 * The three refusals are named, because each of them is a different thing
 * that is wrong: the caller is not the guest's own thread, the guest's thread
 * pointer is not bound on this thread, or the address the guest handed over
 * is not inside the text of the guest's module. A pointer the guest made up
 * is never called. */
enum nvn_command_buffer_guest_call_result {
    NVN_CB_GUEST_CALL_OK = 1,               /* the guest was called */
    NVN_CB_GUEST_CALL_REFUSED = 0,          /* refused, for a reason of its own */
    NVN_CB_GUEST_CALL_WRONG_THREAD = -1,    /* the caller is not the guest's own thread */
    NVN_CB_GUEST_CALL_NO_THREAD_POINTER = -2, /* the guest's thread pointer is not bound */
    NVN_CB_GUEST_CALL_BAD_ADDRESS = -3      /* the address is not inside the module's text */
};

/* Register the call of the guest thread boundary. Passing NULL takes it away
 * again, which is what a build without the boundary has. */
void nvn_command_buffer_set_guest_call(nvn_command_buffer_guest_call call);

/* ------------------------------------------------------------------ */
/* The recorder core. The generated wrappers use these.                */
/* ------------------------------------------------------------------ */

void nvn_command_buffer_record_unmapped(void *buffer, const char *name, const uint64_t *args,
                                        uint32_t arg_count, const uint8_t *unresolved);
void nvn_command_buffer_record_diagnostic(void *buffer, const char *name, const uint64_t *args,
                                          uint32_t arg_count);
uint64_t nvn_command_buffer_float_bits(float value);
void nvn_command_buffer_set_result(uint64_t value);

int nvn_command_buffer_initialize(void *buffer);
void nvn_command_buffer_finalize(void *buffer);
void nvn_command_buffer_begin_recording(void *buffer);
uint64_t nvn_command_buffer_end_recording(void *buffer);
void nvn_command_buffer_add_command_memory(void *buffer, uint64_t size);
void nvn_command_buffer_add_control_memory(void *buffer, uint64_t size);
void nvn_command_buffer_set_copy_row_stride(void *buffer, int64_t stride);
void nvn_command_buffer_set_copy_image_stride(void *buffer, int64_t stride);
void nvn_command_buffer_set_memory_callback(void *buffer, uint64_t callback);
void nvn_command_buffer_set_memory_callback_data(void *buffer, uint64_t data);

uint64_t nvn_command_buffer_get_command_memory_size(const void *buffer);
uint64_t nvn_command_buffer_get_command_memory_free(const void *buffer);
uint64_t nvn_command_buffer_get_command_memory_used(const void *buffer);
uint64_t nvn_command_buffer_get_control_memory_size(const void *buffer);
uint64_t nvn_command_buffer_get_control_memory_free(const void *buffer);
int64_t nvn_command_buffer_get_copy_row_stride(const void *buffer);
int64_t nvn_command_buffer_get_copy_image_stride(const void *buffer);
uint64_t nvn_command_buffer_get_memory_callback(const void *buffer);
uint64_t nvn_command_buffer_get_memory_callback_data(const void *buffer);
int nvn_command_buffer_is_recording(const void *buffer);

/* Say that the guest asked how much of its own memory it has already used.
 *
 * The command memory of this runtime is the host's own, so the number of the
 * bytes that are used is a measurement and comes from
 * nvn_command_buffer_get_command_memory_used. The control memory is the
 * guest's own: the guest writes into it itself, so the runtime cannot see
 * how much of it is written, the answer is zero, and this says so once per
 * buffer, so that the log shows the limit instead of hiding it. */
void nvn_command_buffer_report_used_is_unknown(const void *buffer, const char *what);

/* ------------------------------------------------------------------ */
/* The recorders, one per command the reference's format has            */
/* ------------------------------------------------------------------ */

/* The state of a recording, and the copies that go with it.
 *
 * A size that is passed along is the size of the object the guest handed
 * over, as the vendored header declares it. The runtime does not have the
 * SDK's header at hand in this file, so the generated wrappers pass the size
 * that the generator read out of the header. */
void nvn_command_buffer_rec_set_render_targets(void *buffer, int count, const void *const *colors,
                                                size_t texture_size, const void *const *views,
                                                size_t view_size, const void *depth,
                                                const void *depth_view);
void nvn_command_buffer_rec_clear_color(void *buffer, int index, const void *rgba, int mask,
                                         int is_integer);
void nvn_command_buffer_rec_clear_depth_stencil(void *buffer, float depth, int depth_mask,
                                                 int stencil, int stencil_mask);
void nvn_command_buffer_rec_set_viewports(void *buffer, int first, int count, const float *values);
void nvn_command_buffer_rec_set_viewport(void *buffer, int x, int y, int w, int h);
void nvn_command_buffer_rec_set_scissors(void *buffer, int first, int count, const int *values);
void nvn_command_buffer_rec_set_scissor(void *buffer, int x, int y, int w, int h);
void nvn_command_buffer_rec_set_depth_ranges(void *buffer, int first, int count, const float *values);
void nvn_command_buffer_rec_set_depth_range(void *buffer, float near_value, float far_value);
void nvn_command_buffer_rec_bind_program(void *buffer, const void *program, size_t program_size,
                                          int stages);
void nvn_command_buffer_rec_bind_state(void *buffer, int kind, const void *state,
                                       size_t state_size);
void nvn_command_buffer_rec_bind_vertex_attrib_state(void *buffer, int count, const void *states,
                                                      size_t state_size);
void nvn_command_buffer_rec_bind_vertex_stream_state(void *buffer, int count, const void *states,
                                                      size_t state_size);
void nvn_command_buffer_rec_bind_vertex_buffers(void *buffer, int first, int count,
                                                 const void *ranges, size_t range_size);
void nvn_command_buffer_rec_bind_vertex_buffer(void *buffer, int first, uint64_t address,
                                                uint64_t size);
void nvn_command_buffer_rec_bind_uniform_buffer(void *buffer, int stage, int index, uint64_t address,
                                                 uint64_t size);
void nvn_command_buffer_rec_bind_uniform_buffers(void *buffer, int stage, int first, int count,
                                                  const void *ranges, size_t range_size);
void nvn_command_buffer_rec_bind_storage_buffer(void *buffer, int stage, int index, uint64_t address,
                                                 uint64_t size);
void nvn_command_buffer_rec_bind_storage_buffers(void *buffer, int stage, int first, int count,
                                                  const void *ranges, size_t range_size);
void nvn_command_buffer_rec_bind_texture(void *buffer, int stage, int slot, uint64_t handle);
void nvn_command_buffer_rec_bind_textures(void *buffer, int stage, int first, int count,
                                          const void *handles);
void nvn_command_buffer_rec_bind_separate_texture(void *buffer, int stage, int slot, uint64_t handle);
void nvn_command_buffer_rec_bind_separate_textures(void *buffer, int stage, int first, int count,
                                                   const void *handles);
void nvn_command_buffer_rec_bind_sampler(void *buffer, int stage, int slot, uint64_t handle);
void nvn_command_buffer_rec_bind_samplers(void *buffer, int stage, int first, int count,
                                          const void *handles);
void nvn_command_buffer_rec_bind_separate_sampler(void *buffer, int stage, int slot, uint64_t handle);
void nvn_command_buffer_rec_bind_separate_samplers(void *buffer, int stage, int first, int count,
                                                   const void *handles);
void nvn_command_buffer_rec_bind_image(void *buffer, int stage, int slot, uint64_t handle);
void nvn_command_buffer_rec_bind_images(void *buffer, int stage, int first, int count,
                                        const void *handles);
void nvn_command_buffer_rec_set_stencil(void *buffer, int face, int value_mask, int mask, int ref,
                                        int which);
void nvn_command_buffer_rec_set_polygon_offset(void *buffer, float factor, float units,
                                                float clamp_value);
void nvn_command_buffer_rec_draw(void *buffer, int primitive, int first, int count, int instances,
                                 int base_instance, uint64_t indirect);
void nvn_command_buffer_rec_draw_indexed(void *buffer, int primitive, int index_type, int count,
                                         int base_vertex, int base_instance, int instances,
                                         uint64_t index_buffer, uint64_t indirect);
void nvn_command_buffer_rec_dispatch(void *buffer, int x, int y, int z, uint64_t indirect);
void nvn_command_buffer_rec_barrier(void *buffer, int bits);
void nvn_command_buffer_rec_set_texture_pool(void *buffer, const void *pool);
void nvn_command_buffer_rec_set_sampler_pool(void *buffer, const void *pool);
void nvn_command_buffer_rec_set_blend_color(void *buffer, const float *rgba);
void nvn_command_buffer_rec_clear_buffer(void *buffer, uint64_t address, uint64_t size,
                                          uint32_t value);
void nvn_command_buffer_rec_clear_texture(void *buffer, const void *texture, const void *view,
                                           const void *region, const void *rgba, int mask,
                                           int is_integer);
void nvn_command_buffer_rec_copy_buffer_to_texture(void *buffer, uint64_t source,
                                                   const void *texture, const void *view,
                                                   const void *region, int flags);
void nvn_command_buffer_rec_copy_texture_to_buffer(void *buffer, const void *texture,
                                                   const void *view, const void *region,
                                                   uint64_t destination, int flags);
void nvn_command_buffer_rec_copy_texture_to_texture(void *buffer, const void *source,
                                                     const void *source_view,
                                                     const void *source_region,
                                                     const void *destination,
                                                     const void *destination_view,
                                                     const void *destination_region, int flags);
void nvn_command_buffer_rec_copy_buffer_to_buffer(void *buffer, uint64_t source,
                                                  uint64_t destination, uint64_t size);
void nvn_command_buffer_rec_update_uniform_buffer(void *buffer, uint64_t address, int64_t offset,
                                                   uint64_t size, const void *data);
void nvn_command_buffer_rec_report_counter(void *buffer, uint32_t type, uint64_t address);
void nvn_command_buffer_rec_reset_counter(void *buffer, uint32_t type);
void nvn_command_buffer_rec_report_value(void *buffer, uint32_t value, uint64_t address);
void nvn_command_buffer_rec_signal_event(void *buffer, const void *event, uint32_t mode,
                                          uint32_t value);
void nvn_command_buffer_rec_fence_sync(void *buffer, const void *sync);
void nvn_command_buffer_rec_set_render_enable(void *buffer, int enabled);
void nvn_command_buffer_rec_set_render_enable_conditional(void *buffer, int mode,
                                                           uint64_t address);
void nvn_command_buffer_rec_set_depth_clamp(void *buffer, int enabled);
void nvn_command_buffer_rec_set_rasterizer_discard(void *buffer, int enabled);
void nvn_command_buffer_rec_set_viewport_swizzles(void *buffer, int first, int count,
                                                   const void *swizzles);
void nvn_command_buffer_rec_set_primitive_restart(void *buffer, int enabled, int index);
void nvn_command_buffer_rec_bind_transform_feedback_buffer(void *buffer, int index,
                                                           uint64_t address, uint64_t size);
void nvn_command_buffer_rec_bind_transform_feedback_buffers(void *buffer, int first, int count,
                                                            const void *ranges);
void nvn_command_buffer_rec_transform_feedback_state(void *buffer, int what, uint64_t control);
void nvn_command_buffer_rec_set_copy_stride(void *buffer, int64_t stride, int image);

/* The call of other recordings. The handles are copied at the moment of the
 * call and every one of them is looked up in the registry; a handle the
 * registry does not know refuses the call and is written down. */
void nvn_command_buffer_rec_call_commands(void *buffer, int count, const uint64_t *handles);

#endif /* FFRS_NVN_COMMAND_BUFFER_H */
