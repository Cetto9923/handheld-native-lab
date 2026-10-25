/* The NVN texture object family implementation.
 *
 * See nvn_texture.h for why the state lives on the host and what a texture is.
 *
 * The guest hands a texture builder to nvnTextureInitialize and from then on
 * the texture holds a copy of the builder's description. The copy is taken at
 * the moment of the initialization, so a texture is a snapshot: the builder can
 * change afterwards without touching the texture that was already made.
 *
 * The description is read out of the builder through the builder family's own
 * name table, the same way the guest reads it: the texture family asks the
 * builder family for the values and never touches the guest's builder memory.
 * The builder's getters answer with the defaults of the builder family for a
 * builder that was never prepared, and the texture family does not invent a
 * second notion of "prepared".
 *
 * The evidence for the values:
 *
 *   - The signatures of the entry points are the ones of the vendored NVN
 *     header (src/graphics/nvn/include/nvn/nvn.h:1320-1360), which is a
 *     reconstruction of NVN 53.105 taken from another game (Darksiders II).
 *     Its signatures are therefore NOT confirmed for this game, which is Unreal
 *     Engine 5.6.1. The reference host that runs this very game agrees with
 *     them (.local/refs/nxnative/dm/gfx/nvn/nvn_protos.h).
 *   - The description that is copied is the one of the reference host's
 *     tex_builder_t (hle_nvn.c:437): the target, the format, the flags, the
 *     three dimensions, the number of levels, the number of samples, the
 *     stride, the memory pool and the offset inside it, the swizzle and the
 *     depth stencil mode.
 *   - The defaults are the ones of the reference host (hle_nvn.c:280): the
 *     target 2D, width, height, depth and levels at 1, and the swizzle the
 *     identity R, G, B, A.
 *   - The comparison compares the description, the storage and the swizzle,
 *     which is what the reference compares (hle_nvn.c:492): the memory the
 *     reference compares is the resolved address of the storage, and here the
 *     storage of a texture is the pair of the memory pool and the offset inside
 *     it, so that pair takes its place.
 *   - The ZCull save area is the one of the reference host (hle_nvn.c:489):
 *     eight bytes per 32x32 tile and layer, rounded up to 256 bytes, and 256
 *     bytes for a texture without tiles. The reference host says in the same
 *     place that the hardware's formula is not known and that this one grows
 *     with the texture; this runtime uses the same formula and does not claim
 *     that it is the hardware's.
 *
 * Three of the answers this family gives are NOT the reference's answers,
 * because the reference computes them with a texture layout this runtime does
 * not have:
 *
 *   - nvnTextureGetStorageSize answers 0 and says so. The layout algorithm
 *     lives in the reference tree (nvn_tex.c, a port of deko3d's
 *     dk_image.cpp); a number invented here would be smaller than the real one
 *     and would let the guest place two textures on top of each other.
 *   - nvnTextureGetViewOffset answers 0 and says so, for the same reason: the
 *     offset of a level and the size of a layer are both layout.
 *   - nvnTextureGetSparseTileLayout does not fill the caller's structure and
 *     says so. The structure is opaque in the vendored header (a single dummy
 *     byte) and the reference host does not have this entry point at all.
 *
 * The texel entry points are refused by name as well: the reference reads and
 * writes the storage of a texture through the GPU, and this runtime has
 * neither a created texture nor the format table that says how many bytes a
 * block of a format takes. A copy that guessed the block size would put the
 * wrong bytes in the guest's pool.
 *
 * The three device handle functions answer with the number of the descriptor
 * inside a pool, the way the reference host does (hle_nvn.c:181, 183, 2059):
 * the texture number in the low twenty bits and the sampler number from bit
 * twenty upwards. The numbering base of a pool is NOT added here: the base is
 * added where the handle is read, not where it is made.
 */
#include "nvn_texture.h"

#include "nvn_device.h"

/* The vendored NVN header comes before the memory pool header: the memory
 * pool header defines the NVN_MEMORY_POOL_FLAGS_* macros, and the vendored
 * header has enumerators of the same names. */
#include "../nvn/include/nvn/nvn.h"

#include "nvn_memory_pool.h"
#include "nvn_texture_builder.h"

#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    void *texture;
    void *device;
    NVNformat format;
    NVNtextureTarget target;
    NVNtextureFlags flags;
    int width;
    int height;
    int depth;
    int levels;
    int samples;
    ptrdiff_t stride;
    void *memory_pool;
    ptrdiff_t memory_offset;
    NVNtextureSwizzle swizzle[4];
    NVNtextureDepthStencilMode depth_stencil_mode;
    size_t zcull_storage_size;
    uint64_t debug_id;
    char debug_label[64];
    int initialized;
    int in_use;
} nvn_texture_state;

/* The names of this family, in the order of the family table. That order is
 * the order the guest itself resolves the names in (the trap slots 26 to 71 of
 * the device run of 2026-10-25). The index is used for the "only the first
 * call is logged" bookkeeping. */
enum {
    TX_INITIALIZE,
    TX_FINALIZE,
    TX_SET_DEBUG_LABEL,
    TX_GET_DEBUG_ID,
    TX_GET_FLAGS,
    TX_GET_TARGET,
    TX_GET_WIDTH,
    TX_GET_HEIGHT,
    TX_GET_DEPTH,
    TX_GET_LEVELS,
    TX_GET_SAMPLES,
    TX_GET_FORMAT,
    TX_GET_STRIDE,
    TX_GET_MEMORY_POOL,
    TX_GET_MEMORY_OFFSET,
    TX_GET_TEXTURE_ADDRESS,
    TX_GET_VIEW_OFFSET,
    TX_GET_STORAGE_CLASS,
    TX_GET_RAW_STORAGE_CLASS,
    TX_GET_STORAGE_SIZE,
    TX_GET_ZCULL_STORAGE_SIZE,
    TX_GET_SWIZZLE,
    TX_GET_DEPTH_STENCIL_MODE,
    TX_GET_SPARSE_TILE_LAYOUT,
    TX_COMPARE,
    TX_READ_TEXELS,
    TX_READ_TEXELS_STRIDED,
    TX_WRITE_TEXELS,
    TX_WRITE_TEXELS_STRIDED,
    TX_FLUSH_TEXELS,
    TX_INVALIDATE_TEXELS,
    TX_NAME_COUNT
};

static pthread_mutex_t s_texture_lock = PTHREAD_MUTEX_INITIALIZER;
static nvn_texture_state s_textures[NVN_TEXTURE_MAX_TEXTURES];
static nvn_texture_stats s_stats;
static int s_logged[TX_NAME_COUNT];

/* The defaults of the reference tree (hle_nvn.c:280, 490). */
static nvn_texture_state default_state(void) {
    nvn_texture_state t;
    memset(&t, 0, sizeof(t));
    t.target = NVN_TEXTURE_TARGET_2D;
    t.width = 1;
    t.height = 1;
    t.depth = 1;
    t.levels = 1;
    t.swizzle[0] = NVN_TEXTURE_SWIZZLE_R;
    t.swizzle[1] = NVN_TEXTURE_SWIZZLE_G;
    t.swizzle[2] = NVN_TEXTURE_SWIZZLE_B;
    t.swizzle[3] = NVN_TEXTURE_SWIZZLE_A;
    t.depth_stencil_mode = NVN_TEXTURE_DEPTH_STENCIL_MODE_DEPTH;
    return t;
}

