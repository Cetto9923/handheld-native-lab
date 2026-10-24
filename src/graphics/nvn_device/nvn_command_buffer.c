/* The NVN command buffer recorder of the FFRS RG477M native runtime.
 *
 * An NVN command buffer is a recorder in the real SDK as well: the guest
 * records the commands it wants into the buffer's own memory, and the device
 * executes them later, when the buffer is submitted. So recording the calls
 * faithfully is the whole of this layer, and the replay that will read these
 * records does not exist yet.
 *
 * The records are written in the format of the game this port runs, which is
 * the format the reference tree that runs the same game carries (MIT,
 * .local/refs/nxnative, revision 7a029f66): a header of {op, size} and a
 * payload whose layout is the reference's p_* structure, field for field. The
 * opcode numbering is the reference's numbering, unchanged. The header, the
 * opcodes and the payloads are declared in nvn_command_buffer.h.
 *
 * The command memory is the host's own. The guest hands over command memory
 * with AddCommandMemory, and this runtime cannot use the guest's memory for
 * the records: the layout of the SDK's memory pool objects is a property of
 * the Nintendo SDK, which this runtime does not have. So the size the guest
 * hands over is the size of a host side block that the records go into, and
 * the handle of a recording is the address of its first record in that
 * block.
 *
 * The state lives on the host and is keyed by the guest's command buffer
 * address. Nothing is written into the guest's command buffer object, because
 * the layout of an NVNcommandBuffer is a property of the Nintendo SDK, which
 * this runtime does not have.
 */
#include "nvn_command_buffer.h"

#include "nvn_sampler_pool.h"
#include "nvn_texture_pool.h"

#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* The bounds of this layer                                            */
/* ------------------------------------------------------------------ */

/* How many command buffers the runtime holds at once. The guest keeps a
 * handful, so this is a safety net rather than a limit the guest reaches. */
#define NVN_CB_MAX_BUFFERS 32

/* How many records one recording holds. A frame of the guest records a few
 * hundred, and the bound keeps a runaway guest from eating the host's
 * memory. */
#define NVN_CB_MAX_RECORDS 8192

/* How many diagnostic records one buffer holds. */
#define NVN_CB_MAX_DIAGNOSTICS 512

/* The largest a single record may be. A command that does not fit is
 * refused; it is never truncated and reported as a success. */
#define NVN_CB_MAX_RECORD_BYTES 4096

/* The largest a single copy of guest data may be. The device object alone is
 * 12 KiB, so this is well above the objects the header declares. */
#define NVN_CB_MAX_COPY_BYTES 65536

/* How many packages the registry holds, and how many bytes they hold. The
 * packages are never given back: a handle has to stay valid for as long as
 * the guest may use it, and the guest may use it after the buffer was
 * finalized and its address reused. The bound is what keeps that honest. */
#define NVN_CB_MAX_PACKAGES 65536
#define NVN_CB_MAX_PACKAGE_BYTES (256u * 1024u * 1024u)

/* How much memory the host lends a recording at a time when the guest's own
 * command memory is not enough. The reference lends 256 KiB at a time. */
#define NVN_CB_HOSTSEG_BYTES (256u * 1024u)

/* How many such segments the host holds at once. This is the bound of the
 * memory the host lends: a recording that needs more is refused by name, and
 * the refusal says that a command does not fit. */
#define NVN_CB_MAX_HOSTSEGMENTS 64

/* How many times the guest's memory callback is asked for more command memory
 * before the recording continues in a segment of the host's own memory. The
 * reference asks twice and then lends a segment. */
#define NVN_CB_MAX_CALLBACK_TRIES 2

/* The view of a texture is the reference's own 16 byte structure. */
_Static_assert(sizeof(nvn_cb_view) == 16, "the view of the reference is 16 bytes");

/* ------------------------------------------------------------------ */
/* The state                                                           */
/* ------------------------------------------------------------------ */

/* An immutable copy of guest data that a record points at. The copies belong
 * to the recording that made them and are handed to the package with it, so
 * that a record never points at something the runtime may give back. */
typedef struct nvn_cb_copy {
    void *bytes;
    size_t size;
    struct nvn_cb_copy *next;
} nvn_cb_copy;

/* A segment of the command memory of a recording.
 *
 * The first segment of a recording is the command memory the guest handed
 * over. The ones after it are segments of the host's own memory, which the
 * host lends when the guest's callback cannot add enough: the records this
 * runtime writes are larger than the commands of a real device, so the budget
 * the guest reserved for the real device runs out early. The reference says
 * the same about this game: it is not an error state of the game but
 * something that has to be caught.
 *
 * The bytes of a segment the host lent are the host's. The guest's memory is
 * never written to. */
typedef struct nvn_cb_segment {
    uint8_t *mem;
    size_t capacity;
    size_t used;
    size_t record_count;
    int host;           /* 1 = the host lent this memory */
    int dead;           /* the guest gave the memory back */
    uint64_t guest_lo;  /* the guest's command memory the segment took over */
    uint64_t guest_hi;
    struct nvn_cb_segment *next;
} nvn_cb_segment;

/* The command buffer of the guest, as this runtime holds it. */
typedef struct {
    void *object;         /* the address the guest gave for its buffer */
    int in_use;
    int finalized;
    int recording;        /* a recording is running right now */
    int void_recording;   /* the recording cannot be completed */
    const char *void_reason;
    int used_reported;    /* the honest "used" answer was already logged */
    uint64_t sequence;    /* the order the slots were taken in */
    uint64_t command_memory_size;
    uint64_t control_memory_size;
    int64_t copy_row_stride;
    int64_t copy_image_stride;
    uint64_t memory_callback;
    uint64_t memory_callback_data;

    /* The numbering bases of the descriptor pools this buffer bound last.
     * The reference keeps them in the command buffer's own state, and the
     * single texture bind carries them, so that a handle in a record can be
     * read with the numbering of the pool it was registered in. */
    int32_t tex_base;
    int32_t samp_base;

    /* The command memory of the recording that is running. It is a list of
     * segments: the command memory the guest handed over is the first one,
     * and the segments the host lent are the ones after it. */
    nvn_cb_segment *segments;
    size_t segment_count;
    nvn_cb_copy *copies;

    /* The records of the recording, seen from the outside. */
    nvn_recorded_command *views;
    size_t view_count;
    size_t view_capacity;

    /* The calls that the reference's format has no record for. */
    nvn_diagnostic_command *diagnostics;
    size_t diagnostic_count;
    size_t diagnostic_capacity;
} nvn_cb_buffer;

/* An immutable package of commands. The handle of a recording is the address
 * of its first record, which is the address of this block. */
typedef struct nvn_cb_package {
    uint64_t handle;
    uint8_t *bytes;
    size_t bytes_used;
    size_t record_count;
    nvn_command_package_segment *segments;
    size_t segment_count;
    nvn_cb_copy *copies;
    struct nvn_cb_package *next;
} nvn_cb_package;

/* The segments of the host's own memory that the host lends.
 *
 * A segment is lent to one recording at a time and is given back when that
 * recording is over: the package of the recording holds a copy of the
 * records, so the memory of the segment is free again. The number of the
 * segments is the bound of the memory the host lends. */
typedef struct nvn_cb_pool_segment {
    uint8_t *mem;
    size_t capacity;
    int lent;
} nvn_cb_pool_segment;

static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static nvn_cb_buffer s_buffers[NVN_CB_MAX_BUFFERS];
static nvn_command_buffer_stats s_stats;
static uint64_t s_sequence;
static nvn_cb_package *s_packages;
static size_t s_package_count;
static uint64_t s_package_bytes;
static nvn_cb_pool_segment s_pool[NVN_CB_MAX_HOSTSEGMENTS];
static size_t s_pool_bytes;

/* The call of the guest thread boundary.
 *
 * The boundary component registers it when it comes up. This layer does not
 * link the boundary, so a build that has none still links and behaves
 * honestly: the memory callback of the guest is then refused by name and
 * counted. */
static nvn_command_buffer_guest_call s_guest_call;

void nvn_command_buffer_set_guest_call(nvn_command_buffer_guest_call call) {
    pthread_mutex_lock(&s_lock);
    s_guest_call = call;
    pthread_mutex_unlock(&s_lock);
}

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

static size_t align8(size_t value) { return (value + 7u) & ~(size_t)7u; }

/* ------------------------------------------------------------------ */
/* The segments of a recording                                         */
/* ------------------------------------------------------------------ */

/* The segment the recording writes into right now. It is the last one of the
 * list: the command memory the guest handed over first, and the segments the
 * host lent after it. */
static nvn_cb_segment *segment_current(nvn_cb_buffer *slot) {
    nvn_cb_segment *segment = slot->segments;
    if (!segment) return NULL;
    while (segment->next) segment = segment->next;
    return segment;
}

/* The segment with the given index. The index zero is the command memory the
 * guest handed over, so a record knows which memory it is in. */
static nvn_cb_segment *segment_at(nvn_cb_buffer *slot, size_t index) {
    nvn_cb_segment *segment = slot->segments;
    while (segment && index) {
        segment = segment->next;
        index--;
    }
    return segment;
}

/* The number of the segments a recording holds. */
static size_t segment_index_of(nvn_cb_buffer *slot, const nvn_cb_segment *wanted) {
    size_t index = 0;
    nvn_cb_segment *segment = slot->segments;
    while (segment && segment != wanted) {
        segment = segment->next;
        index++;
    }
    return segment ? index : 0;
}

/* The bytes the recording has written into the command memory the guest
 * handed over. The guest's accounting is about those bytes only: the memory
 * the host lends is not the guest's and is not counted against its budget. */
static size_t segment_guest_used(nvn_cb_buffer *slot) {
    nvn_cb_segment *first = slot->segments;
    return first ? first->used : 0;
}

/* Give the segments of a recording back and drop them.
 *
 * The memory of a segment the host lent goes back to the pool of the host:
 * the package of the recording holds a copy of the records, so the segment
 * itself is not needed any more. The command memory the guest handed over is
 * the package's memory when the recording ended, and is given back when the
 * recording was abandoned. */
static void segments_release(nvn_cb_buffer *slot, int keep_guest_memory) {
    nvn_cb_segment *segment = slot->segments;
    while (segment) {
        nvn_cb_segment *next = segment->next;
        if (segment->host) {
            size_t i;
            for (i = 0; i < NVN_CB_MAX_HOSTSEGMENTS; i++) {
                if (s_pool[i].lent && s_pool[i].mem == segment->mem) {
                    s_pool[i].lent = 0;
                    if (s_pool_bytes >= s_pool[i].capacity) s_pool_bytes -= s_pool[i].capacity;
                    break;
                }
            }
        } else if (!keep_guest_memory) {
            free(segment->mem);
        }
        free(segment);
        segment = next;
    }
    slot->segments = NULL;
    slot->segment_count = 0;
}

/* Take a segment of the host's own memory for a recording.
 *
 * The reference lends a segment of 256 KiB when the game's callback cannot
 * add enough. The bytes are the host's: the guest's memory is never written
 * to. The segment takes over the range of the guest's command memory the
 * recording had occupied, so that the guest can give it back.
 *
 * The pool is bounded. A recording that needs a segment and finds none is
 * refused by name, and the refusal says that a command does not fit: a
 * recording is never void because of memory. */
static nvn_cb_segment *hostseg_take(nvn_cb_buffer *slot) {
    size_t i;
    nvn_cb_segment *segment = NULL;
    for (i = 0; i < NVN_CB_MAX_HOSTSEGMENTS; i++) {
        nvn_cb_pool_segment *pool = &s_pool[i];
        nvn_cb_segment *tail;
        if (pool->lent) continue;
        if (!pool->mem) {
            pool->mem = (uint8_t *)malloc(NVN_CB_HOSTSEG_BYTES);
            if (!pool->mem) break;
            pool->capacity = NVN_CB_HOSTSEG_BYTES;
        }
        pool->lent = 1;
        s_pool_bytes += pool->capacity;
        segment = (nvn_cb_segment *)calloc(1, sizeof(*segment));
        if (!segment) {
            pool->lent = 0;
            s_pool_bytes -= pool->capacity;
            break;
        }
        segment->mem = pool->mem;
        segment->capacity = pool->capacity;
        segment->host = 1;
        segment->guest_lo = 0;
        segment->guest_hi = segment_guest_used(slot);
        /* The segment goes to the end of the list, so that the records stay
         * in the order they were made. */
        tail = segment_current(slot);
        if (tail) tail->next = segment;
        else slot->segments = segment;
        slot->segment_count++;
        s_stats.host_segments++;
        break;
    }
    if (segment) {
        s_stats.host_segment_bytes = s_pool_bytes;
        s_stats.host_segment_bound = (uint64_t)NVN_CB_MAX_HOSTSEGMENTS * NVN_CB_HOSTSEG_BYTES;
    } else {
        s_stats.host_segment_refused++;
        fprintf(stderr, "[nvn] the host has no segment of command memory left: all %u segments of "
                        "%u KiB are lent out and the bound of %u KiB is reached, so a command that "
                        "does not fit was refused\n",
                (unsigned)NVN_CB_MAX_HOSTSEGMENTS, (unsigned)(NVN_CB_HOSTSEG_BYTES >> 10),
                (unsigned)(((size_t)NVN_CB_MAX_HOSTSEGMENTS * NVN_CB_HOSTSEG_BYTES) >> 10));
    }
    return segment;
}

