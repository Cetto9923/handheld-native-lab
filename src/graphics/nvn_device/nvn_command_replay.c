/* The consumer of the NVN command packages of this runtime.
 *
 * See nvn_command_replay.h for what this layer is and is not.
 *
 * The rules it follows come from the reference that runs this game:
 * .local/refs/nxnative/dm/gfx/nvn/nvn_obj.h holds the record format and
 * .local/refs/nxnative/dm/gfx/nvn/hle_nvn.c holds the semantics of every
 * opcode. Two of them matter most here and both are easy to get wrong:
 *
 *   - a texture handle is not a pointer. It is the texture's number in the
 *     low twenty bits and the sampler's number above them, and the numbering
 *     bases of the descriptor pools are added to them when the handle is
 *     read. A handle that is treated as a pointer is a wild pointer.
 *   - the addresses in a bind or a copy are the guest's buffer addresses,
 *     never host pointers. They are turned back into host memory through the
 *     pool they belong to, and a range that runs past the end of the pool is
 *     refused here and not left to the driver.
 */
#include "nvn_command_replay.h"

#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include "nvn_memory_pool.h"

/* The size of the record of an opcode, as the range the record may take.
 *
 * The reference allocates the records that carry an array by the number of
 * elements that are really in them, and a draw that has no indirect
 * arguments stops before the fields that hold them. The reference says so:
 * "Binding records with arrays are allocated by the actual count: prefix
 * fields + n elements (the replay side reads only the first n)." So a fixed
 * size would refuse records that are perfectly good, and the smallest size
 * is the one that has to be there.
 *
 * The range is the size of the whole record: the header plus the payload. */
typedef struct nvn_cb_size_range {
    size_t min;
    size_t max; /* zero means that the record says how long it is */
} nvn_cb_size_range;