/* The texture state for one guest texture address. A texture that was never
 * initialized has no state to answer with, and the functions that need one say
 * so instead of inventing one. */
static nvn_texture_state *texture_find(void *texture, int create) {
    size_t i;
    nvn_texture_state *free_slot = NULL;
    if (!texture) return NULL;
    for (i = 0; i < NVN_TEXTURE_MAX_TEXTURES; i++) {
        if (s_textures[i].in_use) {
            if (s_textures[i].texture == texture) return &s_textures[i];
        } else if (!free_slot) {
            free_slot = &s_textures[i];
        }
    }
    if (!create || !free_slot) return NULL;
    *free_slot = default_state();
    free_slot->texture = texture;
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
/* Reading the builder and the memory pool                              */
/* ------------------------------------------------------------------ */

/* The builder family's own getters, resolved through the builder family's name
 * table. The texture family does not read the guest's builder memory and does
 * not reach into the builder family's tables either: it asks the builder
 * family for the values, the same way the guest asks it.
 *
 * The casts are the ones of the vendored header (nvn.h:1281-1293). Every one
 * of these enumerations has a member of 0x7FFFFFFF, so the underlying type of
 * each of them is int, and one shape of getter covers all of them. */
typedef int (*builder_get_int_fn)(void *);
typedef ptrdiff_t (*builder_get_offset_fn)(void *);
typedef void *(*builder_get_pointer_fn)(void *);
typedef void (*builder_get_swizzle_fn)(void *, NVNtextureSwizzle *, NVNtextureSwizzle *, NVNtextureSwizzle *,
                                       NVNtextureSwizzle *);

/* The entry point of the builder family, found by its name. */
static void *builder_entry(const char *name) {
    size_t i;
    size_t count = nvn_texture_builder_family_count();
    for (i = 0; i < count; i++) {
        const nvn_texture_builder_family_entry *entry = nvn_texture_builder_family_entry_at(i);
        if (entry && entry->name && strcmp(entry->name, name) == 0) return entry->fn;
    }
    return NULL;
}

/* The entry point of the memory pool family, found by its name. */
static void *memory_pool_entry(const char *name) {
    size_t i;
    size_t count = nvn_memory_pool_family_count();
    for (i = 0; i < count; i++) {
        const nvn_memory_pool_family_entry *entry = nvn_memory_pool_family_entry_at(i);
        if (entry && entry->name && strcmp(entry->name, name) == 0) return entry->fn;
    }
    return NULL;
}

/* The address the memory pool hands out for its storage. The reference host
 * answers with the address of the pool's memory (hle_nvn.c:478), and this
 * runtime's memory pool hands out an address of its own for the same purpose
 * (nvn_memory_pool_get_buffer_address). A pool that was never initialized has
 * no address, and the answer is then zero. */
static uint64_t memory_pool_buffer_address(const void *pool) {
    typedef uint64_t (*pool_get_buffer_address_fn)(const void *);
    pool_get_buffer_address_fn fn = (pool_get_buffer_address_fn)(uintptr_t)memory_pool_entry("nvnMemoryPoolGetBufferAddress");
    if (!fn || !pool) return 0;
    return fn(pool);
}

/* The ZCull save area of a texture of these dimensions (hle_nvn.c:489). */
static size_t zcull_size(int width, int height, int depth) {
    uint64_t tiles;
    if (width <= 0 || height <= 0) return 0x100;
    tiles = (uint64_t)((width + 31) / 32) * (uint64_t)((height + 31) / 32) * (uint64_t)(depth > 0 ? depth : 1);
    if (!tiles) return 0x100;
    return (size_t)((tiles * 8 + 0xff) & ~(uint64_t)0xff);
}

/* Take the description of the builder into the texture. */
static int snapshot_builder(nvn_texture_state *t, const void *builder) {
    builder_get_int_fn get_format = (builder_get_int_fn)(uintptr_t)builder_entry("nvnTextureBuilderGetFormat");
    builder_get_int_fn get_target = (builder_get_int_fn)(uintptr_t)builder_entry("nvnTextureBuilderGetTarget");
    builder_get_int_fn get_flags = (builder_get_int_fn)(uintptr_t)builder_entry("nvnTextureBuilderGetFlags");
    builder_get_int_fn get_width = (builder_get_int_fn)(uintptr_t)builder_entry("nvnTextureBuilderGetWidth");
    builder_get_int_fn get_height = (builder_get_int_fn)(uintptr_t)builder_entry("nvnTextureBuilderGetHeight");
    builder_get_int_fn get_depth = (builder_get_int_fn)(uintptr_t)builder_entry("nvnTextureBuilderGetDepth");
    builder_get_int_fn get_levels = (builder_get_int_fn)(uintptr_t)builder_entry("nvnTextureBuilderGetLevels");
    builder_get_int_fn get_samples = (builder_get_int_fn)(uintptr_t)builder_entry("nvnTextureBuilderGetSamples");
    builder_get_int_fn get_dsmode =
        (builder_get_int_fn)(uintptr_t)builder_entry("nvnTextureBuilderGetDepthStencilMode");
    builder_get_offset_fn get_stride = (builder_get_offset_fn)(uintptr_t)builder_entry("nvnTextureBuilderGetStride");
    builder_get_offset_fn get_memory_offset =
        (builder_get_offset_fn)(uintptr_t)builder_entry("nvnTextureBuilderGetMemoryOffset");
    builder_get_pointer_fn get_memory_pool =
        (builder_get_pointer_fn)(uintptr_t)builder_entry("nvnTextureBuilderGetMemoryPool");
    builder_get_swizzle_fn get_swizzle = (builder_get_swizzle_fn)(uintptr_t)builder_entry("nvnTextureBuilderGetSwizzle");
    void *device;

    if (!get_format || !get_target || !get_flags || !get_width || !get_height || !get_depth || !get_levels ||
        !get_samples || !get_dsmode || !get_stride || !get_memory_offset || !get_memory_pool || !get_swizzle) {
        return 0;
    }

    device = nvn_texture_builder_device_of(builder);
    t->device = device;
    t->format = (NVNformat)get_format((void *)(uintptr_t)builder);
    t->target = (NVNtextureTarget)get_target((void *)(uintptr_t)builder);
    t->flags = (NVNtextureFlags)(unsigned)get_flags((void *)(uintptr_t)builder);
    t->width = get_width((void *)(uintptr_t)builder);
    t->height = get_height((void *)(uintptr_t)builder);
    t->depth = get_depth((void *)(uintptr_t)builder);
    t->levels = get_levels((void *)(uintptr_t)builder);
    t->samples = get_samples((void *)(uintptr_t)builder);
    t->stride = get_stride((void *)(uintptr_t)builder);
    t->memory_pool = get_memory_pool((void *)(uintptr_t)builder);
    t->memory_offset = get_memory_offset((void *)(uintptr_t)builder);
    get_swizzle((void *)(uintptr_t)builder, &t->swizzle[0], &t->swizzle[1], &t->swizzle[2], &t->swizzle[3]);
    t->depth_stencil_mode = (NVNtextureDepthStencilMode)get_dsmode((void *)(uintptr_t)builder);
    t->zcull_storage_size = zcull_size(t->width, t->height, t->depth);
    return 1;
}

/* ------------------------------------------------------------------ */
/* nvnTextureInitialize                                                 */
/* ------------------------------------------------------------------ */

/* nvnTextureInitialize(texture, builder) -> boolean.
 *
 * The reference host copies the description of the builder into the texture
 * and answers true (hle_nvn.c:437). Here the copy goes into the host's own
 * table, keyed by the address of the texture, because the guest's texture
 * object has a layout this runtime does not know.
 *
 * The reference host releases the host texture of a texture object that is
 * initialized again without a Finalize (hle_nvn.c:438). This runtime has no
 * host texture, so there is nothing to release; the description of the new
 * builder replaces the old one all the same. */
int nvnTextureInitialize(void *texture, const void *builder) {
    nvn_texture_state taken = default_state();
    nvn_texture_state *slot = NULL;
    int ready = 0;
    int copied = 0;
    const char *why = NULL;
    int first = 0;

    /* The copy is taken before the lock of this family is held: the builder
     * family has its own lock, and the copy is the only thing this call needs
     * from it. */
    if (texture && builder) {
        taken.texture = texture;
        taken.in_use = 1;
        copied = snapshot_builder(&taken, builder);
    }

    pthread_mutex_lock(&s_texture_lock);
    s_stats.calls++;
    if (!texture) {
        why = "there is no texture to initialize";
    } else if (!builder) {
        why = "there is no builder to take the description from";
    } else if (!copied) {
        why = "the builder family does not hand out the getters this family needs";
    } else {
        slot = texture_find(texture, 1);
        if (!slot) {
            why = "no room for another texture";
        } else {
            /* The debug id comes from the device's own debug domain id
             * generator, the same one every other object of this runtime
             * uses. The generator hands out an id from the device's own
             * sequence and ignores its arguments. */
            taken.debug_id = nvn_device_generate_debug_domain_id(taken.device, "nvnTexture");
            taken.initialized = 1;
            *slot = taken;
            s_stats.initialized++;
            ready = 1;
        }
    }
    if (!ready) s_stats.refused++;
    first = mark_logged(TX_INITIALIZE);
    pthread_mutex_unlock(&s_texture_lock);

    if (!ready) {
        fprintf(stderr, "[nvn] nvnTextureInitialize: texture=%p builder=%p was refused: %s\n", texture, builder, why);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnTextureInitialize: texture=%p builder=%p -> the description of the builder is "
                        "copied into the texture's own state\n",
                texture, builder);
    }
    return ready ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* nvnTextureFinalize                                                   */
/* ------------------------------------------------------------------ */

/* nvnTextureFinalize(texture).
 *
 * The reference host drops the texture (hle_nvn.c:465). Here the state of the
 * texture is dropped, so that a getter on that address afterwards says that the
 * texture was never initialized instead of answering with the values of a
 * texture that does not exist any more. */
void nvnTextureFinalize(void *texture) {
    nvn_texture_state *found;
    int refused = 0;
    int first = 0;
    pthread_mutex_lock(&s_texture_lock);
    s_stats.calls++;
    found = texture_find(texture, 0);
    if (found) {
        found->initialized = 0;
        found->in_use = 0;
        s_stats.finalized++;
    } else {
        s_stats.refused++;
        refused = 1;
    }
    first = mark_logged(TX_FINALIZE);
    pthread_mutex_unlock(&s_texture_lock);
    if (refused) {
        fprintf(stderr, "[nvn] nvnTextureFinalize: texture=%p was never initialized, so there is nothing to "
                        "finalize\n",
                texture);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnTextureFinalize: texture=%p\n", texture);
    }
    NVN_VOID_RETURN();
}

/* ------------------------------------------------------------------ */
/* nvnTextureSetDebugLabel                                              */
/* ------------------------------------------------------------------ */

/* nvnTextureSetDebugLabel(texture, label).
 *
 * The reference host throws the label away (hle_nvn.c:1964). Here it is kept
 * on the host with the rest of the state of the texture, so that the log can
 * name the texture the guest is working with.
 *
 * The label is only recorded for a texture that exists, because a label on an
 * address the guest never initialized would take a slot of the table away from
 * a texture that does exist. The reference drops the label in every case, so
 * nothing the guest can observe changes. */
void nvnTextureSetDebugLabel(void *texture, const char *label) {
    nvn_texture_state *found;
    int refused = 0;
    int first = 0;
    pthread_mutex_lock(&s_texture_lock);
    s_stats.calls++;
    found = texture_find(texture, 0);
    if (found && found->initialized) {
        snprintf(found->debug_label, sizeof(found->debug_label), "%s", label ? label : "");
    } else {
        s_stats.refused++;
        refused = 1;
    }
    first = mark_logged(TX_SET_DEBUG_LABEL);
    pthread_mutex_unlock(&s_texture_lock);
    if (refused) {
        fprintf(stderr, "[nvn] nvnTextureSetDebugLabel: texture=%p was never initialized, so the label was not "
                        "recorded\n",
                texture);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnTextureSetDebugLabel: texture=%p \"%s\"\n", texture, label ? label : "");
    }
    NVN_VOID_RETURN();
}

