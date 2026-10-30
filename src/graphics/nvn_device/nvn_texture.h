/* The NVN texture object family of the FFRS RG477M native runtime.
 *
 * A texture is what the guest makes out of a texture builder: the guest fills a
 * builder, hands it to nvnTextureInitialize, and from then on the texture is the
 * object its draws refer to. The guest reads the description of a texture back
 * through the getters of this family, and it compares two textures with
 * nvnTextureCompare to keep its texture pool free of duplicates.
 *
 * The state lives on the host and is keyed by the address of the guest's
 * texture, exactly as the queue, the window, the memory pool, the pool
 * families, the blend state, the render state and the sampler families do.
 * Nothing is ever written into the guest's texture object: the layout of an
 * NVNtexture is a property of the Nintendo SDK, which this runtime does not
 * have. The contract proves that with a 0xAA sentinel over the guest's own
 * memory.
 *
 * A texture is a snapshot of the builder. The description is copied at the
 * moment of nvnTextureInitialize; a later change of the builder does not change
 * a texture that was already made, and every getter answers with the texture's
 * own values.
 *
 * The family also holds the three device handle functions, because a handle is
 * the number of a descriptor inside a pool and not a pointer:
 * nvnDeviceGetTextureHandle packs a texture number and a sampler number into
 * one word, and nvnDeviceGetImageHandle and nvnDeviceGetSeparateTextureHandle
 * hand out the number alone. The numbering base of a pool is NOT added here:
 * the base is added where the handle is read, not where it is made.
 *
 * Where the evidence comes from:
 *
 *   - the signatures of the entry points are the ones of the vendored NVN
 *     header (src/graphics/nvn/include/nvn/nvn.h:1320-1360 for the texture,
 *     1300-1319 for the view and 1130, 1132, 1151 for the three handles).
 *     That header is a reconstruction of NVN 53.105 taken from another game
 *     (Darksiders II), so the signatures are NOT confirmed for this game.
 *   - the semantics are the ones of the reference host that runs this very
 *     game (.local/refs/nxnative/dm/gfx/nvn/hle_nvn.c, MIT). The names that
 *     the reference host does not have are named in the report of this family
 *     and are not claimed to agree with it.
 */
#ifndef FFRS_NVN_TEXTURE_H
#define FFRS_NVN_TEXTURE_H

#include <stddef.h>
#include <stdint.h>

/* How many textures the runtime tracks at the same time.
 *
 * The table grows with the number of textures the guest has, so this is a
 * limit and not a guess: it is the number of textures the device allows
 * (MAX_TEXTURE_POOL_SIZE is 1048576), so a guest that runs out of them is told
 * so instead of being served silently. */
#define NVN_TEXTURE_MAX_OBJECTS 1048576

typedef struct nvn_texture_family_entry {
    const char *name;
    void *fn;
} nvn_texture_family_entry;

size_t nvn_texture_family_count(void);
const nvn_texture_family_entry *nvn_texture_family_entry_at(size_t index);

/* The three device handle functions. They are a family of their own, because
 * they belong to the device and not to the texture object, but they live in
 * this file because the coordinator keeps the write permission of the device
 * layer. */
typedef struct nvn_device_handle_family_entry {
    const char *name;
    void *fn;
} nvn_device_handle_family_entry;

size_t nvn_device_handle_family_count(void);
const nvn_device_handle_family_entry *nvn_device_handle_family_entry_at(size_t index);

/* Statistics for the contract. */
typedef struct nvn_texture_stats {
    uint64_t calls;          /* every entry point of the texture family */
    uint64_t initialized;    /* a texture was made out of a builder */
    uint64_t finalized;      /* a texture was finalized */
    uint64_t gets;           /* a getter ran */
    uint64_t compares;       /* two textures were compared */
    uint64_t texel_calls;    /* a texel entry point ran */
    uint64_t refused;        /* the call could not be answered */
    uint64_t handle_calls;   /* one of the three device handle functions ran */
    int live_textures;       /* textures the guest has initialized */
} nvn_texture_stats;

void nvn_texture_get_stats(nvn_texture_stats *out);

#endif /* FFRS_NVN_TEXTURE_H */