static nvn_cb_size_range record_size_of(uint32_t op) {
    nvn_cb_size_range range;
    range.min = 0;
    range.max = 0;
    switch (op) {
    case NVN_CB_OP_END: range.min = range.max = sizeof(nvn_cb_op_hdr); break;
    case NVN_CB_OP_CALL: range.min = range.max = sizeof(nvn_cb_op_hdr) + sizeof(uint64_t); break;
    case NVN_CB_OP_SIGNAL_EVENT: range.min = range.max = sizeof(nvn_cb_op_hdr) + 16u; break;
    case NVN_CB_OP_REPORT_COUNTER: range.min = range.max = sizeof(nvn_cb_op_hdr) + 16u; break;
    case NVN_CB_OP_RESET_COUNTER: range.min = range.max = sizeof(nvn_cb_op_hdr) + 8u; break;
    case NVN_CB_OP_FENCE_SYNC: range.min = range.max = sizeof(nvn_cb_op_hdr) + 8u; break;
    case NVN_CB_OP_BARRIER: range.min = range.max = sizeof(nvn_cb_op_hdr) + sizeof(int32_t); break;
    case NVN_CB_OP_COPY_B2B: range.min = range.max = sizeof(nvn_cb_op_hdr) + 3u * sizeof(uint64_t); break;
    /* The length of the payload of an update is in the record itself. */
    case NVN_CB_OP_UPDATE_UB: range.min = sizeof(nvn_cb_op_hdr) + 16u; range.max = 0; break;
    case NVN_CB_OP_SET_RT: range.min = range.max = sizeof(nvn_cb_op_hdr) + sizeof(nvn_cb_p_set_rt); break;
    case NVN_CB_OP_CLEAR_COLOR: range.min = range.max = sizeof(nvn_cb_op_hdr) + sizeof(nvn_cb_p_clear_color); break;
    case NVN_CB_OP_CLEAR_DS: range.min = range.max = sizeof(nvn_cb_op_hdr) + sizeof(nvn_cb_p_clear_ds); break;
    case NVN_CB_OP_RENDER_COND: range.min = range.max = sizeof(nvn_cb_op_hdr) + sizeof(nvn_cb_p_render_cond); break;
    case NVN_CB_OP_VIEWPORTS: range.min = range.max = sizeof(nvn_cb_op_hdr) + sizeof(nvn_cb_p_viewports); break;
    case NVN_CB_OP_SCISSOR: range.min = range.max = sizeof(nvn_cb_op_hdr) + sizeof(nvn_cb_p_scissor); break;
    case NVN_CB_OP_DEPTH_RANGE: range.min = range.max = sizeof(nvn_cb_op_hdr) + sizeof(nvn_cb_p_depth_range); break;
    case NVN_CB_OP_PRIM_RESTART: range.min = range.max = sizeof(nvn_cb_op_hdr) + sizeof(nvn_cb_p_prim_restart); break;
    case NVN_CB_OP_DEPTH_CLAMP: range.min = range.max = sizeof(nvn_cb_op_hdr) + sizeof(nvn_cb_p_depth_clamp); break;
    case NVN_CB_OP_VP_SWIZZLE: range.min = range.max = sizeof(nvn_cb_op_hdr) + sizeof(nvn_cb_p_vp_swizzle); break;
    case NVN_CB_OP_BIND_PROGRAM: range.min = range.max = sizeof(nvn_cb_op_hdr) + sizeof(nvn_cb_p_bind_program); break;
    case NVN_CB_OP_BIND_STATE: range.min = range.max = sizeof(nvn_cb_op_hdr) + sizeof(nvn_cb_p_bind_state); break;
    /* The ones with an array: the reference allocates them by the count. */
    case NVN_CB_OP_BIND_VATTR:
        range.min = sizeof(nvn_cb_op_hdr) + offsetof(nvn_cb_p_bind_vattr, st);
        range.max = sizeof(nvn_cb_op_hdr) + sizeof(nvn_cb_p_bind_vattr);
        break;
    case NVN_CB_OP_BIND_VSTREAM:
        range.min = sizeof(nvn_cb_op_hdr) + offsetof(nvn_cb_p_bind_vstream, st);
        range.max = sizeof(nvn_cb_op_hdr) + sizeof(nvn_cb_p_bind_vstream);
        break;
    case NVN_CB_OP_BIND_VBUF:
        range.min = sizeof(nvn_cb_op_hdr) + offsetof(nvn_cb_p_bind_vbuf, r);
        range.max = sizeof(nvn_cb_op_hdr) + sizeof(nvn_cb_p_bind_vbuf);
        break;
    case NVN_CB_OP_BIND_TEX:
    case NVN_CB_OP_BIND_SAMP:
    case NVN_CB_OP_BIND_IMG:
        range.min = sizeof(nvn_cb_op_hdr) + offsetof(nvn_cb_p_bind_handles, h);
        range.max = sizeof(nvn_cb_op_hdr) + sizeof(nvn_cb_p_bind_handles);
        break;
    case NVN_CB_OP_BIND_UBO: range.min = range.max = sizeof(nvn_cb_op_hdr) + sizeof(nvn_cb_p_bind_buf); break;
    case NVN_CB_OP_BIND_SSBO: range.min = range.max = sizeof(nvn_cb_op_hdr) + sizeof(nvn_cb_p_bind_buf); break;
    case NVN_CB_OP_BIND_TEX1: range.min = range.max = sizeof(nvn_cb_op_hdr) + sizeof(nvn_cb_p_bind_tex1); break;
    case NVN_CB_OP_STENCIL: range.min = range.max = sizeof(nvn_cb_op_hdr) + sizeof(nvn_cb_p_stencil); break;
    case NVN_CB_OP_POLY_OFFSET: range.min = range.max = sizeof(nvn_cb_op_hdr) + sizeof(nvn_cb_p_poly_offset); break;
    /* A draw without indirect arguments stops before the fields that hold
     * them, exactly as the reference does. */
    case NVN_CB_OP_DRAW:
        range.min = sizeof(nvn_cb_op_hdr) + offsetof(nvn_cb_p_draw, ind);
        range.max = sizeof(nvn_cb_op_hdr) + sizeof(nvn_cb_p_draw);
        break;
    case NVN_CB_OP_DRAW_IDX:
        range.min = sizeof(nvn_cb_op_hdr) + offsetof(nvn_cb_p_draw_idx, ind);
        range.max = sizeof(nvn_cb_op_hdr) + sizeof(nvn_cb_p_draw_idx);
        break;
    case NVN_CB_OP_DISPATCH: range.min = range.max = sizeof(nvn_cb_op_hdr) + sizeof(nvn_cb_p_dispatch); break;
    case NVN_CB_OP_SET_TEX_POOL: range.min = range.max = sizeof(nvn_cb_op_hdr) + sizeof(nvn_cb_p_set_pool); break;
    case NVN_CB_OP_SET_SAMP_POOL: range.min = range.max = sizeof(nvn_cb_op_hdr) + sizeof(nvn_cb_p_set_pool); break;
    case NVN_CB_OP_COPY_B2T: range.min = range.max = sizeof(nvn_cb_op_hdr) + sizeof(nvn_cb_p_copy_b2t); break;
    case NVN_CB_OP_COPY_T2B: range.min = range.max = sizeof(nvn_cb_op_hdr) + sizeof(nvn_cb_p_copy_t2t); break;
    case NVN_CB_OP_COPY_T2T: range.min = range.max = sizeof(nvn_cb_op_hdr) + sizeof(nvn_cb_p_copy_t2t); break;
    case NVN_CB_OP_COPY_STRIDE: range.min = range.max = sizeof(nvn_cb_op_hdr) + sizeof(nvn_cb_p_copy_stride); break;
    case NVN_CB_OP_CLEAR_TEX: range.min = range.max = sizeof(nvn_cb_op_hdr) + sizeof(nvn_cb_p_clear_tex); break;
    case NVN_CB_OP_CLEAR_BUF: range.min = range.max = sizeof(nvn_cb_op_hdr) + sizeof(nvn_cb_p_clear_buf); break;
    case NVN_CB_OP_BLEND_COLOR: range.min = range.max = sizeof(nvn_cb_op_hdr) + sizeof(nvn_cb_p_blend_color); break;
    case NVN_CB_OP_TF_BIND: range.min = range.max = sizeof(nvn_cb_op_hdr) + sizeof(nvn_cb_p_tf_bind); break;
    case NVN_CB_OP_TF_STATE: range.min = range.max = sizeof(nvn_cb_op_hdr) + sizeof(nvn_cb_p_tf_state); break;
    case NVN_CB_OP_RAST_DISCARD: range.min = range.max = sizeof(nvn_cb_op_hdr) + sizeof(nvn_cb_p_rast_discard); break;
    default: break;
    }
    return range;
}