/* ------------------------------------------------------------------ */
/* The getters                                                          */
/* ------------------------------------------------------------------ */

/* Every getter answers with the texture's own values. A texture that was never
 * initialized has none, so the getter says so and answers with the defaults:
 * a defined answer that the guest cannot mistake for a value of this texture,
 * because the log names the refusal. */

/* The state of a texture, copied out. The lock of this family must be held
 * while this runs. The copy is what the getter answers with, so that the
 * answer is a value and not a pointer into the table. */
static int texture_state_locked(const void *texture, nvn_texture_state *out) {
    nvn_texture_state *found = texture_find((void *)(uintptr_t)texture, 0);
    if (found && found->initialized) {
        if (out) *out = *found;
        return 1;
    }
    if (out) *out = default_state();
    return 0;
}

NVNtextureFlags nvnTextureGetFlags(const void *texture) {
    nvn_texture_state t;
    int refused = 0;
    int first = 0;
    pthread_mutex_lock(&s_texture_lock);
    s_stats.calls++;
    s_stats.gets++;
    refused = !texture_state_locked(texture, &t);
    first = mark_logged(TX_GET_FLAGS);
    pthread_mutex_unlock(&s_texture_lock);
    if (refused) {
        fprintf(stderr, "[nvn] nvnTextureGetFlags: texture=%p was never initialized; the answer is the default, not "
                        "a value of this texture\n",
                texture);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnTextureGetFlags: texture=%p flags=0x%x\n", texture, (unsigned)t.flags);
    }
    return t.flags;
}