/* The guest hands out command memory again.
 *
 * The guest's command memory is one area that the guest hands out in pieces:
 * every grant is a window at the start of that area, and a recording that
 * ran out of it hangs off the range of the area it occupied. When the guest
 * hands out a window that covers that range, the guest gave that memory
 * back, and the segments of the host's memory that took it over are released.
 *
 * The runtime does not see the addresses of the guest's pool, so the overlap
 * is decided on the area of the guest's command memory and not on the
 * guest's addresses. */
static void hostseg_release(nvn_cb_buffer *slot, uint64_t grant) {
    nvn_cb_segment *segment;
    for (segment = slot->segments; segment; segment = segment->next) {
        if (!segment->host || segment->dead) continue;
        if (grant > segment->guest_lo && segment->guest_lo < segment->guest_hi) {
            segment->dead = 1;
            s_stats.host_segments_released++;
            fprintf(stderr, "[nvn] the guest handed out %" PRIu64 " bytes of command memory again: "
                            "the segment of the host's memory that took over the range %" PRIu64
                            "..%" PRIu64 " of the guest's command memory was given back\n",
                    grant, segment->guest_lo, segment->guest_hi);
        }
    }
}

/* The name of an opcode, for the log and for the tests. The name is the
 * command of the guest that the opcode stands for. */
const char *nvn_command_buffer_opcode_name(uint32_t op) {
    switch (op) {
    case NVN_CB_OP_END: return "nvnCommandBufferEndRecording";
    case NVN_CB_OP_JUMP: return "nvnCommandBufferJump";
    case NVN_CB_OP_BEGIN: return "nvnCommandBufferBeginRecording";
    case NVN_CB_OP_CALL: return "nvnCommandBufferCallCommands";
    case NVN_CB_OP_SIGNAL_EVENT: return "nvnCommandBufferSignalEvent";
    case NVN_CB_OP_REPORT_COUNTER: return "nvnCommandBufferReportCounter";
    case NVN_CB_OP_COPY_B2B: return "nvnCommandBufferCopyBufferToBuffer";
    case NVN_CB_OP_UPDATE_UB: return "nvnCommandBufferUpdateUniformBuffer";
    case NVN_CB_OP_SET_RT: return "nvnCommandBufferSetRenderTargets";
    case NVN_CB_OP_CLEAR_COLOR: return "nvnCommandBufferClearColor";
    case NVN_CB_OP_CLEAR_DS: return "nvnCommandBufferClearDepthStencil";
    case NVN_CB_OP_VIEWPORTS: return "nvnCommandBufferSetViewports";
    case NVN_CB_OP_SCISSOR: return "nvnCommandBufferSetScissor";
    case NVN_CB_OP_DEPTH_RANGE: return "nvnCommandBufferSetDepthRange";
    case NVN_CB_OP_BIND_PROGRAM: return "nvnCommandBufferBindProgram";
    case NVN_CB_OP_BIND_STATE: return "nvnCommandBufferBindState";
    case NVN_CB_OP_BIND_VATTR: return "nvnCommandBufferBindVertexAttribState";
    case NVN_CB_OP_BIND_VSTREAM: return "nvnCommandBufferBindVertexStreamState";
    case NVN_CB_OP_BIND_VBUF: return "nvnCommandBufferBindVertexBuffers";
    case NVN_CB_OP_BIND_UBO: return "nvnCommandBufferBindUniformBuffer";
    case NVN_CB_OP_BIND_SSBO: return "nvnCommandBufferBindStorageBuffer";
    case NVN_CB_OP_BIND_TEX: return "nvnCommandBufferBindTextures";
    case NVN_CB_OP_BIND_SAMP: return "nvnCommandBufferBindSamplers";
    case NVN_CB_OP_STENCIL: return "nvnCommandBufferSetStencil";
    case NVN_CB_OP_POLY_OFFSET: return "nvnCommandBufferSetPolygonOffset";
    case NVN_CB_OP_DRAW: return "nvnCommandBufferDrawArrays";
    case NVN_CB_OP_DRAW_IDX: return "nvnCommandBufferDrawElements";
    case NVN_CB_OP_DISPATCH: return "nvnCommandBufferDispatchCompute";
    case NVN_CB_OP_BARRIER: return "nvnCommandBufferBarrier";
    case NVN_CB_OP_SET_TEX_POOL: return "nvnCommandBufferSetTexturePool";
    case NVN_CB_OP_SET_SAMP_POOL: return "nvnCommandBufferSetSamplerPool";
    case NVN_CB_OP_COPY_B2T: return "nvnCommandBufferCopyBufferToTexture";
    case NVN_CB_OP_COPY_STRIDE: return "nvnCommandBufferSetCopyRowStride";
    case NVN_CB_OP_BLEND_COLOR: return "nvnCommandBufferSetBlendColor";
    case NVN_CB_OP_COPY_T2B: return "nvnCommandBufferCopyTextureToBuffer";
    case NVN_CB_OP_BIND_IMG: return "nvnCommandBufferBindImages";
    case NVN_CB_OP_COPY_T2T: return "nvnCommandBufferCopyTextureToTexture";
    case NVN_CB_OP_RESET_COUNTER: return "nvnCommandBufferResetCounter";
    case NVN_CB_OP_CLEAR_TEX: return "nvnCommandBufferClearTexture";
    case NVN_CB_OP_CLEAR_BUF: return "nvnCommandBufferClearBuffer";
    case NVN_CB_OP_DEPTH_CLAMP: return "nvnCommandBufferSetDepthClamp";
    case NVN_CB_OP_RENDER_COND: return "nvnCommandBufferSetRenderEnable";
    case NVN_CB_OP_FENCE_SYNC: return "nvnCommandBufferFenceSync";
    case NVN_CB_OP_TF_BIND: return "nvnCommandBufferBindTransformFeedbackBuffer";
    case NVN_CB_OP_TF_STATE: return "nvnCommandBufferBeginTransformFeedback";
    case NVN_CB_OP_RAST_DISCARD: return "nvnCommandBufferSetRasterizerDiscard";
    case NVN_CB_OP_VP_SWIZZLE: return "nvnCommandBufferSetViewportSwizzles";
    case NVN_CB_OP_BIND_TEX1: return "nvnCommandBufferBindTexture";
    case NVN_CB_OP_PRIM_RESTART: return "nvnCommandBufferSetPrimitiveRestart";
    default: return NULL;
    }
}

/* ------------------------------------------------------------------ */
/* The slots of the guest's command buffers                            */
/* ------------------------------------------------------------------ */

/* The copies of a recording are given back when the recording is abandoned.
 * A package keeps its copies: a record of a package may point at them for as
 * long as the package lives. */
static void copies_free(nvn_cb_copy *copies) {
    while (copies) {
        nvn_cb_copy *next = copies->next;
        free(copies->bytes);
        free(copies);
        copies = next;
    }
}

/* The slot of one guest command buffer.
 *
 * The records of the slot that is given up are dropped with it: the slot
 * belongs to another buffer now, and the records of that one belong to it.
 * The packages of the earlier recordings are not dropped: they are the
 * command memory that an earlier handle names. */
static nvn_cb_buffer *buffer_reset(nvn_cb_buffer *buffer, const void *object) {
    copies_free(buffer->copies);
    segments_release(buffer, 0);
    free(buffer->views);
    free(buffer->diagnostics);
    memset(buffer, 0, sizeof(*buffer));
    buffer->object = (void *)(uintptr_t)object;
    buffer->in_use = 1;
    buffer->sequence = ++s_sequence;
    return buffer;
}

/* The slot of one guest command buffer.
 *
 * A slot of a finalized buffer is given up before a slot of a live one,
 * because the recordings of a finalized buffer have been read by whoever
 * wanted them. */
static nvn_cb_buffer *buffer_find(const void *object, int create) {
    size_t i;
    nvn_cb_buffer *free_slot = NULL;
    nvn_cb_buffer *oldest_finalized = NULL;
    nvn_cb_buffer *oldest = NULL;
    if (!object) return NULL;
    for (i = 0; i < NVN_CB_MAX_BUFFERS; i++) {
        nvn_cb_buffer *buffer = &s_buffers[i];
        if (!buffer->in_use) {
            if (!free_slot) free_slot = buffer;
            continue;
        }
        if (buffer->object == object) return buffer;
        if (!oldest || buffer->sequence < oldest->sequence) oldest = buffer;
        if (buffer->finalized &&
            (!oldest_finalized || buffer->sequence < oldest_finalized->sequence)) {
            oldest_finalized = buffer;
        }
    }
    if (!create) return NULL;
    if (free_slot) return buffer_reset(free_slot, object);
    if (oldest_finalized) return buffer_reset(oldest_finalized, object);
    (void)oldest;
    return NULL;
}

/* ------------------------------------------------------------------ */
/* The registry of the packages                                       */
/* ------------------------------------------------------------------ */

/* The package a handle names.
 *
 * The handle is an address, so the lookup is a comparison against the
 * addresses the registry holds. A handle that the registry does not know is
 * never dereferenced: the comparison is the whole of the check. */