static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static nvn_replay_state s_state;
static nvn_replay_stats s_stats;

/* The host memory behind a range of the guest's buffer address.
 *
 * The address is the guest's, and it is never a host pointer. The pool it
 * belongs to is found, the bytes behind it are named, and the range has to
 * stay inside the pool: an address that belongs to no pool is refused by
 * name, and so is a range that runs past the end of the pool. */
static int resolve_range(uint64_t address, uint64_t length) {
    size_t available = 0;
    void *host;
    if (length == 0) return 1;
    host = nvn_memory_pool_resolve_address(address, &available);
    if (!host) {
        pthread_mutex_lock(&s_lock);
        s_stats.refused_no_pool++;
        pthread_mutex_unlock(&s_lock);
        fprintf(stderr, "[nvn-replay] the address 0x%" PRIx64 " belongs to no memory pool of the "
                        "guest; the command that used it was refused\n",
                address);
        return 0;
    }
    if (length > (uint64_t)available) {
        pthread_mutex_lock(&s_lock);
        s_stats.refused_out_of_range++;
        pthread_mutex_unlock(&s_lock);
        fprintf(stderr, "[nvn-replay] the range of %" PRIu64 " bytes at 0x%" PRIx64 " runs past the "
                        "end of the pool, which has %zu bytes left; the command was refused\n",
                length, address, available);
        return 0;
    }
    pthread_mutex_lock(&s_lock);
    s_stats.resolved_addresses++;
    s_stats.resolved_bytes += length;
    pthread_mutex_unlock(&s_lock);
    return 1;
}