NVNtextureTarget nvnTextureGetTarget(const void *texture) {
    nvn_texture_state t;
    int refused = 0;
    int first = 0;
    pthread_mutex_lock(&s_texture_lock);
    s_stats.calls++;
    s_stats.gets++;
    refused = !texture_state_locked(texture, &t);
    first = mark_logged(TX_GET_TARGET);
    pthread_mutex_unlock(&s_texture_lock);
    if (refused) {
        fprintf(stderr, "[nvn] nvnTextureGetTarget: texture=%p was never initialized; the answer is the default, not "
                        "a value of this texture\n",
                texture);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnTextureGetTarget: texture=%p target=0x%x\n", texture, (unsigned)t.target);
    }
    return t.target;
}

int nvnTextureGetWidth(const void *texture) {
    nvn_texture_state t;
    int refused = 0;
    int first = 0;
    pthread_mutex_lock(&s_texture_lock);
    s_stats.calls++;
    s_stats.gets++;
    refused = !texture_state_locked(texture, &t);
    first = mark_logged(TX_GET_WIDTH);
    pthread_mutex_unlock(&s_texture_lock);
    if (refused) {
        fprintf(stderr, "[nvn] nvnTextureGetWidth: texture=%p was never initialized; the answer is the default, not a "
                        "value of this texture\n",
                texture);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnTextureGetWidth: texture=%p width=%d\n", texture, t.width);
    }
    return t.width;
}

int nvnTextureGetHeight(const void *texture) {
    nvn_texture_state t;
    int refused = 0;
    int first = 0;
    pthread_mutex_lock(&s_texture_lock);
    s_stats.calls++;
    s_stats.gets++;
    refused = !texture_state_locked(texture, &t);
    first = mark_logged(TX_GET_HEIGHT);
    pthread_mutex_unlock(&s_texture_lock);
    if (refused) {
        fprintf(stderr, "[nvn] nvnTextureGetHeight: texture=%p was never initialized; the answer is the default, not "
                        "a value of this texture\n",
                texture);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnTextureGetHeight: texture=%p height=%d\n", texture, t.height);
    }
    return t.height;
}

int nvnTextureGetDepth(const void *texture) {
    nvn_texture_state t;
    int refused = 0;
    int first = 0;
    pthread_mutex_lock(&s_texture_lock);
    s_stats.calls++;
    s_stats.gets++;
    refused = !texture_state_locked(texture, &t);
    first = mark_logged(TX_GET_DEPTH);
    pthread_mutex_unlock(&s_texture_lock);
    if (refused) {
        fprintf(stderr, "[nvn] nvnTextureGetDepth: texture=%p was never initialized; the answer is the default, not a "
                        "value of this texture\n",
                texture);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnTextureGetDepth: texture=%p depth=%d\n", texture, t.depth);
    }
    return t.depth;
}

int nvnTextureGetLevels(const void *texture) {
    nvn_texture_state t;
    int refused = 0;
    int first = 0;
    pthread_mutex_lock(&s_texture_lock);
    s_stats.calls++;
    s_stats.gets++;
    refused = !texture_state_locked(texture, &t);
    first = mark_logged(TX_GET_LEVELS);
    pthread_mutex_unlock(&s_texture_lock);
    if (refused) {
        fprintf(stderr, "[nvn] nvnTextureGetLevels: texture=%p was never initialized; the answer is the default, not "
                        "a value of this texture\n",
                texture);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnTextureGetLevels: texture=%p levels=%d\n", texture, t.levels);
    }
    return t.levels;
}

int nvnTextureGetSamples(const void *texture) {
    nvn_texture_state t;
    int refused = 0;
    int first = 0;
    pthread_mutex_lock(&s_texture_lock);
    s_stats.calls++;
    s_stats.gets++;
    refused = !texture_state_locked(texture, &t);
    first = mark_logged(TX_GET_SAMPLES);
    pthread_mutex_unlock(&s_texture_lock);
    if (refused) {
        fprintf(stderr, "[nvn] nvnTextureGetSamples: texture=%p was never initialized; the answer is the default, not "
                        "a value of this texture\n",
                texture);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnTextureGetSamples: texture=%p samples=%d\n", texture, t.samples);
    }
    return t.samples;
}

NVNformat nvnTextureGetFormat(const void *texture) {
    nvn_texture_state t;
    int refused = 0;
    int first = 0;
    pthread_mutex_lock(&s_texture_lock);
    s_stats.calls++;
    s_stats.gets++;
    refused = !texture_state_locked(texture, &t);
    first = mark_logged(TX_GET_FORMAT);
    pthread_mutex_unlock(&s_texture_lock);
    if (refused) {
        fprintf(stderr, "[nvn] nvnTextureGetFormat: texture=%p was never initialized; the answer is the default, not "
                        "a value of this texture\n",
                texture);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnTextureGetFormat: texture=%p format=0x%x\n", texture, (unsigned)t.format);
    }
    return t.format;
}

ptrdiff_t nvnTextureGetStride(const void *texture) {
    nvn_texture_state t;
    int refused = 0;
    int first = 0;
    pthread_mutex_lock(&s_texture_lock);
    s_stats.calls++;
    s_stats.gets++;
    refused = !texture_state_locked(texture, &t);
    first = mark_logged(TX_GET_STRIDE);
    pthread_mutex_unlock(&s_texture_lock);
    if (refused) {
        fprintf(stderr, "[nvn] nvnTextureGetStride: texture=%p was never initialized; the answer is the default, not a "
                        "value of this texture\n",
                texture);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnTextureGetStride: texture=%p stride=%" PRIdPTR "\n", texture, t.stride);
    }
    return t.stride;
}

/* The memory pool the texture lives in.
 *
 * The vendored header says this entry point returns the NVNmemoryPool
 * structure by value (nvn.h:1356), but that structure is a 256 byte opaque
 * block in the header, while the reference host that runs this very game
 * returns a pointer to the pool (hle_nvn.c:475), and so does the builder
 * family of this runtime (nvnTextureBuilderGetMemoryPool). The pointer is
 * what the guest's call site expects, and that is what this family answers
 * with. */
void *nvnTextureGetMemoryPool(const void *texture) {
    nvn_texture_state t;
    int refused = 0;
    int first = 0;
    pthread_mutex_lock(&s_texture_lock);
    s_stats.calls++;
    s_stats.gets++;
    refused = !texture_state_locked(texture, &t);
    first = mark_logged(TX_GET_MEMORY_POOL);
    pthread_mutex_unlock(&s_texture_lock);
    if (refused) {
        fprintf(stderr, "[nvn] nvnTextureGetMemoryPool: texture=%p was never initialized; the answer is the default, "
                        "not a value of this texture\n",
                texture);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnTextureGetMemoryPool: texture=%p pool=%p\n", texture, t.memory_pool);
    }
    return t.memory_pool;
}