static nvn_cb_package *package_find(uint64_t handle) {
    nvn_cb_package *package;
    if (!handle) return NULL;
    for (package = s_packages; package; package = package->next) {
        if (package->handle == handle) return package;
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* The copies of guest data                                           */
/* ------------------------------------------------------------------ */

/* A copy of guest data, made at the moment of the call.
 *
 * The copy belongs to the recording, and the recording hands it to the
 * package, so a record never points at memory that the runtime may give
 * back. A copy that is too large is refused: the command that wanted it is
 * refused too, rather than recorded with half of the data. */
static void count_copied(size_t bytes) {
    s_stats.copies_bytes += bytes;
}

static void *copy_guest_data(nvn_cb_buffer *slot, const void *source, size_t size) {
    nvn_cb_copy *copy;
    void *bytes;
    if (!source || !size) return NULL;
    if (size > NVN_CB_MAX_COPY_BYTES) {
        s_stats.copy_refused++;
        return NULL;
    }
    bytes = malloc(size);
    if (!bytes) {
        s_stats.copy_refused++;
        return NULL;
    }
    memcpy(bytes, source, size);
    copy = (nvn_cb_copy *)calloc(1, sizeof(*copy));
    if (!copy) {
        free(bytes);
        s_stats.copy_refused++;
        return NULL;
    }
    copy->bytes = bytes;
    copy->size = size;
    copy->next = slot->copies;
    slot->copies = copy;
    count_copied(size);
    return bytes;
}

/* ------------------------------------------------------------------ */
/* The command memory of a recording                                   */
/* ------------------------------------------------------------------ */

/* How many bytes of the command memory the guest handed over may hold
 * records. The last eight bytes are kept for the end of the recording, so
 * that a recording that fills the memory still has room for its end. */
static size_t work_limit(const nvn_cb_buffer *slot) {
    if (slot->command_memory_size <= 8u) return 0;
    return (size_t)(slot->command_memory_size - 8u);
}

/* Room for one record of the given size in the command memory the guest
 * handed over.
 *
 * The memory grows with the budget the guest gives and never grows past it.
 * The last eight bytes of the budget are kept for the end of the recording:
 * a record that is not the end may not use them, and the end is the one
 * record that may. */
static int guest_reserve(nvn_cb_buffer *slot, size_t total, int is_end) {
    nvn_cb_segment *segment = segment_at(slot, 0);
    size_t budget = (size_t)slot->command_memory_size;
    size_t limit = is_end ? budget : work_limit(slot);
    size_t want;
    uint8_t *grown;
    if (!segment) return 0;
    if (segment->used + total > limit) return 0;
    if (segment->used + total <= segment->capacity) return 1;
    want = segment->capacity ? segment->capacity * 2u : 256u;
    if (want < segment->used + total) want = segment->used + total;
    if (want > budget) want = budget;
    if (want < segment->used + total) return 0;
    grown = (uint8_t *)realloc(segment->mem, want);
    if (!grown) return 0;
    if (grown != segment->mem && slot->view_count) {
        /* The views point into the command memory, so they move with it. */
        ptrdiff_t delta = (ptrdiff_t)(grown - segment->mem);
        size_t i;
        for (i = 0; i < slot->view_count; i++) {
            if (slot->views[i].segment != 0) continue;
            slot->views[i].payload = (const uint8_t *)((ptrdiff_t)slot->views[i].payload + delta);
        }
    }
    segment->mem = grown;
    segment->capacity = want;
    return 1;
}

/* The recording cannot be completed. Everything that was recorded into it
 * is lost, and the end of the recording will not hand out a handle.
 *
 * A recording is never void because of memory: the memory the host runs out
 * of is a segment of the host's own memory, and the bound of that memory is
 * refused by name. The reason of a void recording is a command that does not
 * fit. */
static void recording_void(nvn_cb_buffer *slot, const char *reason) {
    if (!slot->void_recording) {
        slot->void_recording = 1;
        slot->void_reason = reason;
        s_stats.void_reason = reason;
        /* The records of a void recording are lost, so their memory goes
         * back and the views of them are dropped with it. */
        segments_release(slot, 0);
        slot->view_count = 0;
        fprintf(stderr, "[nvn] the recording of the command buffer %p is void: %s; the records of "
                        "this recording are lost and its end will not hand out a handle\n",
                slot->object, reason);
    }
}

/* Ask the guest for more command memory, through the guest thread boundary.
 *
 * The callback is the guest's own function, and it calls back into this
 * layer to add command memory, so the lock is not held while it runs. The
 * call goes through the hook of the boundary: the callback runs as guest code
 * on the guest's own thread, and the guest's pointer is never called
 * directly.
 *
 * The hook answers with one of the values of
 * nvn_command_buffer_guest_call_result. A refusal is counted under its own
 * name: the caller was not the guest's own thread, the guest's thread
 * pointer was not bound on this thread, or the address the guest handed over
 * is not inside the text of the guest's module. A pointer the guest made up
 * is never called.
 *
 * When no boundary is registered the callback is refused, said by name and
 * counted: a build without the boundary has no way to call the guest, and
 * pretending otherwise would be a lie. The recording is not void because of
 * it: the host lends a segment of its own memory instead. */
static int request_more_memory(void *buffer, uint64_t needed) {
    uint64_t callback = 0;
    uint64_t data = 0;
    nvn_command_buffer_guest_call call = NULL;
    pthread_mutex_lock(&s_lock);
    {
        nvn_cb_buffer *slot = buffer_find(buffer, 0);
        if (slot) {
            callback = slot->memory_callback;
            data = slot->memory_callback_data;
        }
        call = s_guest_call;
    }
    pthread_mutex_unlock(&s_lock);
    if (!callback) {
        pthread_mutex_lock(&s_lock);
        s_stats.callback_absent++;
        pthread_mutex_unlock(&s_lock);
        return 0;
    }
    if (!call) {
        pthread_mutex_lock(&s_lock);
        s_stats.callback_unavailable++;
        pthread_mutex_unlock(&s_lock);
        fprintf(stderr, "[nvn] the command memory of the command buffer %p is exhausted and the "
                        "guest thread boundary has not registered its call, so the memory callback "
                        "of the guest was not called; the host lends a segment of its own memory "
                        "instead\n",
                buffer);
        return 0;
    }
    {
        uint64_t x[9];
        uint64_t out_x[9];
        int i;
        int rc;
        for (i = 0; i < 9; i++) {
            x[i] = 0;
            out_x[i] = 0;
        }
        x[0] = (uint64_t)(uintptr_t)buffer;
        x[1] = 0u; /* NVN_COMMAND_BUFFER_MEMORY_EVENT_OUT_OF_COMMAND_MEMORY */
        x[2] = needed;
        x[3] = data;
        rc = call((void *)(uintptr_t)callback, x, out_x);
        if (rc != NVN_CB_GUEST_CALL_OK) {
            pthread_mutex_lock(&s_lock);
            if (rc == NVN_CB_GUEST_CALL_WRONG_THREAD) {
                s_stats.callback_refused_thread++;
            } else if (rc == NVN_CB_GUEST_CALL_NO_THREAD_POINTER) {
                s_stats.callback_refused_pointer++;
            } else if (rc == NVN_CB_GUEST_CALL_BAD_ADDRESS) {
                s_stats.callback_refused_address++;
            } else {
                s_stats.callback_failures++;
            }
            pthread_mutex_unlock(&s_lock);
            fprintf(stderr, "[nvn] the memory callback of the guest was refused and was not "
                            "called; the host lends a segment of its own memory instead\n");
            return 0;
        }
    }
    pthread_mutex_lock(&s_lock);
    s_stats.callback_calls++;
    pthread_mutex_unlock(&s_lock);
    return 1;
}

/* Write one record into the command memory of the recording. The lock is
 * held by the caller.
 *
 * The payload is zeroed, as the reference zeroes it, and the header is
 * written with the size the record takes. The payload pointer is returned so
 * that the caller can fill it in.
 *
 * NVN_CB_RESERVE_FULL means that the command memory is full: the caller may
 * ask the guest for more and try again. NVN_CB_RESERVE_REFUSED means that
 * the command cannot be recorded at all. */
#define NVN_CB_RESERVE_OK 0
#define NVN_CB_RESERVE_FULL 1
#define NVN_CB_RESERVE_REFUSED 2

static int record_write_locked(nvn_cb_buffer *slot, uint32_t op, uint32_t payload_size,
                                int is_end, uint8_t **out) {
    size_t total;
    nvn_cb_op_hdr *header;
    uint8_t *at;
    nvn_cb_segment *segment;
    uint32_t index;
    *out = NULL;
    if (payload_size + 8u > NVN_CB_MAX_RECORD_BYTES) {
        s_stats.dropped++;
        s_stats.copy_refused++;
        fprintf(stderr, "[nvn] the command %s does not fit into a record of %u bytes, so it was "
                        "refused and not recorded\n",
                nvn_command_buffer_opcode_name(op) ? nvn_command_buffer_opcode_name(op)
                                                   : "of an unknown kind",
                NVN_CB_MAX_RECORD_BYTES);
        return NVN_CB_RESERVE_REFUSED;
    }
    total = align8((size_t)payload_size + 8u);
    segment = segment_current(slot);
    if (!segment) {
        /* The first segment of a recording is the command memory the guest
         * handed over. It is made when the first record is written. */
        segment = (nvn_cb_segment *)calloc(1, sizeof(*segment));
        if (!segment) {
            s_stats.dropped++;
            return NVN_CB_RESERVE_REFUSED;
        }
        slot->segments = segment;
        slot->segment_count = 1;
    }
    index = (uint32_t)segment_index_of(slot, segment);
    if (segment->host) {
        /* A segment of the host's memory has a fixed size: when it is full,
         * the recording needs another one. */
        if (segment->used + total > segment->capacity) return NVN_CB_RESERVE_FULL;
    } else if (!guest_reserve(slot, total, is_end)) {
        return NVN_CB_RESERVE_FULL;
    }
    at = segment->mem + segment->used;
    header = (nvn_cb_op_hdr *)at;
    header->op = op;
    header->size = (uint32_t)total;
    memset(at + 8, 0, total - 8u);
    segment->used += total;
    segment->record_count++;
    if (slot->view_count >= NVN_CB_MAX_RECORDS) {
        s_stats.dropped++;
    } else {
        if (slot->view_count >= slot->view_capacity) {
            size_t capacity = slot->view_capacity ? slot->view_capacity * 2u : 64u;
            nvn_recorded_command *views;
            if (capacity > NVN_CB_MAX_RECORDS) capacity = NVN_CB_MAX_RECORDS;
            views = (nvn_recorded_command *)realloc(slot->views,
                                                     capacity * sizeof(nvn_recorded_command));
            if (!views) {
                s_stats.dropped++;
            } else {
                slot->views = views;
                slot->view_capacity = capacity;
            }
        }
        if (slot->view_count < slot->view_capacity) {
            slot->views[slot->view_count].op = op;
            slot->views[slot->view_count].size = (uint32_t)total;
            slot->views[slot->view_count].payload = at + 8;
            slot->views[slot->view_count].segment = index;
            slot->view_count++;
        }
    }
    *out = at + 8;
    s_stats.recorded++;
    return NVN_CB_RESERVE_OK;
}

/* Reserve one record in the command memory of the recording that is running.
 *
 * A command that is made outside a recording window does not enter the
 * command stream: it is counted and named.
 *
 * A command that does not fit into the command memory the guest handed over
 * asks the guest for more memory through its callback, as the reference does.
 * When the guest's callback cannot add enough, the recording continues in a
 * segment of the host's own memory: a recording is never void because of
 * memory, and the end of the recording still hands out a handle. Only when
 * the bound of the host's lent memory is reached is a command refused, and
 * that refusal is named. */
static uint8_t *record_open(void *buffer, uint32_t op, uint32_t payload_size) {
    uint8_t *payload = NULL;
    int tries = 0;
    s_stats.calls++;
    for (;;) {
        int need_more = 0;
        uint64_t needed = 0;
        pthread_mutex_lock(&s_lock);
        {
            nvn_cb_buffer *slot = buffer_find(buffer, 1);
            if (!slot) {
                s_stats.refused++;
            } else if (!slot->recording) {
                s_stats.outside_recording++;
                s_stats.dropped++;
            } else if (slot->void_recording) {
                s_stats.dropped++;
            } else {
                int result = record_write_locked(slot, op, payload_size, 0, &payload);
                if (result == NVN_CB_RESERVE_FULL) {
                    need_more = 1;
                    needed = align8((size_t)payload_size + 8u);
                }
            }
        }
        pthread_mutex_unlock(&s_lock);
        if (!need_more) return payload;
        /* The guest's callback is asked for more command memory, twice, as
         * the reference does. */
        if (tries < NVN_CB_MAX_CALLBACK_TRIES) {
            tries++;
            if (request_more_memory(buffer, needed)) continue;
            /* The callback could not be made. The host lends a segment of its
             * own memory instead, and the refusal was said by name. */
        }
        /* The guest's callback did not add enough: the recording continues in
         * a segment of the host's own memory. */
        {
            int lent = 0;
            pthread_mutex_lock(&s_lock);
            {
                nvn_cb_buffer *slot = buffer_find(buffer, 0);
                if (slot && !slot->void_recording && hostseg_take(slot)) lent = 1;
            }
            pthread_mutex_unlock(&s_lock);
            if (lent) continue;
        }
        /* The bound of the host's lent memory is reached. The refusal was
         * named, and the recording says that a command does not fit: it is
         * not void because of memory. */
        pthread_mutex_lock(&s_lock);
        {
            nvn_cb_buffer *slot = buffer_find(buffer, 0);
            if (slot) {
                recording_void(slot, "a command does not fit into the command memory of the "
                                     "recording");
            }
        }
        pthread_mutex_unlock(&s_lock);
        s_stats.overflow++;
        return NULL;
    }
}

/* ------------------------------------------------------------------ */
/* The recorders                                                       */
/* ------------------------------------------------------------------ */

/* The kinds of the state that a bind state record carries. The numbers are
 * the reference's. */
enum {
    NVN_CB_STATE_BLEND = 0,
    NVN_CB_STATE_COLOR = 1,
    NVN_CB_STATE_CHANNEL_MASK = 2,
    NVN_CB_STATE_MULTISAMPLE = 3,
    NVN_CB_STATE_POLYGON = 4,
    NVN_CB_STATE_DEPTH_STENCIL = 5
};

/* The texture slots of a stage: the separate texture binds have slots of
 * their own, as the reference has them. */
#define NVN_CB_SEPTEX_SLOT0 128

/* The copy of a view. The reference reads the first sixteen bytes of the
 * guest's view object as its own view structure; the same sixteen bytes go
 * here, and nothing more is read. */
static void copy_view(nvn_cb_view *out, uint8_t *has, const void *view, size_t view_size) {
    size_t size = sizeof(*out);
    *has = view != NULL;
    if (!view) return;
    if (view_size < size) size = view_size;
    memcpy(out, view, size);
    count_copied(size);
}

void nvn_command_buffer_rec_set_render_targets(void *buffer, int count, const void *const *colors,
                                                size_t texture_size, const void *const *views,
                                                size_t view_size, const void *depth,
                                                const void *depth_view) {
    nvn_cb_p_set_rt *record;
    void *color_copy[8];
    void *depth_copy = NULL;
    int i;
    int failed = 0;
    if (count < 0 || count > 8) {
        fprintf(stderr, "[nvn] SetRenderTargets with %d targets: the reference holds eight, so the "
                        "command was refused and not recorded\n",
                count);
        s_stats.dropped++;
        return;
    }
    memset(color_copy, 0, sizeof(color_copy));
    /* The copies are made before the record is written: a target that could
     * not be copied refuses the whole command rather than being recorded as
     * a target that is not there. */
    pthread_mutex_lock(&s_lock);
    {
        nvn_cb_buffer *slot = buffer_find(buffer, 1);
        if (slot) {
            for (i = 0; i < count; i++) {
                const void *texture = colors ? colors[i] : NULL;
                if (!texture) continue;
                color_copy[i] = copy_guest_data(slot, texture, texture_size);
                if (!color_copy[i]) {
                    failed = 1;
                    break;
                }
            }
            if (!failed && depth) {
                depth_copy = copy_guest_data(slot, depth, texture_size);
                if (!depth_copy) failed = 1;
            }
        } else {
            failed = 1;
        }
    }
    pthread_mutex_unlock(&s_lock);
    if (failed) {
        s_stats.dropped++;
        return;
    }
    record = (nvn_cb_p_set_rt *)record_open(buffer, NVN_CB_OP_SET_RT, sizeof(*record));
    if (!record) return;
    record->n = count;
    for (i = 0; i < count; i++) {
        record->color[i] = color_copy[i];
        copy_view(&record->cview[i], &record->has_cview[i], views ? views[i] : NULL, view_size);
    }
    record->depth = depth_copy;
    copy_view(&record->dview, &record->has_dview, depth_view, view_size);
}

void nvn_command_buffer_rec_clear_color(void *buffer, int index, const void *rgba, int mask,
                                         int is_integer) {
    nvn_cb_p_clear_color *record;
    if (!rgba) {
        s_stats.dropped++;
        return;
    }
    record = (nvn_cb_p_clear_color *)record_open(buffer, NVN_CB_OP_CLEAR_COLOR, sizeof(*record));
    if (!record) return;
    record->index = index;
    record->mask = mask;
    if (is_integer) {
        const uint32_t *values = (const uint32_t *)rgba;
        int i;
        for (i = 0; i < 4; i++) record->rgba[i] = (float)values[i];
    } else {
        memcpy(record->rgba, rgba, sizeof(record->rgba));
    }
    count_copied(sizeof(record->rgba));
}

void nvn_command_buffer_rec_clear_depth_stencil(void *buffer, float depth, int depth_mask,
                                                 int stencil, int stencil_mask) {
    nvn_cb_p_clear_ds *record =
        (nvn_cb_p_clear_ds *)record_open(buffer, NVN_CB_OP_CLEAR_DS, sizeof(*record));
    if (!record) return;
    record->depth = depth;
    record->depth_mask = depth_mask;
    record->stencil = stencil;
    record->stencil_mask = stencil_mask;
}

void nvn_command_buffer_rec_set_viewports(void *buffer, int first, int count, const float *values) {
    nvn_cb_p_viewports *record;
    int i;
    if (!values || count < 0 || first < 0 || first + count > 16) {
        fprintf(stderr, "[nvn] SetViewports first=%d count=%d: the reference holds sixteen "
                        "viewports, so the command was refused and not recorded\n",
                first, count);
        s_stats.dropped++;
        return;
    }
    record = (nvn_cb_p_viewports *)record_open(buffer, NVN_CB_OP_VIEWPORTS, sizeof(*record));
    if (!record) return;
    record->first = first;
    record->n = count;
    for (i = 0; i < count; i++) memcpy(record->v[i], values + 4 * i, 4 * sizeof(float));
    count_copied((size_t)count * 4 * sizeof(float));
}

void nvn_command_buffer_rec_set_viewport(void *buffer, int x, int y, int w, int h) {
    float values[4];
    values[0] = (float)x;
    values[1] = (float)y;
    values[2] = (float)w;
    values[3] = (float)h;
    nvn_command_buffer_rec_set_viewports(buffer, 0, 1, values);
}

void nvn_command_buffer_rec_set_scissors(void *buffer, int first, int count, const int *values) {
    int i;
    if (!values || count < 0) {
        s_stats.dropped++;
        return;
    }
    for (i = 0; i < count; i++) {
        nvn_cb_p_scissor *record =
            (nvn_cb_p_scissor *)record_open(buffer, NVN_CB_OP_SCISSOR, sizeof(*record));
        const int *source = values + 4 * i;
        if (!record) return;
        record->x = source[0];
        record->y = source[1];
        record->w = source[2];
        record->h = source[3];
    }
    count_copied((size_t)count * 4 * sizeof(int));
    (void)first;
}

void nvn_command_buffer_rec_set_scissor(void *buffer, int x, int y, int w, int h) {
    nvn_cb_p_scissor *record =
        (nvn_cb_p_scissor *)record_open(buffer, NVN_CB_OP_SCISSOR, sizeof(*record));
    if (!record) return;
    record->x = x;
    record->y = y;
    record->w = w;
    record->h = h;
}

void nvn_command_buffer_rec_set_depth_ranges(void *buffer, int first, int count,
                                              const float *values) {
    int i;
    if (!values || count < 0) {
        s_stats.dropped++;
        return;
    }
    for (i = 0; i < count; i++) {
        nvn_cb_p_depth_range *record =
            (nvn_cb_p_depth_range *)record_open(buffer, NVN_CB_OP_DEPTH_RANGE, sizeof(*record));
        const float *source = values + 2 * i;
        if (!record) return;
        record->n = source[0];
        record->f = source[1];
    }
    count_copied((size_t)count * 2 * sizeof(float));
    (void)first;
}

void nvn_command_buffer_rec_set_depth_range(void *buffer, float near_value, float far_value) {
    nvn_cb_p_depth_range *record =
        (nvn_cb_p_depth_range *)record_open(buffer, NVN_CB_OP_DEPTH_RANGE, sizeof(*record));
    if (!record) return;
    record->n = near_value;
    record->f = far_value;
}

void nvn_command_buffer_rec_bind_program(void *buffer, const void *program, size_t program_size,
                                          int stages) {
    nvn_cb_p_bind_program *record;
    nvn_cb_buffer *slot;
    void *copy = NULL;
    if (program) {
        pthread_mutex_lock(&s_lock);
        slot = buffer_find(buffer, 0);
        if (slot) copy = copy_guest_data(slot, program, program_size);
        pthread_mutex_unlock(&s_lock);
        if (!copy) {
            s_stats.dropped++;
            return;
        }
    }
    record = (nvn_cb_p_bind_program *)record_open(buffer, NVN_CB_OP_BIND_PROGRAM, sizeof(*record));
    if (!record) return;
    record->prog = copy;
    record->stages = stages;
}

void nvn_command_buffer_rec_bind_state(void *buffer, int kind, const void *state,
                                       size_t state_size) {
    nvn_cb_p_bind_state *record;
    if (!state) {
        s_stats.dropped++;
        return;
    }
    record = (nvn_cb_p_bind_state *)record_open(buffer, NVN_CB_OP_BIND_STATE, sizeof(*record));
    if (!record) return;
    record->kind = kind;
    if (state_size > sizeof(record->bytes)) state_size = sizeof(record->bytes);
    memcpy(record->bytes, state, state_size);
    count_copied(state_size);
}

void nvn_command_buffer_rec_bind_vertex_attrib_state(void *buffer, int count, const void *states,
                                                      size_t state_size) {
    nvn_cb_p_bind_vattr *record;
    size_t payload;
    if (!states || count < 0 || count > 16) {
        s_stats.dropped++;
        return;
    }
    payload = offsetof(nvn_cb_p_bind_vattr, st) + (size_t)count * state_size;
    record = (nvn_cb_p_bind_vattr *)record_open(buffer, NVN_CB_OP_BIND_VATTR, (uint32_t)payload);
    if (!record) return;
    record->first = 0;
    record->n = count;
    memcpy(record->st, states, (size_t)count * state_size);
    count_copied((size_t)count * state_size);
}

void nvn_command_buffer_rec_bind_vertex_stream_state(void *buffer, int count, const void *states,
                                                      size_t state_size) {
    nvn_cb_p_bind_vstream *record;
    size_t payload;
    if (!states || count < 0 || count > 16) {
        s_stats.dropped++;
        return;
    }
    payload = offsetof(nvn_cb_p_bind_vstream, st) + (size_t)count * state_size;
    record = (nvn_cb_p_bind_vstream *)record_open(buffer, NVN_CB_OP_BIND_VSTREAM,
                                                   (uint32_t)payload);
    if (!record) return;
    record->first = 0;
    record->n = count;
    memcpy(record->st, states, (size_t)count * state_size);
    count_copied((size_t)count * state_size);
}

void nvn_command_buffer_rec_bind_vertex_buffers(void *buffer, int first, int count,
                                                 const void *ranges, size_t range_size) {
    nvn_cb_p_bind_vbuf *record;
    size_t payload;
    if (!ranges || count < 0 || first < 0 || first + count > 16) {
        s_stats.dropped++;
        return;
    }
    payload = offsetof(nvn_cb_p_bind_vbuf, r) + (size_t)count * range_size;
    record = (nvn_cb_p_bind_vbuf *)record_open(buffer, NVN_CB_OP_BIND_VBUF, (uint32_t)payload);
    if (!record) return;
    record->first = first;
    record->n = count;
    memcpy(record->r, ranges, (size_t)count * range_size);
    count_copied((size_t)count * range_size);
}

void nvn_command_buffer_rec_bind_vertex_buffer(void *buffer, int first, uint64_t address,
                                                uint64_t size) {
    nvn_cb_buffer_range range;
    nvn_cb_p_bind_vbuf *record;
    size_t payload;
    range.address = address;
    range.size = size;
    payload = offsetof(nvn_cb_p_bind_vbuf, r) + sizeof(range);
    record = (nvn_cb_p_bind_vbuf *)record_open(buffer, NVN_CB_OP_BIND_VBUF, (uint32_t)payload);
    if (!record) return;
    record->first = first;
    record->n = 1;
    memcpy(record->r, &range, sizeof(range));
}

void nvn_command_buffer_rec_bind_uniform_buffer(void *buffer, int stage, int index,
                                                 uint64_t address, uint64_t size) {
    nvn_cb_p_bind_buf *record =
        (nvn_cb_p_bind_buf *)record_open(buffer, NVN_CB_OP_BIND_UBO, sizeof(*record));
    if (!record) return;
    record->stage = stage;
    record->index = index;
    record->addr = address;
    record->size = size;
}

void nvn_command_buffer_rec_bind_uniform_buffers(void *buffer, int stage, int first, int count,
                                                  const void *ranges, size_t range_size) {
    int i;
    if (!ranges || count < 0) {
        s_stats.dropped++;
        return;
    }
    for (i = 0; i < count; i++) {
        const unsigned char *source = (const unsigned char *)ranges + (size_t)i * range_size;
        uint64_t address = 0;
        uint64_t size = 0;
        memcpy(&address, source, sizeof(address) < range_size ? sizeof(address) : range_size);
        if (range_size >= sizeof(size)) memcpy(&size, source + sizeof(address), sizeof(size));
        nvn_command_buffer_rec_bind_uniform_buffer(buffer, stage, first + i, address, size);
    }
    count_copied((size_t)count * 16);
}

void nvn_command_buffer_rec_bind_storage_buffer(void *buffer, int stage, int index,
                                                 uint64_t address, uint64_t size) {
    nvn_cb_p_bind_buf *record =
        (nvn_cb_p_bind_buf *)record_open(buffer, NVN_CB_OP_BIND_SSBO, sizeof(*record));
    if (!record) return;
    record->stage = stage;
    record->index = index;
    record->addr = address;
    record->size = size;
}

void nvn_command_buffer_rec_bind_storage_buffers(void *buffer, int stage, int first, int count,
                                                  const void *ranges, size_t range_size) {
    int i;
    if (!ranges || count < 0) {
        s_stats.dropped++;
        return;
    }
    for (i = 0; i < count; i++) {
        const unsigned char *source = (const unsigned char *)ranges + (size_t)i * range_size;
        uint64_t address = 0;
        uint64_t size = 0;
        memcpy(&address, source, sizeof(address) < range_size ? sizeof(address) : range_size);
        if (range_size >= sizeof(size)) memcpy(&size, source + sizeof(address), sizeof(size));
        nvn_command_buffer_rec_bind_storage_buffer(buffer, stage, first + i, address, size);
    }
    count_copied((size_t)count * 16);
}

/* The handles of a bind: the reference reads them as words. */
static void bind_handles(void *buffer, uint32_t op, int stage, int first, int count,
                          const void *handles) {
    nvn_cb_p_bind_handles *record;
    size_t payload;
    if (!handles || count < 0 || count > 32) {
        s_stats.dropped++;
        return;
    }
    payload = offsetof(nvn_cb_p_bind_handles, h) + (size_t)count * sizeof(uint64_t);
    record = (nvn_cb_p_bind_handles *)record_open(buffer, op, (uint32_t)payload);
    if (!record) return;
    record->stage = stage;
    record->first = first;
    record->n = count;
    record->defer = 0;
    record->src = 0;
    memcpy(record->h, handles, (size_t)count * sizeof(uint64_t));
    count_copied((size_t)count * sizeof(uint64_t));
    /* The array form of the bind has no room for the numbering bases, so a
     * handle in it is read with the numbering of the pool as it was
     * registered, without the base of the pool the buffer bound. The
     * reference says so out loud, and so does this. */
    {
        int32_t tex_base = 0;
        int32_t samp_base = 0;
        pthread_mutex_lock(&s_lock);
        {
            nvn_cb_buffer *state = buffer_find(buffer, 0);
            if (state) {
                tex_base = state->tex_base;
                samp_base = state->samp_base;
            }
        }
        pthread_mutex_unlock(&s_lock);
        if (tex_base || samp_base) {
            fprintf(stderr, "[nvn] the handles bound in a batch are read at replay without the numbering "
                            "base of the descriptor pools this buffer bound (texture pool base %d, sampler "
                            "pool base %d); the single texture bind carries the bases\n",
                    tex_base, samp_base);
        }
    }
}

void nvn_command_buffer_rec_bind_texture(void *buffer, int stage, int slot, uint64_t handle) {
    nvn_cb_p_bind_tex1 *record =
        (nvn_cb_p_bind_tex1 *)record_open(buffer, NVN_CB_OP_BIND_TEX1, sizeof(*record));
    int32_t tex_base = 0;
    int32_t samp_base = 0;
    if (!record) return;
    pthread_mutex_lock(&s_lock);
    {
        nvn_cb_buffer *state = buffer_find(buffer, 0);
        if (state) {
            tex_base = state->tex_base;
            samp_base = state->samp_base;
        }
    }
    pthread_mutex_unlock(&s_lock);
    record->stage = stage;
    record->slot = slot;
    record->h = handle;
    record->tbase = tex_base;
    record->sbase = samp_base;
}

void nvn_command_buffer_rec_bind_textures(void *buffer, int stage, int first, int count,
                                          const void *handles) {
    bind_handles(buffer, NVN_CB_OP_BIND_TEX, stage, first, count, handles);
}

void nvn_command_buffer_rec_bind_separate_texture(void *buffer, int stage, int slot,
                                                   uint64_t handle) {
    bind_handles(buffer, NVN_CB_OP_BIND_TEX, stage, NVN_CB_SEPTEX_SLOT0 + slot, 1, &handle);
}

void nvn_command_buffer_rec_bind_separate_textures(void *buffer, int stage, int first, int count,
                                                   const void *handles) {
    bind_handles(buffer, NVN_CB_OP_BIND_TEX, stage, NVN_CB_SEPTEX_SLOT0 + first, count, handles);
}

void nvn_command_buffer_rec_bind_sampler(void *buffer, int stage, int slot, uint64_t handle) {
    bind_handles(buffer, NVN_CB_OP_BIND_SAMP, stage, slot, 1, &handle);
}

void nvn_command_buffer_rec_bind_samplers(void *buffer, int stage, int first, int count,
                                          const void *handles) {
    bind_handles(buffer, NVN_CB_OP_BIND_SAMP, stage, first, count, handles);
}

void nvn_command_buffer_rec_bind_separate_sampler(void *buffer, int stage, int slot,
                                                   uint64_t handle) {
    bind_handles(buffer, NVN_CB_OP_BIND_SAMP, stage, slot, 1, &handle);
}

void nvn_command_buffer_rec_bind_separate_samplers(void *buffer, int stage, int first, int count,
                                                   const void *handles) {
    bind_handles(buffer, NVN_CB_OP_BIND_SAMP, stage, first, count, handles);
}

void nvn_command_buffer_rec_bind_image(void *buffer, int stage, int slot, uint64_t handle) {
    bind_handles(buffer, NVN_CB_OP_BIND_IMG, stage, slot, 1, &handle);
}

void nvn_command_buffer_rec_bind_images(void *buffer, int stage, int first, int count,
                                        const void *handles) {
    bind_handles(buffer, NVN_CB_OP_BIND_IMG, stage, first, count, handles);
}

void nvn_command_buffer_rec_set_stencil(void *buffer, int face, int value_mask, int mask, int ref,
                                        int which) {
    nvn_cb_p_stencil *record =
        (nvn_cb_p_stencil *)record_open(buffer, NVN_CB_OP_STENCIL, sizeof(*record));
    if (!record) return;
    record->face = face;
    record->value_mask = value_mask;
    record->mask = mask;
    record->ref = ref;
    record->which = which;
}

void nvn_command_buffer_rec_set_polygon_offset(void *buffer, float factor, float units,
                                                float clamp_value) {
    nvn_cb_p_poly_offset *record =
        (nvn_cb_p_poly_offset *)record_open(buffer, NVN_CB_OP_POLY_OFFSET, sizeof(*record));
    if (!record) return;
    record->factor = factor;
    record->units = units;
    record->clamp = clamp_value;
}

void nvn_command_buffer_rec_draw(void *buffer, int primitive, int first, int count, int instances,
                                 int base_instance, uint64_t indirect) {
    nvn_cb_p_draw draw;
    size_t payload;
    nvn_cb_p_draw *record;
    memset(&draw, 0, sizeof(draw));
    draw.prim = primitive;
    draw.first = first;
    draw.count = count;
    draw.instances = instances;
    draw.base_instance = base_instance;
    draw.ind = indirect;
    /* A plain draw stops before the indirect fields, as the reference does:
     * the game sizes its command memory by what this runtime reports. */
    payload = indirect ? sizeof(draw) : offsetof(nvn_cb_p_draw, ind);
    record = (nvn_cb_p_draw *)record_open(buffer, NVN_CB_OP_DRAW, (uint32_t)payload);
    if (!record) return;
    memcpy(record, &draw, payload);
}

void nvn_command_buffer_rec_draw_indexed(void *buffer, int primitive, int index_type, int count,
                                         int base_vertex, int base_instance, int instances,
                                         uint64_t index_buffer, uint64_t indirect) {
    nvn_cb_p_draw_idx draw;
    size_t payload;
    nvn_cb_p_draw_idx *record;
    memset(&draw, 0, sizeof(draw));
    draw.prim = primitive;
    draw.itype = index_type;
    draw.count = count;
    draw.base_vertex = base_vertex;
    draw.base_instance = base_instance;
    draw.instances = instances;
    draw.ibuf = index_buffer;
    draw.ind = indirect;
    payload = indirect ? sizeof(draw) : offsetof(nvn_cb_p_draw_idx, ind);
    record = (nvn_cb_p_draw_idx *)record_open(buffer, NVN_CB_OP_DRAW_IDX, (uint32_t)payload);
    if (!record) return;
    memcpy(record, &draw, payload);
}

void nvn_command_buffer_rec_dispatch(void *buffer, int x, int y, int z, uint64_t indirect) {
    nvn_cb_p_dispatch *record =
        (nvn_cb_p_dispatch *)record_open(buffer, NVN_CB_OP_DISPATCH, sizeof(*record));
    if (!record) return;
    record->x = x;
    record->y = y;
    record->z = z;
    record->caller = 0;
    record->ind = indirect;
}

void nvn_command_buffer_rec_barrier(void *buffer, int bits) {
    int32_t *record = (int32_t *)record_open(buffer, NVN_CB_OP_BARRIER, sizeof(int32_t));
    if (!record) return;
    *record = bits;
}

void nvn_command_buffer_rec_set_texture_pool(void *buffer, const void *pool) {
    nvn_cb_p_set_pool *record;
    nvn_cb_buffer *slot;
    void *copy = NULL;
    if (pool) {
        pthread_mutex_lock(&s_lock);
        slot = buffer_find(buffer, 0);
        if (slot) {
            copy = copy_guest_data(slot, pool, 0x20u);
            /* The numbering base of the pool is settled here and kept in
             * the buffer's own state, the way the reference does it. */
            slot->tex_base = nvn_texture_pool_base(pool);
        }
        pthread_mutex_unlock(&s_lock);
        if (!copy) {
            s_stats.dropped++;
            return;
        }
    }
    record = (nvn_cb_p_set_pool *)record_open(buffer, NVN_CB_OP_SET_TEX_POOL, sizeof(*record));
    if (!record) return;
    record->pool = copy;
}

void nvn_command_buffer_rec_set_sampler_pool(void *buffer, const void *pool) {
    nvn_cb_p_set_pool *record;
    nvn_cb_buffer *slot;
    void *copy = NULL;
    if (pool) {
        pthread_mutex_lock(&s_lock);
        slot = buffer_find(buffer, 0);
        if (slot) {
            copy = copy_guest_data(slot, pool, 0x20u);
            /* The samplers have a numbering of their own, apart from the
             * numbering of the textures. */
            slot->samp_base = nvn_sampler_pool_base(pool);
        }
        pthread_mutex_unlock(&s_lock);
        if (!copy) {
            s_stats.dropped++;
            return;
        }
    }
    record = (nvn_cb_p_set_pool *)record_open(buffer, NVN_CB_OP_SET_SAMP_POOL, sizeof(*record));
    if (!record) return;
    record->pool = copy;
}

void nvn_command_buffer_rec_set_blend_color(void *buffer, const float *rgba) {
    nvn_cb_p_blend_color *record;
    if (!rgba) {
        s_stats.dropped++;
        return;
    }
    record = (nvn_cb_p_blend_color *)record_open(buffer, NVN_CB_OP_BLEND_COLOR, sizeof(*record));
    if (!record) return;
    memcpy(record->rgba, rgba, sizeof(record->rgba));
    count_copied(sizeof(record->rgba));
}

void nvn_command_buffer_rec_clear_buffer(void *buffer, uint64_t address, uint64_t size,
                                          uint32_t value) {
    nvn_cb_p_clear_buf *record =
        (nvn_cb_p_clear_buf *)record_open(buffer, NVN_CB_OP_CLEAR_BUF, sizeof(*record));
    if (!record) return;
    record->addr = address;
    record->size = size;
    record->value = value;
    record->pad = 0;
}

void nvn_command_buffer_rec_clear_texture(void *buffer, const void *texture, const void *view,
                                           const void *region, const void *rgba, int mask,
                                           int is_integer) {
    nvn_cb_p_clear_tex *record;
    nvn_cb_buffer *slot;
    void *texture_copy = NULL;
    if (!texture || !rgba || !region) {
        s_stats.dropped++;
        return;
    }
    pthread_mutex_lock(&s_lock);
    slot = buffer_find(buffer, 0);
    if (slot) texture_copy = copy_guest_data(slot, texture, 0xC0u);
    pthread_mutex_unlock(&s_lock);
    if (!texture_copy) {
        s_stats.dropped++;
        return;
    }
    record = (nvn_cb_p_clear_tex *)record_open(buffer, NVN_CB_OP_CLEAR_TEX, sizeof(*record));
    if (!record) return;
    record->tex = texture_copy;
    copy_view(&record->view, &record->has_view, view, 0x28u);
    memcpy(&record->r, region, sizeof(record->r));
    count_copied(sizeof(record->r));
    record->mask = mask;
    record->is_uint = is_integer ? 1u : 0u;
    if (is_integer) {
        const uint32_t *values = (const uint32_t *)rgba;
        int i;
        for (i = 0; i < 4; i++) record->c.u[i] = values[i];
    } else {
        const float *values = (const float *)rgba;
        int i;
        for (i = 0; i < 4; i++) record->c.f[i] = values[i];
    }
    count_copied(16);
}

void nvn_command_buffer_rec_copy_buffer_to_texture(void *buffer, uint64_t source,
                                                   const void *texture, const void *view,
                                                   const void *region, int flags) {
    nvn_cb_p_copy_b2t *record;
    nvn_cb_buffer *slot;
    void *texture_copy = NULL;
    if (!texture || !region) {
        s_stats.dropped++;
        return;
    }
    pthread_mutex_lock(&s_lock);
    slot = buffer_find(buffer, 0);
    if (slot) texture_copy = copy_guest_data(slot, texture, 0xC0u);
    pthread_mutex_unlock(&s_lock);
    if (!texture_copy) {
        s_stats.dropped++;
        return;
    }
    record = (nvn_cb_p_copy_b2t *)record_open(buffer, NVN_CB_OP_COPY_B2T, sizeof(*record));
    if (!record) return;
    record->src = source;
    record->tex = texture_copy;
    copy_view(&record->view, &record->has_view, view, 0x28u);
    memcpy(&record->r, region, sizeof(record->r));
    count_copied(sizeof(record->r));
    record->flags = flags;
}

void nvn_command_buffer_rec_copy_texture_to_buffer(void *buffer, const void *texture,
                                                   const void *view, const void *region,
                                                   uint64_t destination, int flags) {
    nvn_cb_p_copy_b2t *record;
    nvn_cb_buffer *slot;
    void *texture_copy = NULL;
    if (!texture || !region) {
        s_stats.dropped++;
        return;
    }
    pthread_mutex_lock(&s_lock);
    slot = buffer_find(buffer, 0);
    if (slot) texture_copy = copy_guest_data(slot, texture, 0xC0u);
    pthread_mutex_unlock(&s_lock);
    if (!texture_copy) {
        s_stats.dropped++;
        return;
    }
    record = (nvn_cb_p_copy_b2t *)record_open(buffer, NVN_CB_OP_COPY_T2B, sizeof(*record));
    if (!record) return;
    record->src = destination;
    record->tex = texture_copy;
    copy_view(&record->view, &record->has_view, view, 0x28u);
    memcpy(&record->r, region, sizeof(record->r));
    count_copied(sizeof(record->r));
    record->flags = flags;
}

void nvn_command_buffer_rec_copy_texture_to_texture(void *buffer, const void *source,
                                                     const void *source_view,
                                                     const void *source_region, const void *destination,
                                                     const void *destination_view,
                                                     const void *destination_region, int flags) {
    nvn_cb_p_copy_t2t *record;
    nvn_cb_buffer *slot;
    void *source_copy = NULL;
    void *destination_copy = NULL;
    if (!source || !destination || !source_region || !destination_region) {
        s_stats.dropped++;
        return;
    }
    pthread_mutex_lock(&s_lock);
    slot = buffer_find(buffer, 0);
    if (slot) {
        source_copy = copy_guest_data(slot, source, 0xC0u);
        destination_copy = copy_guest_data(slot, destination, 0xC0u);
    }
    pthread_mutex_unlock(&s_lock);
    if (!source_copy || !destination_copy) {
        s_stats.dropped++;
        return;
    }
    record = (nvn_cb_p_copy_t2t *)record_open(buffer, NVN_CB_OP_COPY_T2T, sizeof(*record));
    if (!record) return;
    record->src = source_copy;
    record->dst = destination_copy;
    copy_view(&record->sv, &record->has_sv, source_view, 0x28u);
    copy_view(&record->dv, &record->has_dv, destination_view, 0x28u);
    memcpy(&record->sr, source_region, sizeof(record->sr));
    memcpy(&record->dr, destination_region, sizeof(record->dr));
    count_copied(sizeof(record->sr) + sizeof(record->dr));
    record->flags = flags;
}

void nvn_command_buffer_rec_copy_buffer_to_buffer(void *buffer, uint64_t source,
                                                  uint64_t destination, uint64_t size) {
    /* The reference writes the three words of this record itself. */
    uint64_t *record = (uint64_t *)record_open(buffer, NVN_CB_OP_COPY_B2B, 3 * sizeof(uint64_t));
    if (!record) return;
    record[0] = source;
    record[1] = destination;
    record[2] = size;
}

void nvn_command_buffer_rec_update_uniform_buffer(void *buffer, uint64_t address, int64_t offset,
                                                   uint64_t size, const void *data) {
    uint8_t *record;
    size_t payload;
    if (!data || size > NVN_CB_MAX_COPY_BYTES) {
        s_stats.dropped++;
        s_stats.copy_refused++;
        return;
    }
    payload = 24u + (size_t)size;
    record = (uint8_t *)record_open(buffer, NVN_CB_OP_UPDATE_UB, (uint32_t)payload);
    if (!record) return;
    {
        uint64_t destination = address + (uint64_t)offset;
        uint64_t length = size;
        memcpy(record, &destination, sizeof(destination));
        memcpy(record + 8, &length, sizeof(length));
        memcpy(record + 16, data, (size_t)size);
    }
    count_copied((size_t)size);
}

void nvn_command_buffer_rec_report_counter(void *buffer, uint32_t type, uint64_t address) {
    uint8_t *record = (uint8_t *)record_open(buffer, NVN_CB_OP_REPORT_COUNTER, 16);
    uint64_t value = type;
    if (!record) return;
    memcpy(record, &value, sizeof(value));
    memcpy(record + 8, &address, sizeof(address));
}

void nvn_command_buffer_rec_reset_counter(void *buffer, uint32_t type) {
    uint8_t *record = (uint8_t *)record_open(buffer, NVN_CB_OP_RESET_COUNTER, 8);
    uint64_t value = type;
    if (!record) return;
    memcpy(record, &value, sizeof(value));
}

void nvn_command_buffer_rec_report_value(void *buffer, uint32_t value, uint64_t address) {
    /* The reference writes a value write as a signal event of the write
     * mode, with the address of the word as the event's value pointer. */
    uint8_t *record = (uint8_t *)record_open(buffer, NVN_CB_OP_SIGNAL_EVENT, 16);
    uint64_t pointer = address;
    uint32_t mode = 2u;
    if (!record) return;
    memcpy(record, &pointer, sizeof(pointer));
    memcpy(record + 8, &mode, sizeof(mode));
    memcpy(record + 12, &value, sizeof(value));
}

void nvn_command_buffer_rec_signal_event(void *buffer, const void *event, uint32_t mode,
                                          uint32_t value) {
    uint8_t *record;
    nvn_cb_buffer *slot;
    void *copy = NULL;
    if (event) {
        pthread_mutex_lock(&s_lock);
        slot = buffer_find(buffer, 1);
        if (slot) copy = copy_guest_data(slot, event, 0x40u);
        pthread_mutex_unlock(&s_lock);
        if (!copy) {
            s_stats.dropped++;
            return;
        }
    }
    record = (uint8_t *)record_open(buffer, NVN_CB_OP_SIGNAL_EVENT, 16);
    if (!record) return;
    {
        uint64_t pointer = (uint64_t)(uintptr_t)copy;
        memcpy(record, &pointer, sizeof(pointer));
        memcpy(record + 8, &mode, sizeof(mode));
        memcpy(record + 12, &value, sizeof(value));
    }
}

void nvn_command_buffer_rec_fence_sync(void *buffer, const void *sync) {
    uint8_t *record;
    nvn_cb_buffer *slot;
    void *copy = NULL;
    if (sync) {
        pthread_mutex_lock(&s_lock);
        slot = buffer_find(buffer, 1);
        if (slot) copy = copy_guest_data(slot, sync, 0x40u);
        pthread_mutex_unlock(&s_lock);
        if (!copy) {
            s_stats.dropped++;
            return;
        }
    }
    record = (uint8_t *)record_open(buffer, NVN_CB_OP_FENCE_SYNC, 8);
    if (!record) return;
    {
        uint64_t pointer = (uint64_t)(uintptr_t)copy;
        memcpy(record, &pointer, sizeof(pointer));
    }
}

void nvn_command_buffer_rec_set_render_enable(void *buffer, int enabled) {
    nvn_cb_p_render_cond *record =
        (nvn_cb_p_render_cond *)record_open(buffer, NVN_CB_OP_RENDER_COND, sizeof(*record));
    if (!record) return;
    record->mode = enabled ? 2 : 3;
    record->pad = 0;
    record->addr = 0;
}

void nvn_command_buffer_rec_set_render_enable_conditional(void *buffer, int mode,
                                                           uint64_t address) {
    nvn_cb_p_render_cond *record =
        (nvn_cb_p_render_cond *)record_open(buffer, NVN_CB_OP_RENDER_COND, sizeof(*record));
    if (!record) return;
    record->mode = mode & 1;
    record->pad = 0;
    record->addr = address;
}

void nvn_command_buffer_rec_set_depth_clamp(void *buffer, int enabled) {
    nvn_cb_p_depth_clamp *record =
        (nvn_cb_p_depth_clamp *)record_open(buffer, NVN_CB_OP_DEPTH_CLAMP, sizeof(*record));
    if (!record) return;
    record->on = enabled ? 1 : 0;
}

void nvn_command_buffer_rec_set_rasterizer_discard(void *buffer, int enabled) {
    nvn_cb_p_rast_discard *record =
        (nvn_cb_p_rast_discard *)record_open(buffer, NVN_CB_OP_RAST_DISCARD, sizeof(*record));
    if (!record) return;
    record->on = enabled ? 1 : 0;
}

void nvn_command_buffer_rec_set_viewport_swizzles(void *buffer, int first, int count,
                                                   const void *swizzles) {
    nvn_cb_p_vp_swizzle *record;
    const uint32_t *values = (const uint32_t *)swizzles;
    int i;
    int k;
    if (!values || count < 0 || first < 0 || first + count > 16) {
        s_stats.dropped++;
        return;
    }
    record = (nvn_cb_p_vp_swizzle *)record_open(buffer, NVN_CB_OP_VP_SWIZZLE, sizeof(*record));
    if (!record) return;
    record->first = first;
    record->n = count;
    for (i = 0; i < count; i++) {
        for (k = 0; k < 4; k++) {
            uint32_t value = values[4 * i + k];
            record->sw[i][k] = (uint8_t)((value ^ (uint32_t)(2 * k)) & 7u);
        }
    }
    count_copied((size_t)count * 4);
}

void nvn_command_buffer_rec_set_primitive_restart(void *buffer, int enabled, int index) {
    nvn_cb_p_prim_restart *record =
        (nvn_cb_p_prim_restart *)record_open(buffer, NVN_CB_OP_PRIM_RESTART, sizeof(*record));
    if (!record) return;
    record->on = enabled ? 1u : 0u;
    record->index = (uint32_t)index;
}

void nvn_command_buffer_rec_bind_transform_feedback_buffer(void *buffer, int index,
                                                           uint64_t address, uint64_t size) {
    nvn_cb_p_tf_bind *record =
        (nvn_cb_p_tf_bind *)record_open(buffer, NVN_CB_OP_TF_BIND, sizeof(*record));
    if (!record) return;
    record->index = index;
    record->pad = 0;
    record->addr = address;
    record->size = size;
}

void nvn_command_buffer_rec_bind_transform_feedback_buffers(void *buffer, int first, int count,
                                                            const void *ranges) {
    int i;
    if (!ranges || count < 0) {
        s_stats.dropped++;
        return;
    }
    for (i = 0; i < count; i++) {
        const unsigned char *source = (const unsigned char *)ranges + (size_t)i * 16u;
        uint64_t address = 0;
        uint64_t size = 0;
        memcpy(&address, source, sizeof(address));
        memcpy(&size, source + sizeof(address), sizeof(size));
        nvn_command_buffer_rec_bind_transform_feedback_buffer(buffer, first + i, address, size);
    }
    count_copied((size_t)count * 16);
}

void nvn_command_buffer_rec_transform_feedback_state(void *buffer, int what, uint64_t control) {
    nvn_cb_p_tf_state *record =
        (nvn_cb_p_tf_state *)record_open(buffer, NVN_CB_OP_TF_STATE, sizeof(*record));
    if (!record) return;
    record->what = what;
    record->pad = 0;
    record->ctl = control;
}

void nvn_command_buffer_rec_set_copy_stride(void *buffer, int64_t stride, int image) {
    nvn_cb_p_copy_stride *record =
        (nvn_cb_p_copy_stride *)record_open(buffer, NVN_CB_OP_COPY_STRIDE, sizeof(*record));
    if (!record) return;
    record->stride = stride;
    record->image = image;
}

void nvn_command_buffer_rec_call_commands(void *buffer, int count, const uint64_t *handles) {
    uint64_t *copied = NULL;
    int i;
    int unknown = 0;
    if (count < 0 || (count > 0 && !handles)) {
        s_stats.dropped++;
        return;
    }
    if (count > 0) {
        /* The guest may write over the array of handles the moment the call
         * returns, so the array is copied before anything else happens. */
        copied = (uint64_t *)malloc((size_t)count * sizeof(uint64_t));
        if (!copied) {
            s_stats.dropped++;
            return;
        }
        memcpy(copied, handles, (size_t)count * sizeof(uint64_t));
    }
    pthread_mutex_lock(&s_lock);
    for (i = 0; i < count; i++) {
        if (!package_find(copied[i])) {
            unknown++;
            s_stats.unknown_handles++;
            fprintf(stderr, "[nvn] the call of a command handle %" PRIu64 " was refused: the "
                            "registry does not know it, and it was not dereferenced\n",
                    copied[i]);
        }
    }
    pthread_mutex_unlock(&s_lock);
    if (unknown) {
        /* A handle that is not known refuses the whole call: the commands it
         * stands for are not known either. */
        free(copied);
        s_stats.dropped++;
        return;
    }
    for (i = 0; i < count; i++) {
        uint8_t *record = (uint8_t *)record_open(buffer, NVN_CB_OP_CALL, sizeof(uint64_t));
        if (!record) break;
        memcpy(record, &copied[i], sizeof(uint64_t));
    }
    free(copied);
}

/* ------------------------------------------------------------------ */
/* The commands that the reference's format has no record for          */
/* ------------------------------------------------------------------ */

/* A diagnostic record of a call that did not enter the command stream. */
static void diagnostic_add(nvn_cb_buffer *slot, const char *name, const uint64_t *args,
                            uint32_t arg_count, const uint8_t *unresolved) {
    nvn_diagnostic_command *record;
    uint32_t i;
    if (slot->diagnostic_count >= NVN_CB_MAX_DIAGNOSTICS) return;
    if (slot->diagnostic_count >= slot->diagnostic_capacity) {
        size_t capacity = slot->diagnostic_capacity ? slot->diagnostic_capacity * 2u : 32u;
        nvn_diagnostic_command *grown;
        if (capacity > NVN_CB_MAX_DIAGNOSTICS) capacity = NVN_CB_MAX_DIAGNOSTICS;
        grown = (nvn_diagnostic_command *)realloc(slot->diagnostics,
                                                   capacity * sizeof(nvn_diagnostic_command));
        if (!grown) return;
        slot->diagnostics = grown;
        slot->diagnostic_capacity = capacity;
    }
    record = &slot->diagnostics[slot->diagnostic_count++];
    memset(record, 0, sizeof(*record));
    record->name = name;
    record->arg_count = arg_count > 8 ? 8u : arg_count;
    for (i = 0; i < record->arg_count; i++) record->args[i] = args ? args[i] : 0;
    if (unresolved) {
        for (i = 0; i < record->arg_count; i++) {
            if (unresolved[i]) {
                record->unresolved[i] = 1;
                record->unresolved_count++;
            }
        }
    }
}

void nvn_command_buffer_record_unmapped(void *buffer, const char *name, const uint64_t *args,
                                        uint32_t arg_count, const uint8_t *unresolved) {
    uint32_t i;
    pthread_mutex_lock(&s_lock);
    s_stats.calls++;
    s_stats.unmapped++;
    for (i = 0; i < arg_count && i < 8u; i++) {
        if (unresolved && unresolved[i]) s_stats.unresolved++;
    }
    {
        nvn_cb_buffer *slot = buffer_find(buffer, 1);
        if (slot) {
            diagnostic_add(slot, name, args, arg_count, unresolved);
        } else {
            s_stats.refused++;
        }
    }
    pthread_mutex_unlock(&s_lock);
    {
        static uint32_t reported;
        if (reported < 32u) {
            reported++;
            fprintf(stderr, "[nvn] %s: the format of the game this port runs has no record for "
                            "this command, so it was not recorded in the command stream",
                    name);
            for (i = 1; i < arg_count && i < 8u; i++) {
                if (unresolved && unresolved[i]) {
                    fprintf(stderr, " %" PRIu64 " (a pointer that was not followed)", args[i]);
                } else {
                    fprintf(stderr, " %" PRIu64, args[i]);
                }
            }
            fprintf(stderr, "\n");
        }
    }
}

void nvn_command_buffer_record_diagnostic(void *buffer, const char *name, const uint64_t *args,
                                          uint32_t arg_count) {
    uint8_t unresolved[8];
    uint32_t i;
    for (i = 0; i < 8u; i++) unresolved[i] = 0;
    /* The signature of this entry point is not known, so nothing is known
     * about its arguments either: none of them was followed. */
    for (i = 1; i < arg_count && i < 8u; i++) unresolved[i] = 1;
    pthread_mutex_lock(&s_lock);
    s_stats.calls++;
    s_stats.raw_calls++;
    s_stats.diagnostic_only++;
    s_stats.unresolved += (arg_count > 1u) ? (arg_count - 1u) : 0u;
    {
        nvn_cb_buffer *slot = buffer_find(buffer, 1);
        if (slot) {
            diagnostic_add(slot, name, args, arg_count, unresolved);
        } else {
            s_stats.refused++;
        }
    }
    pthread_mutex_unlock(&s_lock);
    {
        static uint32_t reported;
        if (reported < 32u) {
            reported++;
            fprintf(stderr, "[nvn] %s: diagnostic only; the signature of this entry point is not "
                            "in the header this runtime was built against, so the arguments were "
                            "taken as plain words, nothing was dereferenced and nothing was "
                            "recorded in the command stream\n",
                    name);
        }
    }
}

/* ------------------------------------------------------------------ */
/* The state of the recorder                                           */
/* ------------------------------------------------------------------ */

uint64_t nvn_command_buffer_float_bits(float value) {
    uint32_t bits = 0;
    memcpy(&bits, &value, sizeof(bits));
    return (uint64_t)bits;
}

void nvn_command_buffer_set_result(uint64_t value) {
    /* The result of a call is handed over in x0. The entry point that needs
     * this declares nothing as its return type, so the value cannot be
     * handed over through the C return. */
    __asm__ volatile("mov x0, %0" : : "r"(value) : "x0");
}

int nvn_command_buffer_initialize(void *buffer) {
    int ok = 0;
    pthread_mutex_lock(&s_lock);
    {
        nvn_cb_buffer *slot = buffer_find(buffer, 1);
        if (slot) {
            /* A buffer that is initialised again starts empty, because the
             * commands of the earlier run are not the commands of this one.
             * The packages of the earlier recordings stay: an earlier handle
             * still names them. */
            slot->recording = 0;
            slot->void_recording = 0;
            slot->void_reason = NULL;
            slot->finalized = 0;
            slot->command_memory_size = 0;
            slot->control_memory_size = 0;
            slot->copy_row_stride = 0;
            slot->copy_image_stride = 0;
            slot->memory_callback = 0;
            slot->memory_callback_data = 0;
            segments_release(slot, 0);
            slot->view_count = 0;
            slot->diagnostic_count = 0;
            s_stats.initialized++;
            ok = 1;
        } else {
            s_stats.refused++;
        }
    }
    pthread_mutex_unlock(&s_lock);
    if (ok) {
        fprintf(stderr, "[nvn] nvnCommandBufferInitialize: buffer=%p; the commands of this buffer "
                        "are recorded on the host in the format of the game\n",
                buffer);
    } else {
        fprintf(stderr, "[nvn] nvnCommandBufferInitialize: buffer=%p; the command buffer table is "
                        "full, so the buffer was not initialized\n",
                buffer);
    }
    return ok;
}

void nvn_command_buffer_finalize(void *buffer) {
    pthread_mutex_lock(&s_lock);
    {
        nvn_cb_buffer *slot = buffer_find(buffer, 0);
        if (slot) {
            /* The packages stay: an earlier handle still names them, and the
             * guest may call them after the buffer was given up. */
            slot->finalized = 1;
            slot->recording = 0;
            s_stats.finalized++;
        }
    }
    pthread_mutex_unlock(&s_lock);
}

void nvn_command_buffer_begin_recording(void *buffer) {
    pthread_mutex_lock(&s_lock);
    {
        nvn_cb_buffer *slot = buffer_find(buffer, 1);
        if (slot) {
            slot->recording = 1;
            slot->void_recording = 0;
            slot->void_reason = NULL;
            /* The command memory of a recording that was never ended is of no
             * use to anyone: the segments of the host's memory go back to the
             * pool with it. */
            segments_release(slot, 0);
            slot->view_count = 0;
            /* The copies of a recording that was never ended are of no use
             * to anyone; the copies of a recording that ended went to its
             * package with it. */
            copies_free(slot->copies);
            slot->copies = NULL;
        }
    }
    pthread_mutex_unlock(&s_lock);
}

/* The memory of the package.
 *
 * The package holds the command memory the guest handed over and the records
 * of the segments the host lent, in the order they were made, in one piece of
 * memory: a reader that walks the package sees every record. The records of
 * the host's segments are copied into it, so the segment itself is free again
 * when the recording is over.
 *
 * The views of the records follow their bytes. */
static uint8_t *package_assemble(nvn_cb_buffer *slot, size_t total,
                                  nvn_command_package_segment *segments) {
    nvn_cb_segment *first = slot->segments;
    nvn_cb_segment *segment;
    uint8_t *block;
    size_t offset;
    size_t index = 0;
    if (!first) return NULL;
    if (total > first->capacity) {
        uint8_t *grown = (uint8_t *)realloc(first->mem, total);
        if (!grown) return NULL;
        if (grown != first->mem && slot->view_count) {
            ptrdiff_t delta = (ptrdiff_t)(grown - first->mem);
            size_t i;
            for (i = 0; i < slot->view_count; i++) {
                if (slot->views[i].segment != 0) continue;
                slot->views[i].payload = (const uint8_t *)((ptrdiff_t)slot->views[i].payload + delta);
            }
        }
        first->mem = grown;
        first->capacity = total;
    }
    block = first->mem;
    offset = first->used;
    segments[0].offset = 0;
    segments[0].bytes = first->used;
    segments[0].record_count = first->record_count;
    segments[0].host = 0;
    for (segment = first->next; segment; segment = segment->next) {
        index++;
        if (segment->used) memcpy(block + offset, segment->mem, segment->used);
        segments[index].offset = offset;
        segments[index].bytes = segment->used;
        segments[index].record_count = segment->record_count;
        segments[index].host = 1;
        offset += segment->used;
    }
    /* The records that were in a segment of the host's memory are in the
     * package now, and their views follow them. */
    {
        size_t i;
        for (i = 0; i < slot->view_count; i++) {
            uint32_t which = slot->views[i].segment;
            nvn_cb_segment *source;
            size_t within;
            if (which == 0) continue;
            source = segment_at(slot, which);
            if (!source) continue;
            within = (size_t)((slot->views[i].payload - sizeof(nvn_cb_op_hdr)) - source->mem);
            slot->views[i].payload =
                block + segments[which].offset + within + sizeof(nvn_cb_op_hdr);
        }
    }
    return block;
}

uint64_t nvn_command_buffer_end_recording(void *buffer) {
    uint64_t handle = 0;
    const char *reason = NULL;
    pthread_mutex_lock(&s_lock);
    {
        nvn_cb_buffer *slot = buffer_find(buffer, 0);
        if (!slot) {
            s_stats.refused++;
            reason = "the command buffer is not known";
        } else if (!slot->recording) {
            reason = "no recording was running";
        } else if (slot->void_recording) {
            reason = slot->void_reason ? slot->void_reason : "the recording was void";
        } else {
            /* The end of the recording is the last record of it, as in the
             * reference. It has to fit into the command memory the guest
             * handed over; eight bytes of it are kept for exactly this. When
             * it does not fit, a segment of the host's memory takes it, and
             * the recording is not void because of memory. */
            uint8_t *end = NULL;
            int result = record_write_locked(slot, NVN_CB_OP_END, 0, 1, &end);
            if (result == NVN_CB_RESERVE_FULL && hostseg_take(slot)) {
                result = record_write_locked(slot, NVN_CB_OP_END, 0, 1, &end);
            }
            if (result != NVN_CB_RESERVE_OK) {
                reason = "a command does not fit into the command memory of the recording";
                s_stats.overflow++;
            }
        }
        if (!reason) {
            nvn_cb_package *package;
            size_t total = 0;
            size_t count = 0;
            size_t segments = slot->segment_count;
            nvn_cb_segment *segment;
            nvn_command_package_segment *map = NULL;
            uint8_t *block = NULL;
            for (segment = slot->segments; segment; segment = segment->next) {
                total += segment->used;
                count += segment->record_count;
            }
            if (s_package_count >= NVN_CB_MAX_PACKAGES ||
                s_package_bytes + total > NVN_CB_MAX_PACKAGE_BYTES) {
                reason = "the registry of the command packages is full";
            } else if (!segments) {
                reason = "the recording holds no command memory";
            } else {
                map = (nvn_command_package_segment *)calloc(segments, sizeof(*map));
                package = map ? (nvn_cb_package *)calloc(1, sizeof(*package)) : NULL;
                if (!map || !package) {
                    free(map);
                    free(package);
                    reason = "the package of the recording could not be allocated";
                } else {
                    /* The command memory of the recording becomes the
                     * package: it is written once and never written again,
                     * so the handle names an immutable package of commands. */
                    block = package_assemble(slot, total, map);
                    if (!block) {
                        free(map);
                        free(package);
                        reason = "the package of the recording could not be allocated";
                    } else {
                        package->handle = (uint64_t)(uintptr_t)block;
                        package->bytes = block;
                        package->bytes_used = total;
                        package->record_count = count;
                        package->segments = map;
                        package->segment_count = segments;
                        package->copies = slot->copies;
                        package->next = s_packages;
                        s_packages = package;
                        s_package_count++;
                        s_package_bytes += package->bytes_used;
                        s_stats.packages++;
                        s_stats.package_bytes = s_package_bytes;
                        s_stats.handles++;
                        handle = package->handle;
                        slot->copies = NULL;
                        /* The command memory is the package's now, and the
                         * segments of the host's memory go back to the pool:
                         * the package holds a copy of their records. */
                        segments_release(slot, 1);
                    }
                }
            }
        }
        if (reason) {
            s_stats.void_recordings++;
            if (slot && !slot->void_recording) recording_void(slot, reason);
            fprintf(stderr, "[nvn] nvnCommandBufferEndRecording: buffer=%p -> no handle: %s; the "
                            "recording is void and its records were not handed out\n",
                    buffer, reason);
        }
        if (slot) slot->recording = 0;
    }
    pthread_mutex_unlock(&s_lock);
    if (handle) {
        fprintf(stderr, "[nvn] nvnCommandBufferEndRecording: buffer=%p -> handle %" PRIu64
                        "; the handle is the address of the first record of the command memory of "
                        "this recording, and that memory is never written again\n",
                buffer, handle);
    }
    return handle;
}

void nvn_command_buffer_add_command_memory(void *buffer, uint64_t size) {
    pthread_mutex_lock(&s_lock);
    {
        nvn_cb_buffer *slot = buffer_find(buffer, 1);
        if (slot) {
            slot->command_memory_size += size;
            /* The guest hands out command memory again: the segments of the
             * host's memory that took over the range of the guest's command
             * memory the recording occupied are given back. */
            hostseg_release(slot, size);
        }
    }
    pthread_mutex_unlock(&s_lock);
}

void nvn_command_buffer_add_control_memory(void *buffer, uint64_t size) {
    pthread_mutex_lock(&s_lock);
    {
        nvn_cb_buffer *slot = buffer_find(buffer, 1);
        if (slot) slot->control_memory_size += size;
    }
    pthread_mutex_unlock(&s_lock);
}

void nvn_command_buffer_set_copy_row_stride(void *buffer, int64_t stride) {
    pthread_mutex_lock(&s_lock);
    {
        nvn_cb_buffer *slot = buffer_find(buffer, 1);
        if (slot) slot->copy_row_stride = stride;
    }
    pthread_mutex_unlock(&s_lock);
}

void nvn_command_buffer_set_copy_image_stride(void *buffer, int64_t stride) {
    pthread_mutex_lock(&s_lock);
    {
        nvn_cb_buffer *slot = buffer_find(buffer, 1);
        if (slot) slot->copy_image_stride = stride;
    }
    pthread_mutex_unlock(&s_lock);
}

void nvn_command_buffer_set_memory_callback(void *buffer, uint64_t callback) {
    pthread_mutex_lock(&s_lock);
    {
        nvn_cb_buffer *slot = buffer_find(buffer, 1);
        if (slot) slot->memory_callback = callback;
    }
    pthread_mutex_unlock(&s_lock);
}

void nvn_command_buffer_set_memory_callback_data(void *buffer, uint64_t data) {
    pthread_mutex_lock(&s_lock);
    {
        nvn_cb_buffer *slot = buffer_find(buffer, 1);
        if (slot) slot->memory_callback_data = data;
    }
    pthread_mutex_unlock(&s_lock);
}

uint64_t nvn_command_buffer_get_command_memory_size(const void *buffer) {
    uint64_t size = 0;
    pthread_mutex_lock(&s_lock);
    {
        nvn_cb_buffer *slot = buffer_find(buffer, 0);
        if (slot) size = slot->command_memory_size;
    }
    pthread_mutex_unlock(&s_lock);
    return size;
}

uint64_t nvn_command_buffer_get_command_memory_free(const void *buffer) {
    uint64_t size = 0;
    pthread_mutex_lock(&s_lock);
    {
        nvn_cb_buffer *slot = buffer_find(buffer, 0);
        if (slot) {
            uint64_t used = segment_guest_used(slot);
            size = slot->command_memory_size > used ? slot->command_memory_size - used : 0;
        }
    }
    pthread_mutex_unlock(&s_lock);
    return size;
}

uint64_t nvn_command_buffer_get_command_memory_used(const void *buffer) {
    uint64_t size = 0;
    pthread_mutex_lock(&s_lock);
    {
        nvn_cb_buffer *slot = buffer_find(buffer, 0);
        if (slot) size = segment_guest_used(slot);
    }
    pthread_mutex_unlock(&s_lock);
    return size;
}

uint64_t nvn_command_buffer_get_control_memory_size(const void *buffer) {
    uint64_t size = 0;
    pthread_mutex_lock(&s_lock);
    {
        nvn_cb_buffer *slot = buffer_find(buffer, 0);
        if (slot) size = slot->control_memory_size;
    }
    pthread_mutex_unlock(&s_lock);
    return size;
}

uint64_t nvn_command_buffer_get_control_memory_free(const void *buffer) {
    uint64_t size = 0;
    pthread_mutex_lock(&s_lock);
    {
        nvn_cb_buffer *slot = buffer_find(buffer, 0);
        if (slot) size = slot->control_memory_size;
    }
    pthread_mutex_unlock(&s_lock);
    return size;
}

int64_t nvn_command_buffer_get_copy_row_stride(const void *buffer) {
    int64_t stride = 0;
    pthread_mutex_lock(&s_lock);
    {
        nvn_cb_buffer *slot = buffer_find(buffer, 0);
        if (slot) stride = slot->copy_row_stride;
    }
    pthread_mutex_unlock(&s_lock);
    return stride;
}

int64_t nvn_command_buffer_get_copy_image_stride(const void *buffer) {
    int64_t stride = 0;
    pthread_mutex_lock(&s_lock);
    {
        nvn_cb_buffer *slot = buffer_find(buffer, 0);
        if (slot) stride = slot->copy_image_stride;
    }
    pthread_mutex_unlock(&s_lock);
    return stride;
}

uint64_t nvn_command_buffer_get_memory_callback(const void *buffer) {
    uint64_t callback = 0;
    pthread_mutex_lock(&s_lock);
    {
        nvn_cb_buffer *slot = buffer_find(buffer, 0);
        if (slot) callback = slot->memory_callback;
    }
    pthread_mutex_unlock(&s_lock);
    return callback;
}

uint64_t nvn_command_buffer_get_memory_callback_data(const void *buffer) {
    uint64_t data = 0;
    pthread_mutex_lock(&s_lock);
    {
        nvn_cb_buffer *slot = buffer_find(buffer, 0);
        if (slot) data = slot->memory_callback_data;
    }
    pthread_mutex_unlock(&s_lock);
    return data;
}

int nvn_command_buffer_is_recording(const void *buffer) {
    int recording = 0;
    pthread_mutex_lock(&s_lock);
    {
        nvn_cb_buffer *slot = buffer_find(buffer, 0);
        if (slot) recording = slot->recording;
    }
    pthread_mutex_unlock(&s_lock);
    return recording;
}

void nvn_command_buffer_report_used_is_unknown(const void *buffer, const char *what) {
    int report = 0;
    pthread_mutex_lock(&s_lock);
    {
        nvn_cb_buffer *slot = buffer_find(buffer, 0);
        if (slot && !slot->used_reported) {
            slot->used_reported = 1;
            report = 1;
            s_stats.honest_answers++;
        }
    }
    pthread_mutex_unlock(&s_lock);
    if (report) {
        fprintf(stderr, "[nvn] the guest asked how much of its %s it has already used; that memory "
                        "is the guest's own and the guest writes into it itself, so the runtime "
                        "cannot see how much of it is written and the answer is 0, which is not a "
                        "measurement\n",
                what);
    }
}

/* ------------------------------------------------------------------ */
/* Reading the records                                                 */
/* ------------------------------------------------------------------ */

int nvn_command_buffer_get_records(void *buffer, const nvn_recorded_command **out,
                                   size_t *count) {
    int found = 0;
    if (out) *out = NULL;
    if (count) *count = 0;
    if (!buffer || !out || !count) return 0;
    pthread_mutex_lock(&s_lock);
    {
        nvn_cb_buffer *slot = buffer_find(buffer, 0);
        if (slot && (slot->recording || slot->view_count)) {
            *out = slot->views;
            *count = slot->view_count;
            found = 1;
        }
    }
    pthread_mutex_unlock(&s_lock);
    return found;
}

int nvn_command_buffer_get_diagnostics(void *buffer, const nvn_diagnostic_command **out,
                                       size_t *count) {
    int found = 0;
    if (out) *out = NULL;
    if (count) *count = 0;
    if (!buffer || !out || !count) return 0;
    pthread_mutex_lock(&s_lock);
    {
        nvn_cb_buffer *slot = buffer_find(buffer, 0);
        if (slot && slot->diagnostic_count) {
            *out = slot->diagnostics;
            *count = slot->diagnostic_count;
            found = 1;
        }
    }
    pthread_mutex_unlock(&s_lock);
    return found;
}

int nvn_command_buffer_get_package(uint64_t handle, nvn_command_package *out) {
    int found = 0;
    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    pthread_mutex_lock(&s_lock);
    {
        nvn_cb_package *package = package_find(handle);
        if (package) {
            out->handle = package->handle;
            out->records = package->bytes;
            out->bytes = package->bytes_used;
            out->record_count = package->record_count;
            out->segment_count = package->segment_count;
            out->segments = package->segments;
            found = 1;
        }
    }
    pthread_mutex_unlock(&s_lock);
    return found;
}

void nvn_command_buffer_get_stats(nvn_command_buffer_stats *out) {
    size_t i;
    if (!out) return;
    pthread_mutex_lock(&s_lock);
    *out = s_stats;
    out->live_buffers = 0;
    out->records = 0;
    out->host_segment_bytes = s_pool_bytes;
    out->host_segment_bound = (uint64_t)NVN_CB_MAX_HOSTSEGMENTS * NVN_CB_HOSTSEG_BYTES;
    for (i = 0; i < NVN_CB_MAX_BUFFERS; i++) {
        if (!s_buffers[i].in_use) continue;
        if (!s_buffers[i].finalized) out->live_buffers++;
        out->records += (int)s_buffers[i].view_count;
    }
    pthread_mutex_unlock(&s_lock);
}