/* The host memory behind an address of the guest's buffer address.
 *
 * Some records carry an address whose length this layer cannot know yet: the
 * bytes of a copy out of a texture depend on the format of that texture, and
 * the bytes of an index buffer depend on the index type of the draw. For
 * those, the address is resolved and the range is left open, because a range
 * that was guessed wrong would refuse a command that is in fact fine. */
static int resolve_address(uint64_t address) {
    size_t available = 0;
    void *host = nvn_memory_pool_resolve_address(address, &available);
    if (!host) {
        pthread_mutex_lock(&s_lock);
        s_stats.refused_no_pool++;
        pthread_mutex_unlock(&s_lock);
        fprintf(stderr, "[nvn-replay] the address 0x%" PRIx64 " belongs to no memory pool of the "
                        "guest; the command that used it was refused\n",
                address);
        return 0;
    }
    pthread_mutex_lock(&s_lock);
    s_stats.resolved_addresses++;
    pthread_mutex_unlock(&s_lock);
    return 1;
}

/* The texture and the sampler of a handle.
 *
 * A handle is the texture's number in the low twenty bits and the sampler's
 * number above them. The numbering bases of the descriptor pools are added
 * to them when the handle is read, so a handle without a pool bound cannot
 * be read and is refused by name. */
static int read_handle(uint64_t handle, uint32_t *texture, uint32_t *sampler) {
    uint32_t id = (uint32_t)(handle & 0xfffffu);
    uint32_t sid = (uint32_t)(handle >> 20);
    if (id && !s_state.tex_pool) {
        pthread_mutex_lock(&s_lock);
        s_stats.refused_foreign_pool++;
        pthread_mutex_unlock(&s_lock);
        fprintf(stderr, "[nvn-replay] the stream binds the texture %u but has no texture pool "
                        "bound; the handle was refused\n",
                id);
        return 0;
    }
    if (sid && !s_state.samp_pool) {
        pthread_mutex_lock(&s_lock);
        s_stats.refused_foreign_pool++;
        pthread_mutex_unlock(&s_lock);
        fprintf(stderr, "[nvn-replay] the stream binds the sampler %u but has no sampler pool "
                        "bound; the handle was refused\n",
                sid);
        return 0;
    }
    *texture = id;
    *sampler = sid;
    return 1;
}