ptrdiff_t nvnTextureGetMemoryOffset(const void *texture) {
    nvn_texture_state t;
    int refused = 0;
    int first = 0;
    pthread_mutex_lock(&s_texture_lock);
    s_stats.calls++;
    s_stats.gets++;
    refused = !texture_state_locked(texture, &t);
    first = mark_logged(TX_GET_MEMORY_OFFSET);
    pthread_mutex_unlock(&s_texture_lock);
    if (refused) {
        fprintf(stderr, "[nvn] nvnTextureGetMemoryOffset: texture=%p was never initialized; the answer is the "
                        "default, not a value of this texture\n",
                texture);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnTextureGetMemoryOffset: texture=%p offset=%" PRIdPTR "\n", texture, t.memory_offset);
    }
    return t.memory_offset;
}

/* The address of the texture's storage.
 *
 * The reference host answers with the address of the pool's memory plus the
 * offset of the texture in it (hle_nvn.c:478). This runtime's memory pool
 * hands out an address of its own for the same purpose, so the answer is that
 * address plus the offset.
 *
 * A texture that has no memory pool, or whose pool was never initialized, has
 * no address; the reference answers with zero in that case too. */
NVNtextureAddress nvnTextureGetTextureAddress(const void *texture) {
    nvn_texture_state t;
    int refused = 0;
    int first = 0;
    NVNtextureAddress address = 0;
    pthread_mutex_lock(&s_texture_lock);
    s_stats.calls++;
    s_stats.gets++;
    refused = !texture_state_locked(texture, &t);
    first = mark_logged(TX_GET_TEXTURE_ADDRESS);
    pthread_mutex_unlock(&s_texture_lock);
    if (!refused) {
        uint64_t base = memory_pool_buffer_address(t.memory_pool);
        address = base ? (NVNtextureAddress)(base + (uint64_t)t.memory_offset) : 0;
    }
    if (refused) {
        fprintf(stderr, "[nvn] nvnTextureGetTextureAddress: texture=%p was never initialized; the answer is the "
                        "default, not a value of this texture\n",
                texture);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnTextureGetTextureAddress: texture=%p address=0x%" PRIx64 "\n", texture, address);
    }
    return address;
}

/* The offset of a view inside the storage of the texture.
 *
 * The reference host answers with the offset of the level of the view plus the
 * size of the layers before the first layer of the view (hle_nvn.c:507). Both
 * of those are the layout of the texture, and this runtime has no layout
 * algorithm, so the answer is zero and the log says why. */
ptrdiff_t nvnTextureGetViewOffset(const void *texture, const void *view) {
    nvn_texture_state t;
    int refused = 0;
    int first = 0;
    pthread_mutex_lock(&s_texture_lock);
    s_stats.calls++;
    s_stats.gets++;
    refused = !texture_state_locked(texture, &t);
    first = mark_logged(TX_GET_VIEW_OFFSET);
    pthread_mutex_unlock(&s_texture_lock);
    if (refused) {
        fprintf(stderr, "[nvn] nvnTextureGetViewOffset: texture=%p was never initialized; the answer is the default, "
                        "not a value of this texture\n",
                texture);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnTextureGetViewOffset: texture=%p view=%p -> 0; the offset of a level and the size "
                        "of a layer are the layout of the texture, and this runtime does not compute the layout\n",
                texture, view);
    }
    return 0;
}

/* The storage class of a texture.
 *
 * The reference host answers with zero: the host has no storage class of its
 * own (hle_nvn.c:1958), and the builder family of this runtime answers with
 * zero for the same reason. */
NVNstorageClass nvnTextureGetStorageClass(const void *texture) {
    nvn_texture_state t;
    int refused = 0;
    int first = 0;
    pthread_mutex_lock(&s_texture_lock);
    s_stats.calls++;
    s_stats.gets++;
    refused = !texture_state_locked(texture, &t);
    first = mark_logged(TX_GET_STORAGE_CLASS);
    pthread_mutex_unlock(&s_texture_lock);
    if (refused) {
        fprintf(stderr, "[nvn] nvnTextureGetStorageClass: texture=%p was never initialized; the answer is the "
                        "default, not a value of this texture\n",
                texture);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnTextureGetStorageClass: texture=%p storage_class=0\n", texture);
    }
    return 0;
}

/* The raw storage class of a texture.
 *
 * The vendored header has no signature for this entry point. The reference
 * host has one and answers with 1 (hle_nvn.c:497, the NVN_NOPROTO form: the
 * meaning is unverified and the reference returns 1 for now). The signature
 * and the value of the reference are used here, because the reference host is
 * the one that runs this very game; the call sites of the reference compare the
 * answer with 1. */
int nvnTextureGetRawStorageClass(const void *texture) {
    nvn_texture_state t;
    int refused = 0;
    int first = 0;
    pthread_mutex_lock(&s_texture_lock);
    s_stats.calls++;
    s_stats.gets++;
    refused = !texture_state_locked(texture, &t);
    first = mark_logged(TX_GET_RAW_STORAGE_CLASS);
    pthread_mutex_unlock(&s_texture_lock);
    if (refused) {
        fprintf(stderr, "[nvn] nvnTextureGetRawStorageClass: texture=%p was never initialized; the answer is 0\n",
                texture);
        return 0;
    }
    if (first) {
        fprintf(stderr, "[nvn] nvnTextureGetRawStorageClass: texture=%p raw_storage_class=1\n", texture);
    }
    return 1;
}

/* The size of the storage of a texture.
 *
 * The reference host answers with the size of the layout it computed
 * (hle_nvn.c:470). This runtime does not compute the layout, so the answer is
 * zero and the log says so: a number invented here would be smaller than the
 * real one and would let the guest place two textures on top of each other in
 * its memory pool. */
int nvnTextureGetStorageSize(const void *texture) {
    nvn_texture_state t;
    int refused = 0;
    int first = 0;
    pthread_mutex_lock(&s_texture_lock);
    s_stats.calls++;
    s_stats.gets++;
    refused = !texture_state_locked(texture, &t);
    first = mark_logged(TX_GET_STORAGE_SIZE);
    pthread_mutex_unlock(&s_texture_lock);
    if (refused) {
        fprintf(stderr, "[nvn] nvnTextureGetStorageSize: texture=%p was never initialized; the answer is the "
                        "default, not a value of this texture\n",
                texture);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnTextureGetStorageSize: texture=%p -> 0; the storage layout of a texture is not "
                        "computed by this family, so no size is invented here\n",
                texture);
    }
    return 0;
}

/* The size of the ZCull save area of a texture (hle_nvn.c:489). */
size_t nvnTextureGetZCullStorageSize(const void *texture) {
    nvn_texture_state t;
    int refused = 0;
    int first = 0;
    size_t size = 0;
    pthread_mutex_lock(&s_texture_lock);
    s_stats.calls++;
    s_stats.gets++;
    refused = !texture_state_locked(texture, &t);
    first = mark_logged(TX_GET_ZCULL_STORAGE_SIZE);
    pthread_mutex_unlock(&s_texture_lock);
    if (!refused) size = t.zcull_storage_size;
    if (refused) {
        fprintf(stderr, "[nvn] nvnTextureGetZCullStorageSize: texture=%p was never initialized; the answer is the "
                        "default, not a value of this texture\n",
                texture);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnTextureGetZCullStorageSize: texture=%p zcull=%zu\n", texture, size);
    }
    return size;
}

