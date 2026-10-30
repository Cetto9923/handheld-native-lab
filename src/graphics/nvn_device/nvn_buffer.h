/* The NVN buffer family of the FFRS RG477M native runtime.
 *
 * A buffer is what the guest makes out of a buffer builder: the guest fills a
 * builder with the storage it wants (a memory pool and an offset inside it),
 * hands it to nvnBufferInitialize, and from then on the buffer is the object
 * its draws refer to. The guest reads the description of a buffer back through
 * the getters of this family, and it maps the storage of a buffer to write
 * vertices and indices into it.
 *
 * The state lives on the host and is keyed by the address of the guest's
 * buffer, exactly as the queue, the window, the memory pool, the pool
 * families, the blend state, the render state, the sampler families and the
 * texture families do. Nothing is ever written into the guest's buffer object
 * or into the guest's builder object: the layout of an NVNbuffer and of an
 * NVNbufferBuilder is a property of the Nintendo SDK, which this runtime does
 * not have. The contract proves that with a 0xAA sentinel over the guest's own
 * memory.
 *
 * A buffer is a snapshot of the builder. The description is copied at the
 * moment of nvnBufferInitialize; a later change of the builder does not change
 * a buffer that was already made, and every getter answers with the buffer's
 * own values.
 *
 * The storage of a buffer belongs to a memory pool the guest made, so the
 * address this family hands out is the guest's own address: it is the address
 * of the pool's memory plus the offset of the buffer inside it, the way the
 * reference host answers (hle_nvn.c:1779). An address of the guest is what the
 * command buffer records, and the replay resolves such an address back into
 * the host's memory; an address invented here would not resolve.
 *
 * Where the evidence comes from:
 *
 *   - the signatures of the entry points are the ones of the vendored NVN
 *     header (src/graphics/nvn/include/nvn/nvn.h:1234-1251). That header is a
 *     reconstruction of NVN 53.105 taken from another game (Darksiders II), so
 *     the signatures are NOT confirmed for this game.
 *   - the semantics are the ones of the reference host that runs this very
 *     game (.local/refs/nxnative/dm/gfx/nvn/hle_nvn.c, MIT). The names that
 *     the reference host does not have are named in the report of this family
 *     and are not claimed to agree with it.
 */
#ifndef FFRS_NVN_BUFFER_H
#define FFRS_NVN_BUFFER_H

#include <stddef.h>
#include <stdint.h>

/* How many buffers the runtime tracks at the same time. The guest keeps its
 * buffers in a pool, so a handful is enough; a guest that wants more is told
 * so instead of being served silently. */
#define NVN_BUFFER_MAX_BUFFERS 64

/* How many buffer builders the runtime tracks at the same time. The guest
 * makes a builder, fills it, initializes a buffer with it and reuses the
 * builder for the next buffer, so a few are enough. */
#define NVN_BUFFER_MAX_BUILDERS 16

/* The eighteen names of this family. The builder's seven names and the
 * buffer's eleven names live in one table, because the guest resolves them as
 * one group and the coordinator wires one family block for them. */
typedef struct nvn_buffer_family_entry {
    const char *name;
    void *fn;
} nvn_buffer_family_entry;

size_t nvn_buffer_family_count(void);
const nvn_buffer_family_entry *nvn_buffer_family_entry_at(size_t index);

/* Statistics for the contract. */
typedef struct nvn_buffer_stats {
    uint64_t calls;            /* every entry point of the buffer family */
    uint64_t builder_calls;    /* every entry point of the builder part */
    uint64_t initialized;      /* a buffer was made out of a builder */
    uint64_t finalized;        /* a buffer was finalized */
    uint64_t gets;             /* a getter ran */
    uint64_t maps;             /* a buffer was mapped */
    uint64_t flushes;          /* a mapped range was flushed */
    uint64_t invalidates;      /* a mapped range was invalidated */
    uint64_t refused;          /* the call could not be answered */
    uint64_t addresses;        /* a real address was handed out */
    int live_buffers;          /* buffers the guest has initialized */
    int live_builders;         /* builders the guest has prepared */
} nvn_buffer_stats;

void nvn_buffer_get_stats(nvn_buffer_stats *out);

#endif /* FFRS_NVN_BUFFER_H */