static void decode_one(uint32_t op, const uint8_t *payload) {
    switch (op) {
    case NVN_CB_OP_SET_RT: {
        const nvn_cb_p_set_rt *record = (const nvn_cb_p_set_rt *)payload;
        uint32_t i;
        s_state.render_targets = 0;
        for (i = 0; i < 8; i++) {
            if (!record->color[i]) continue;
            s_state.render_target[s_state.render_targets++] = (uint64_t)(uintptr_t)record->color[i];
        }
        break;
    }
    case NVN_CB_OP_SET_TEX_POOL: {
        const nvn_cb_p_set_pool *record = (const nvn_cb_p_set_pool *)payload;
        s_state.tex_pool = (uint64_t)(uintptr_t)record->pool;
        break;
    }
    case NVN_CB_OP_SET_SAMP_POOL: {
        const nvn_cb_p_set_pool *record = (const nvn_cb_p_set_pool *)payload;
        s_state.samp_pool = (uint64_t)(uintptr_t)record->pool;
        break;
    }
    case NVN_CB_OP_BIND_PROGRAM: {
        const nvn_cb_p_bind_program *record = (const nvn_cb_p_bind_program *)payload;
        s_state.program = (uint64_t)(uintptr_t)record->prog;
        break;
    }
    case NVN_CB_OP_BIND_TEX1: {
        const nvn_cb_p_bind_tex1 *record = (const nvn_cb_p_bind_tex1 *)payload;
        uint32_t texture = 0;
        uint32_t sampler = 0;
        if (read_handle(record->h, &texture, &sampler)) {
            pthread_mutex_lock(&s_lock);
            if (texture) s_stats.textures_bound++;
            if (sampler) s_stats.samplers_bound++;
            pthread_mutex_unlock(&s_lock);
        }
        break;
    }
    case NVN_CB_OP_BIND_TEX:
    case NVN_CB_OP_BIND_SAMP:
    case NVN_CB_OP_BIND_IMG: {
        const nvn_cb_p_bind_handles *record = (const nvn_cb_p_bind_handles *)payload;
        uint32_t i;
        for (i = 0; i < (uint32_t)record->n && i < 32u; i++) {
            uint32_t texture = 0;
            uint32_t sampler = 0;
            if (!read_handle(record->h[i], &texture, &sampler)) continue;
            pthread_mutex_lock(&s_lock);
            if (texture) s_stats.textures_bound++;
            if (sampler) s_stats.samplers_bound++;
            pthread_mutex_unlock(&s_lock);
        }
        break;
    }
    case NVN_CB_OP_BIND_VBUF: {
        const nvn_cb_p_bind_vbuf *record = (const nvn_cb_p_bind_vbuf *)payload;
        uint32_t i;
        for (i = 0; i < (uint32_t)record->n && i < 16u; i++) {
            resolve_range(record->r[i].address, record->r[i].size);
        }
        break;
    }
    case NVN_CB_OP_BIND_UBO:
    case NVN_CB_OP_BIND_SSBO: {
        const nvn_cb_p_bind_buf *record = (const nvn_cb_p_bind_buf *)payload;
        resolve_range(record->addr, record->size);
        break;
    }
    case NVN_CB_OP_TF_BIND: {
        const nvn_cb_p_tf_bind *record = (const nvn_cb_p_tf_bind *)payload;
        resolve_range(record->addr, record->size);
        break;
    }
    case NVN_CB_OP_CLEAR_BUF: {
        const nvn_cb_p_clear_buf *record = (const nvn_cb_p_clear_buf *)payload;
        resolve_range(record->addr, record->size);
        break;
    }
    case NVN_CB_OP_COPY_B2B: {
        uint64_t source;
        uint64_t destination;
        uint64_t size;
        memcpy(&source, payload, sizeof(source));
        memcpy(&destination, payload + 8, sizeof(destination));
        memcpy(&size, payload + 16, sizeof(size));
        resolve_range(source, size);
        resolve_range(destination, size);
        break;
    }
    case NVN_CB_OP_COPY_B2T: {
        const nvn_cb_p_copy_b2t *record = (const nvn_cb_p_copy_b2t *)payload;
        /* The number of bytes of the source depends on the format of the
         * texture, which this layer does not know, so the address is
         * resolved and the range is left open. */
        resolve_address(record->src);
        break;
    }
    case NVN_CB_OP_DRAW: {
        const nvn_cb_p_draw *record = (const nvn_cb_p_draw *)payload;
        if (record->ind) resolve_address(record->ind);
        break;
    }
    case NVN_CB_OP_DRAW_IDX: {
        const nvn_cb_p_draw_idx *record = (const nvn_cb_p_draw_idx *)payload;
        if (record->ind) resolve_address(record->ind);
        if (record->ibuf) resolve_address(record->ibuf);
        break;
    }
    case NVN_CB_OP_DISPATCH: {
        const nvn_cb_p_dispatch *record = (const nvn_cb_p_dispatch *)payload;
        if (record->ind) resolve_address(record->ind);
        break;
    }
    case NVN_CB_OP_UPDATE_UB: {
        uint64_t destination;
        uint64_t length;
        memcpy(&destination, payload, sizeof(destination));
        memcpy(&length, payload + 8, sizeof(length));
        resolve_range(destination, length);
        break;
    }
    default:
        break;
    }
}