/* The channel mapping of a texture. The reference host answers with the
 * identity R, G, B, A when the builder was never given one (hle_nvn.c:1954). */
void nvnTextureGetSwizzle(const void *texture, NVNtextureSwizzle *red, NVNtextureSwizzle *green,
                           NVNtextureSwizzle *blue, NVNtextureSwizzle *alpha) {
    nvn_texture_state t;
    int refused = 0;
    int first = 0;
    pthread_mutex_lock(&s_texture_lock);
    s_stats.calls++;
    s_stats.gets++;
    refused = !texture_state_locked(texture, &t);
    first = mark_logged(TX_GET_SWIZZLE);
    pthread_mutex_unlock(&s_texture_lock);
    if (red) *red = t.swizzle[0];
    if (green) *green = t.swizzle[1];
    if (blue) *blue = t.swizzle[2];
    if (alpha) *alpha = t.swizzle[3];
    if (refused) {
        fprintf(stderr, "[nvn] nvnTextureGetSwizzle: texture=%p was never initialized; the answer is the default, not "
                        "a value of this texture\n",
                texture);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnTextureGetSwizzle: texture=%p swizzle=0x%x,0x%x,0x%x,0x%x\n", texture, (unsigned)t.swizzle[0],
                (unsigned)t.swizzle[1], (unsigned)t.swizzle[2], (unsigned)t.swizzle[3]);
    }
    NVN_VOID_RETURN();
}

/* The depth stencil mode of a texture (hle_nvn.c:2039). */
NVNtextureDepthStencilMode nvnTextureGetDepthStencilMode(const void *texture) {
    nvn_texture_state t;
    int refused = 0;
    int first = 0;
    pthread_mutex_lock(&s_texture_lock);
    s_stats.calls++;
    s_stats.gets++;
    refused = !texture_state_locked(texture, &t);
    first = mark_logged(TX_GET_DEPTH_STENCIL_MODE);
    pthread_mutex_unlock(&s_texture_lock);
    if (refused) {
        fprintf(stderr, "[nvn] nvnTextureGetDepthStencilMode: texture=%p was never initialized; the answer is the "
                        "default, not a value of this texture\n",
                texture);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnTextureGetDepthStencilMode: texture=%p mode=0x%x\n", texture,
                (unsigned)t.depth_stencil_mode);
    }
    return t.depth_stencil_mode;
}

/* The sparse tile layout of a texture.
 *
 * The vendored header has the signature of this entry point (nvn.h:1340), but
 * the reference host does not have this entry point at all, and the structure
 * it would fill is opaque in the header (a single dummy byte). The structure
 * of the caller is therefore left alone and the log says why. */
void nvnTextureGetSparseTileLayout(const void *texture, NVNtextureSparseTileLayout *layout) {
    int refused = 0;
    int first = 0;
    (void)layout;
    pthread_mutex_lock(&s_texture_lock);
    s_stats.calls++;
    s_stats.gets++;
    refused = !texture_state_locked(texture, NULL);
    first = mark_logged(TX_GET_SPARSE_TILE_LAYOUT);
    pthread_mutex_unlock(&s_texture_lock);
    if (refused) {
        fprintf(stderr, "[nvn] nvnTextureGetSparseTileLayout: texture=%p was never initialized; the layout is not "
                        "filled in\n",
                texture);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnTextureGetSparseTileLayout: texture=%p; the layout is not filled in, because the "
                        "structure of a sparse tile layout is opaque in the header this runtime was built against\n",
                texture);
    }
    NVN_VOID_RETURN();
}

/* ------------------------------------------------------------------ */
/* nvnTextureCompare                                                    */
/* ------------------------------------------------------------------ */

/* nvnTextureCompare(a, b) -> boolean.
 *
 * Whether two textures have the same description. The guest uses it to keep
 * its texture pool free of duplicates (the reference host compares the whole
 * description, the storage and the swizzle at hle_nvn.c:492).
 *
 * The fields that take part are exactly the ones the reference compares: the
 * target, the format, the flags, the three dimensions, the number of levels,
 * the number of samples, the stride, the storage and the swizzle. The memory
 * the reference compares is the resolved address of the storage, and here the
 * storage of a texture is the pair of the memory pool and the offset inside
 * it, so that pair takes its place.
 *
 * The debug label and the debug id are not part of the state of a texture
 * either. */
int nvnTextureCompare(const void *a, const void *b) {
    nvn_texture_state x = default_state();
    nvn_texture_state y = default_state();
    int have_x = 0;
    int have_y = 0;
    int equal = 0;
    int first = 0;
    pthread_mutex_lock(&s_texture_lock);
    s_stats.calls++;
    s_stats.compares++;
    have_x = texture_state_locked(a, &x);
    have_y = texture_state_locked(b, &y);
    if (!have_x || !have_y) s_stats.refused++;
    first = mark_logged(TX_COMPARE);
    pthread_mutex_unlock(&s_texture_lock);

    if (!have_x || !have_y) {
        fprintf(stderr, "[nvn] nvnTextureCompare: %s was never initialized; two textures can only be compared when "
                        "both of them exist, so the answer is false\n",
                !have_x ? "the first texture" : "the second texture");
        return 0;
    }
    equal = x.format == y.format && x.target == y.target && x.flags == y.flags && x.width == y.width &&
            x.height == y.height && x.depth == y.depth && x.levels == y.levels && x.samples == y.samples &&
            x.stride == y.stride && x.memory_pool == y.memory_pool && x.memory_offset == y.memory_offset &&
            !memcmp(x.swizzle, y.swizzle, sizeof(x.swizzle));
    if (first) {
        fprintf(stderr, "[nvn] nvnTextureCompare: a=%p b=%p -> %s\n", a, b, equal ? "the same" : "not the same");
    }
    return equal;
}

/* ------------------------------------------------------------------ */
/* nvnTextureGetDebugID                                                 */
/* ------------------------------------------------------------------ */

/* nvnTextureGetDebugID(texture) -> uint64_t.
 *
 * The vendored header has the signature of this entry point (nvn.h:1360), but
 * the reference host does not have it at all, so the reference gives no value
 * for it and none is claimed here.
 *
 * The answer is the id this runtime hands out for the object, taken from the
 * device's debug domain id generator (nvn_device_generate_debug_domain_id),
 * which is the same source every other object of this runtime uses for its
 * debug ids. A texture that was never initialized has no id, so the answer is
 * zero and the log says that the texture does not exist. */
uint64_t nvnTextureGetDebugID(const void *texture) {
    nvn_texture_state t;
    uint64_t debug_id = 0;
    int refused = 0;
    int first = 0;
    pthread_mutex_lock(&s_texture_lock);
    s_stats.calls++;
    s_stats.gets++;
    refused = !texture_state_locked(texture, &t);
    if (!refused) debug_id = t.debug_id;
    first = mark_logged(TX_GET_DEBUG_ID);
    pthread_mutex_unlock(&s_texture_lock);
    if (refused) {
        fprintf(stderr, "[nvn] nvnTextureGetDebugID: texture=%p was never initialized, so it has no debug id; the "
                        "answer is 0\n",
                texture);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnTextureGetDebugID: texture=%p debug_id=0x%" PRIx64 "\n", texture, debug_id);
    }
    return debug_id;
}

/* ------------------------------------------------------------------ */
/* The texel entry points                                               */
/* ------------------------------------------------------------------ */

/* The texel entry points read and write the storage of a texture. The
 * reference host does that through the GPU, with the block size of the format
 * (hle_nvn.c:523, 2320). This runtime has neither a created texture nor the
 * format table that says how many bytes a block of a format takes, so the
 * calls are refused and named: a copy that guessed the block size would put
 * the wrong bytes in the guest's pool.
 *
 * The flush and the invalidate are no-ops in the reference host as well
 * (hle_nvn.c:2342, 529), because the host does no ZCull and tracks no dirty
 * page. */

/* A texel entry point that this runtime cannot carry out. The call is counted,
 * the refusal is named, and the buffers of the caller are left alone. */
static void texel_refused(const char *name, const void *texture, int index, const char *why) {
    int first = 0;
    int missing = 0;
    pthread_mutex_lock(&s_texture_lock);
    s_stats.calls++;
    s_stats.texel_calls++;
    missing = !texture_state_locked(texture, NULL);
    if (missing) s_stats.refused++;
    first = mark_logged(index);
    pthread_mutex_unlock(&s_texture_lock);
    if (missing) {
        fprintf(stderr, "[nvn] %s: texture=%p was never initialized; %s\n", name, texture, why);
    } else if (first) {
        fprintf(stderr, "[nvn] %s: texture=%p; %s\n", name, texture, why);
    }
}

void nvnTextureReadTexels(const void *texture, const void *view, const void *region, void *dst) {
    (void)view;
    (void)region;
    (void)dst;
    texel_refused("nvnTextureReadTexels", texture, TX_READ_TEXELS,
                  "the read is refused, because this runtime has no created texture and no table of the block size of "
                  "a format");
    NVN_VOID_RETURN();
}

void nvnTextureReadTexelsStrided(const void *texture, const void *view, const void *region, void *dst, ptrdiff_t row,
                                 ptrdiff_t img) {
    (void)view;
    (void)region;
    (void)dst;
    (void)row;
    (void)img;
    texel_refused("nvnTextureReadTexelsStrided", texture, TX_READ_TEXELS_STRIDED,
                  "the read is refused, because this runtime has no created texture and no table of the block size of "
                  "a format");
    NVN_VOID_RETURN();
}

void nvnTextureWriteTexels(const void *texture, const void *view, const void *region, const void *src) {
    (void)view;
    (void)region;
    (void)src;
    texel_refused("nvnTextureWriteTexels", texture, TX_WRITE_TEXELS,
                  "the write is refused, because this runtime has no created texture and no table of the block size of "
                  "a format");
    NVN_VOID_RETURN();
}

void nvnTextureWriteTexelsStrided(const void *texture, const void *view, const void *region, const void *src,
                                  ptrdiff_t row, ptrdiff_t img) {
    (void)view;
    (void)region;
    (void)src;
    (void)row;
    (void)img;
    texel_refused("nvnTextureWriteTexelsStrided", texture, TX_WRITE_TEXELS_STRIDED,
                  "the write is refused, because this runtime has no created texture and no table of the block size of "
                  "a format");
    NVN_VOID_RETURN();
}

/* The flush: the reference host does nothing here (hle_nvn.c:2342). */
void nvnTextureFlushTexels(const void *texture, const void *view, const void *region) {
    int first = 0;
    (void)view;
    (void)region;
    pthread_mutex_lock(&s_texture_lock);
    s_stats.calls++;
    s_stats.texel_calls++;
    first = mark_logged(TX_FLUSH_TEXELS);
    pthread_mutex_unlock(&s_texture_lock);
    if (first) {
        fprintf(stderr, "[nvn] nvnTextureFlushTexels: texture=%p; there is nothing to flush, because the host does no "
                        "unified memory and tracks no dirty page\n",
                texture);
    }
    NVN_VOID_RETURN();
}

/* The invalidate: the reference host does nothing here (hle_nvn.c:529). */
void nvnTextureInvalidateTexels(const void *texture, const void *view, const void *region) {
    int first = 0;
    (void)view;
    (void)region;
    pthread_mutex_lock(&s_texture_lock);
    s_stats.calls++;
    s_stats.texel_calls++;
    first = mark_logged(TX_INVALIDATE_TEXELS);
    pthread_mutex_unlock(&s_texture_lock);
    if (first) {
        fprintf(stderr, "[nvn] nvnTextureInvalidateTexels: texture=%p; there is nothing to invalidate, because the "
                        "host does no unified memory and tracks no dirty page\n",
                texture);
    }
    NVN_VOID_RETURN();
}

/* ------------------------------------------------------------------ */
/* The device handle functions                                          */
/* ------------------------------------------------------------------ */

/* nvnDeviceGetTextureHandle(device, tex, samp) -> texture handle.
 *
 * The handle is not a pointer: it is the number of the texture inside its
 * pool in the low twenty bits and the number of the sampler from bit twenty
 * upwards (hle_nvn.c:2059). The numbering base of a pool is NOT added here:
 * the base is added where the handle is read, not where it is made. */
NVNtextureHandle nvnDeviceGetTextureHandle(const void *device, int tex, int samp) {
    (void)device;
    pthread_mutex_lock(&s_texture_lock);
    s_stats.calls++;
    s_stats.handle_calls++;
    pthread_mutex_unlock(&s_texture_lock);
    return (NVNtextureHandle)((uint64_t)(uint32_t)tex | (uint64_t)(uint32_t)samp << 20);
}

/* nvnDeviceGetImageHandle(device, id) -> image handle.
 *
 * The handle of an image is its number in the pool (hle_nvn.c:181). */
NVNimageHandle nvnDeviceGetImageHandle(const void *device, int id) {
    (void)device;
    pthread_mutex_lock(&s_texture_lock);
    s_stats.calls++;
    s_stats.handle_calls++;
    pthread_mutex_unlock(&s_texture_lock);
    return (NVNimageHandle)(uint64_t)(uint32_t)id;
}

/* nvnDeviceGetSeparateTextureHandle(device, id) -> separate texture handle.
 *
 * The separate handle of a texture is its number in the pool (hle_nvn.c:183).
 * The sampler of a separate pair is numbered from bit twenty upwards, but that
 * is the other function of the pair and it is not part of this card. */
NVNseparateTextureHandle nvnDeviceGetSeparateTextureHandle(const void *device, int id) {
    NVNseparateTextureHandle handle;
    (void)device;
    pthread_mutex_lock(&s_texture_lock);
    s_stats.calls++;
    s_stats.handle_calls++;
    pthread_mutex_unlock(&s_texture_lock);
    handle.value = (uint64_t)(uint32_t)id;
    return handle;
}