int nvn_command_replay_run(uint64_t handle) {
    nvn_command_package package;
    const uint8_t *at;
    size_t left;
    int ok = 1;

    memset(&s_state, 0, sizeof(s_state));
    pthread_mutex_lock(&s_lock);
    s_stats.packages++;
    pthread_mutex_unlock(&s_lock);

    /* The handle goes through the registry of the recorder. It is never
     * dereferenced: a handle the registry does not know is refused. */
    if (!nvn_command_buffer_get_package(handle, &package)) {
        pthread_mutex_lock(&s_lock);
        s_stats.refused_unknown_handle++;
        pthread_mutex_unlock(&s_lock);
        fprintf(stderr, "[nvn-replay] the handle 0x%" PRIx64 " is not a package of the runtime; "
                        "the package was not read\n",
                handle);
        return 0;
    }

    at = package.records;
    left = package.bytes;
    while (left >= sizeof(nvn_cb_op_hdr)) {
        nvn_cb_op_hdr header;
        size_t size;
        nvn_cb_size_range expected;
        const uint8_t *payload = at + sizeof(nvn_cb_op_hdr);
        memcpy(&header, at, sizeof(header));
        size = header.size;
        if (header.op >= NVN_CB_OP_COUNT) {
            pthread_mutex_lock(&s_lock);
            s_stats.refused_bad_record++;
            pthread_mutex_unlock(&s_lock);
            fprintf(stderr, "[nvn-replay] the record at %zu has the opcode %u, which the format "
                            "does not have; the package was not read further\n",
                    (size_t)(at - package.records), header.op);
            return 0;
        }
        if (size < sizeof(nvn_cb_op_hdr) || size > left || (size % 8u) != 0u) {
            pthread_mutex_lock(&s_lock);
            s_stats.refused_bad_record++;
            pthread_mutex_unlock(&s_lock);
            fprintf(stderr, "[nvn-replay] the record at %zu says it is %zu bytes long, which the "
                            "%zu bytes that are left do not hold; the package was not read further\n",
                    (size_t)(at - package.records), size, left);
            return 0;
        }
        expected = record_size_of(header.op);
        if (expected.max != 0u && (size < expected.min || size > expected.max)) {
            pthread_mutex_lock(&s_lock);
            s_stats.refused_bad_record++;
            pthread_mutex_unlock(&s_lock);
            fprintf(stderr, "[nvn-replay] the record of %s is %zu bytes long and the format takes "
                            "between %zu and %zu; the package was not read further\n",
                    nvn_command_buffer_opcode_name(header.op), size, expected.min, expected.max);
            return 0;
        }
        if (expected.max == 0u && size < expected.min) {
            pthread_mutex_lock(&s_lock);
            s_stats.refused_bad_record++;
            pthread_mutex_unlock(&s_lock);
            fprintf(stderr, "[nvn-replay] the record of %s is %zu bytes long and the format needs "
                            "at least %zu; the package was not read further\n",
                    nvn_command_buffer_opcode_name(header.op), size, expected.min);
            return 0;
        }
        /* The records that carry an array hold the count of the elements in
         * the record, and the record has to be long enough for them. */
        {
            size_t needed = 0;
            int32_t count = 0;
            switch (header.op) {
            case NVN_CB_OP_BIND_VATTR:
                count = ((const nvn_cb_p_bind_vattr *)payload)->n;
                needed = sizeof(nvn_cb_op_hdr) + offsetof(nvn_cb_p_bind_vattr, st) +
                         (size_t)(count > 0 ? count : 0) * sizeof(uint32_t);
                break;
            case NVN_CB_OP_BIND_VSTREAM:
                count = ((const nvn_cb_p_bind_vstream *)payload)->n;
                needed = sizeof(nvn_cb_op_hdr) + offsetof(nvn_cb_p_bind_vstream, st) +
                         (size_t)(count > 0 ? count : 0) * sizeof(nvn_cb_vs_st);
                break;
            case NVN_CB_OP_BIND_VBUF:
                count = ((const nvn_cb_p_bind_vbuf *)payload)->n;
                needed = sizeof(nvn_cb_op_hdr) + offsetof(nvn_cb_p_bind_vbuf, r) +
                         (size_t)(count > 0 ? count : 0) * sizeof(nvn_cb_buffer_range);
                break;
            case NVN_CB_OP_BIND_TEX:
            case NVN_CB_OP_BIND_SAMP:
            case NVN_CB_OP_BIND_IMG:
                count = ((const nvn_cb_p_bind_handles *)payload)->n;
                needed = sizeof(nvn_cb_op_hdr) + offsetof(nvn_cb_p_bind_handles, h) +
                         (size_t)(count > 0 ? count : 0) * sizeof(uint64_t);
                break;
            default:
                break;
            }
            if (needed != 0u && (needed > size || count < 0)) {
                pthread_mutex_lock(&s_lock);
                s_stats.refused_bad_record++;
                pthread_mutex_unlock(&s_lock);
                fprintf(stderr, "[nvn-replay] the record of %s says it holds %d elements but it is "
                                "%zu bytes long and %zu bytes are needed; the package was not read "
                                "further\n",
                        nvn_command_buffer_opcode_name(header.op), (int)count, size, needed);
                return 0;
            }
        }
        if (header.op == NVN_CB_OP_UPDATE_UB) {
            uint64_t length = 0;
            if (size < sizeof(nvn_cb_op_hdr) + 16u) {
                pthread_mutex_lock(&s_lock);
                s_stats.refused_bad_record++;
                pthread_mutex_unlock(&s_lock);
                fprintf(stderr, "[nvn-replay] the record of %s is too short to hold its length; "
                                "the package was not read further\n",
                        nvn_command_buffer_opcode_name(header.op));
                return 0;
            }
            memcpy(&length, at + sizeof(nvn_cb_op_hdr) + 8u, sizeof(length));
            if (length + 16u > size - sizeof(nvn_cb_op_hdr)) {
                pthread_mutex_lock(&s_lock);
                s_stats.refused_bad_record++;
                pthread_mutex_unlock(&s_lock);
                fprintf(stderr, "[nvn-replay] the record of %s says it updates %" PRIu64 " bytes but "
                                "the record is %zu bytes long; the package was not read further\n",
                        nvn_command_buffer_opcode_name(header.op), length, size);
                return 0;
            }
        }
        pthread_mutex_lock(&s_lock);
        s_stats.records++;
        s_stats.opcodes[header.op]++;
        pthread_mutex_unlock(&s_lock);
        switch (header.op) {
        case NVN_CB_OP_DRAW:
        case NVN_CB_OP_DRAW_IDX:
            pthread_mutex_lock(&s_lock);
            s_stats.draws++;
            pthread_mutex_unlock(&s_lock);
            break;
        case NVN_CB_OP_DISPATCH:
            pthread_mutex_lock(&s_lock);
            s_stats.dispatches++;
            pthread_mutex_unlock(&s_lock);
            break;
        case NVN_CB_OP_CLEAR_COLOR:
        case NVN_CB_OP_CLEAR_DS:
        case NVN_CB_OP_CLEAR_TEX:
        case NVN_CB_OP_CLEAR_BUF:
            pthread_mutex_lock(&s_lock);
            s_stats.clears++;
            pthread_mutex_unlock(&s_lock);
            break;
        default:
            break;
        }
        if (size > sizeof(nvn_cb_op_hdr)) decode_one(header.op, at + sizeof(nvn_cb_op_hdr));
        at += size;
        left -= size;
    }
    if (left != 0) {
        pthread_mutex_lock(&s_lock);
        s_stats.refused_bad_record++;
        pthread_mutex_unlock(&s_lock);
        fprintf(stderr, "[nvn-replay] the package ends with %zu bytes that are not a record; the "
                        "package was not read further\n",
                left);
        ok = 0;
    }
    return ok;
}

void nvn_command_replay_get_state(nvn_replay_state *out) {
    if (!out) return;
    pthread_mutex_lock(&s_lock);
    *out = s_state;
    pthread_mutex_unlock(&s_lock);
}

void nvn_command_replay_get_stats(nvn_replay_stats *out) {
    if (!out) return;
    pthread_mutex_lock(&s_lock);
    *out = s_stats;
    pthread_mutex_unlock(&s_lock);
}

void nvn_command_replay_reset(void) {
    pthread_mutex_lock(&s_lock);
    memset(&s_state, 0, sizeof(s_state));
    memset(&s_stats, 0, sizeof(s_stats));
    pthread_mutex_unlock(&s_lock);
}