/* nvnDeviceGetSeparateSamplerHandle(device, id) -> separate sampler handle.
 *
 * The separate handle of a sampler is its number in the pool, counted from
 * bit twenty upwards (hle_nvn.c:184), because the number of the texture of
 * the pair lives in the bits below. The numbering base of the pool is not
 * added here: the base is added where the handle is read. */
NVNseparateSamplerHandle nvnDeviceGetSeparateSamplerHandle(const void *device, int id) {
    NVNseparateSamplerHandle handle;
    (void)device;
    pthread_mutex_lock(&s_texture_lock);
    s_stats.calls++;
    s_stats.handle_calls++;
    pthread_mutex_unlock(&s_texture_lock);
    handle.value = (uint64_t)(uint32_t)id << 20;
    return handle;
}

/* nvnDeviceGetTexelFetchHandle(device, tex) -> texel fetch handle.
 *
 * A texel fetch handle names a texture and nothing else, so it is the
 * number of the texture in its pool (hle_nvn.c:2061). */
NVNtextureHandle nvnDeviceGetTexelFetchHandle(const void *device, int tex) {
    (void)device;
    pthread_mutex_lock(&s_texture_lock);
    s_stats.calls++;
    s_stats.handle_calls++;
    pthread_mutex_unlock(&s_texture_lock);
    return (NVNtextureHandle)(uint64_t)(uint32_t)tex;
}

/* ------------------------------------------------------------------ */
/* The names                                                            */
/* ------------------------------------------------------------------ */

/* The thirty one names the guest itself resolves, in the order the guest
 * resolves them in. */
static const nvn_texture_family_entry s_texture_family[] = {
    { "nvnTextureInitialize", (void *)(uintptr_t)nvnTextureInitialize },
    { "nvnTextureFinalize", (void *)(uintptr_t)nvnTextureFinalize },
    { "nvnTextureSetDebugLabel", (void *)(uintptr_t)nvnTextureSetDebugLabel },
    { "nvnTextureGetDebugID", (void *)(uintptr_t)nvnTextureGetDebugID },
    { "nvnTextureGetFlags", (void *)(uintptr_t)nvnTextureGetFlags },
    { "nvnTextureGetTarget", (void *)(uintptr_t)nvnTextureGetTarget },
    { "nvnTextureGetWidth", (void *)(uintptr_t)nvnTextureGetWidth },
    { "nvnTextureGetHeight", (void *)(uintptr_t)nvnTextureGetHeight },
    { "nvnTextureGetDepth", (void *)(uintptr_t)nvnTextureGetDepth },
    { "nvnTextureGetLevels", (void *)(uintptr_t)nvnTextureGetLevels },
    { "nvnTextureGetSamples", (void *)(uintptr_t)nvnTextureGetSamples },
    { "nvnTextureGetFormat", (void *)(uintptr_t)nvnTextureGetFormat },
    { "nvnTextureGetStride", (void *)(uintptr_t)nvnTextureGetStride },
    { "nvnTextureGetMemoryPool", (void *)(uintptr_t)nvnTextureGetMemoryPool },
    { "nvnTextureGetMemoryOffset", (void *)(uintptr_t)nvnTextureGetMemoryOffset },
    { "nvnTextureGetTextureAddress", (void *)(uintptr_t)nvnTextureGetTextureAddress },
    { "nvnTextureGetViewOffset", (void *)(uintptr_t)nvnTextureGetViewOffset },
    { "nvnTextureGetStorageClass", (void *)(uintptr_t)nvnTextureGetStorageClass },
    { "nvnTextureGetRawStorageClass", (void *)(uintptr_t)nvnTextureGetRawStorageClass },
    { "nvnTextureGetStorageSize", (void *)(uintptr_t)nvnTextureGetStorageSize },
    { "nvnTextureGetZCullStorageSize", (void *)(uintptr_t)nvnTextureGetZCullStorageSize },
    { "nvnTextureGetSwizzle", (void *)(uintptr_t)nvnTextureGetSwizzle },
    { "nvnTextureGetDepthStencilMode", (void *)(uintptr_t)nvnTextureGetDepthStencilMode },
    { "nvnTextureGetSparseTileLayout", (void *)(uintptr_t)nvnTextureGetSparseTileLayout },
    { "nvnTextureCompare", (void *)(uintptr_t)nvnTextureCompare },
    { "nvnTextureReadTexels", (void *)(uintptr_t)nvnTextureReadTexels },
    { "nvnTextureReadTexelsStrided", (void *)(uintptr_t)nvnTextureReadTexelsStrided },
    { "nvnTextureWriteTexels", (void *)(uintptr_t)nvnTextureWriteTexels },
    { "nvnTextureWriteTexelsStrided", (void *)(uintptr_t)nvnTextureWriteTexelsStrided },
    { "nvnTextureFlushTexels", (void *)(uintptr_t)nvnTextureFlushTexels },
    { "nvnTextureInvalidateTexels", (void *)(uintptr_t)nvnTextureInvalidateTexels },
};

/* The three device handle functions. */
static const nvn_device_handle_family_entry s_device_handle_family[] = {
    { "nvnDeviceGetTextureHandle", (void *)(uintptr_t)nvnDeviceGetTextureHandle },
    { "nvnDeviceGetImageHandle", (void *)(uintptr_t)nvnDeviceGetImageHandle },
    { "nvnDeviceGetSeparateTextureHandle", (void *)(uintptr_t)nvnDeviceGetSeparateTextureHandle },
    { "nvnDeviceGetSeparateSamplerHandle", (void *)(uintptr_t)nvnDeviceGetSeparateSamplerHandle },
    { "nvnDeviceGetTexelFetchHandle", (void *)(uintptr_t)nvnDeviceGetTexelFetchHandle },
};

size_t nvn_texture_family_count(void) {
    return sizeof(s_texture_family) / sizeof(s_texture_family[0]);
}

const nvn_texture_family_entry *nvn_texture_family_entry_at(size_t index) {
    if (index >= nvn_texture_family_count()) return NULL;
    return &s_texture_family[index];
}

size_t nvn_device_handle_family_count(void) {
    return sizeof(s_device_handle_family) / sizeof(s_device_handle_family[0]);
}

const nvn_device_handle_family_entry *nvn_device_handle_family_entry_at(size_t index) {
    if (index >= nvn_device_handle_family_count()) return NULL;
    return &s_device_handle_family[index];
}

void nvn_texture_get_stats(nvn_texture_stats *out) {
    size_t i;
    if (!out) return;
    pthread_mutex_lock(&s_texture_lock);
    *out = s_stats;
    out->live_textures = 0;
    for (i = 0; i < NVN_TEXTURE_MAX_TEXTURES; i++) {
        if (s_textures[i].in_use && s_textures[i].initialized) out->live_textures++;
    }
    pthread_mutex_unlock(&s_texture_lock);
}
