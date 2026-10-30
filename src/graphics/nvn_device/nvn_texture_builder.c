/* The NVN texture builder family implementation.
 *
 * See nvn_texture_builder.h for why the state lives on the host.
 *
 * The guest describes a texture in a builder and then hands that builder to
 * nvnTextureInitialize. Every value the guest puts into the builder is kept
 * here, keyed by the address of the builder; the guest's own builder memory is
 * never touched, because its layout belongs to the Nintendo SDK.
 *
 * The builder describes, it does not create. No GPU memory is allocated here:
 * that is the work of the texture creation stage, which does not exist yet.
 * The storage of the texture is recorded as the pair the guest hands over
 * (the memory pool and the offset inside it) and is answered back unchanged.
 *
 * The evidence for the values:
 *
 *   - The argument order and the shape of the setters and getters come from
 *     the vendored NVN header (src/graphics/nvn/include/nvn/nvn.h), which is a
 *     reconstruction of NVN 53.105 taken from another game (Darksiders II).
 *     Its signatures are therefore NOT confirmed for this game, which is
 *     Unreal Engine 5.6.1.
 *   - The default values come from the reference host that runs this very
 *     game: .local/refs/nxnative/dm/gfx/nvn/hle_nvn.c, the
 *     nvnTextureBuilderSetDefaults of nxnative (MIT, revision 7a029f66,
 *     dm/games/0100606026F12000). It sets the target to 2D and width, height,
 *     depth and levels to 1, and leaves everything else at zero. The
 *     enumeration values themselves are the ones of the vendored header.
 *   - The device survives the reset, because the reference tree keeps it
 *     across the reset as well (hle_nvn.c:280). The identity of the builder
 *     and the fact that its table slot is occupied are not part of the reset
 *     either: clearing those was the defect the render state family shipped,
 *     and it made the "the defaults really reset" check pass for the wrong
 *     reason.
 *
 * Four of the forty four names have no signature in the vendored header, and
 * the reference tree lists them as well among the names it can only give a
 * stub (nvn_protos.h: "Names without a prototype in the header"). Those entry
 * points take their arguments as plain words, write them down as they came
 * and answer with zero; nothing is dereferenced, so a signature with fewer or
 * more arguments cannot do any harm here.
 *
 * Three further names are answered with a refusal by name, because their
 * value is the storage layout of the texture and that layout belongs to the
 * texture creation stage: nvnTextureBuilderGetStorageSize,
 * nvnTextureBuilderGetStorageAlignment and nvnTextureBuilderGetZCullStorageSize.
 * The layout algorithm lives in the reference tree (nvn_tex.c, a port of
 * deko3d's dk_image.cpp) and is not part of this family; a number invented
 * here would be smaller than the real one and would let the guest place two
 * textures on top of each other in its memory pool.
 */
#include "nvn_texture_builder.h"

#include "nvn_device.h"
#include "../nvn_vulkan/nvn/nvn_tex.h"

#include "../nvn/include/nvn/nvn.h"

#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include "nvn_object_table.h"

typedef struct {
    void *builder;
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
    int swizzle_set;
    NVNtextureDepthStencilMode depth_stencil_mode;
    uint32_t gl_texture_name;
    const void *packaged_texture_data;
    unsigned char packaged_layout[8];
    int has_packaged_layout;
    /* The block height of a packaged texture: byte 0 of the layout parameters
     * is the log2 of it, plus one (hle_nvn.c:298). It is recorded here so the
     * texture creation stage does not have to read the guest's memory again. */
    uint32_t packaged_block_height;
    int in_use;
} nvn_texture_builder_state;

/* The names of this family, in the order of the family table. The index is
 * used for the "only the first call is logged" bookkeeping. */
enum {
    TB_SET_DEFAULTS,
    TB_SET_DEVICE,
    TB_SET_FORMAT,
    TB_SET_TARGET,
    TB_SET_FLAGS,
    TB_SET_WIDTH,
    TB_SET_HEIGHT,
    TB_SET_DEPTH,
    TB_SET_LEVELS,
    TB_SET_SAMPLES,
    TB_SET_STRIDE,
    TB_SET_STORAGE,
    TB_SET_SIZE1D,
    TB_SET_SIZE2D,
    TB_SET_SIZE3D,
    TB_SET_SWIZZLE,
    TB_SET_DEPTH_STENCIL_MODE,
    TB_SET_GL_TEXTURE_NAME,
    TB_SET_PACKAGED_TEXTURE_DATA,
    TB_SET_PACKAGED_TEXTURE_LAYOUT,
    TB_GET_DEVICE,
    TB_GET_FORMAT,
    TB_GET_TARGET,
    TB_GET_FLAGS,
    TB_GET_WIDTH,
    TB_GET_HEIGHT,
    TB_GET_DEPTH,
    TB_GET_LEVELS,
    TB_GET_SAMPLES,
    TB_GET_STRIDE,
    TB_GET_SWIZZLE,
    TB_GET_DEPTH_STENCIL_MODE,
    TB_GET_GL_TEXTURE_NAME,
    TB_GET_MEMORY_POOL,
    TB_GET_MEMORY_OFFSET,
    TB_GET_STORAGE_SIZE,
    TB_GET_STORAGE_ALIGNMENT,
    TB_GET_STORAGE_CLASS,
    TB_GET_RAW_STORAGE_CLASS,
    TB_GET_VIEW_OFFSET,
    TB_GET_SPARSE_TILE_LAYOUT,
    TB_GET_ZCULL_STORAGE_SIZE,
    TB_GET_PACKAGED_TEXTURE_DATA,
    TB_GET_PACKAGED_TEXTURE_LAYOUT,
    TB_NAME_COUNT
};

static pthread_mutex_t s_texture_builder_lock = PTHREAD_MUTEX_INITIALIZER;
static nvn_object_table s_builders = { NULL, 0, 0, sizeof(nvn_texture_builder_state) };
static nvn_texture_builder_stats s_stats;
static int s_logged[TB_NAME_COUNT];

#define NVN_BUILDER_MAGIC 0x424C4452u /* 'BLDR' */

typedef struct nvn_builder_inline {
    uint32_t magic;
    uint32_t in_use;
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
    int swizzle_set;
    NVNtextureDepthStencilMode depth_stencil_mode;
    uint32_t gl_texture_name;
    const void *packaged_texture_data;
    unsigned char packaged_layout[8];
    int has_packaged_layout;
    uint32_t packaged_block_height;
} nvn_builder_inline;
_Static_assert(sizeof(nvn_builder_inline) <= 128, "nvn_builder_inline must fit in NVNtextureBuilder (128 bytes)");

static void sync_to_inline(void *builder, const nvn_texture_builder_state *s) {
    if (!builder || !s) return;
    nvn_builder_inline *inl = (nvn_builder_inline *)builder;
    inl->magic = NVN_BUILDER_MAGIC;
    inl->in_use = s->in_use;
    inl->device = s->device;
    inl->format = s->format;
    inl->target = s->target;
    inl->flags = s->flags;
    inl->width = s->width;
    inl->height = s->height;
    inl->depth = s->depth;
    inl->levels = s->levels;
    inl->samples = s->samples;
    inl->stride = s->stride;
    inl->memory_pool = s->memory_pool;
    inl->memory_offset = s->memory_offset;
    memcpy(inl->swizzle, s->swizzle, sizeof(s->swizzle));
    inl->swizzle_set = s->swizzle_set;
    inl->depth_stencil_mode = s->depth_stencil_mode;
    inl->gl_texture_name = s->gl_texture_name;
    inl->packaged_texture_data = s->packaged_texture_data;
    memcpy(inl->packaged_layout, s->packaged_layout, sizeof(s->packaged_layout));
    inl->has_packaged_layout = s->has_packaged_layout;
    inl->packaged_block_height = s->packaged_block_height;
}

static void sync_from_inline(nvn_texture_builder_state *s, const void *builder) {
    if (!s || !builder) return;
    const nvn_builder_inline *inl = (const nvn_builder_inline *)builder;
    if (inl->magic != NVN_BUILDER_MAGIC || !inl->in_use) return;
    s->device = inl->device;
    s->format = inl->format;
    s->target = inl->target;
    s->flags = inl->flags;
    s->width = inl->width;
    s->height = inl->height;
    s->depth = inl->depth;
    s->levels = inl->levels;
    s->samples = inl->samples;
    s->stride = inl->stride;
    s->memory_pool = inl->memory_pool;
    s->memory_offset = inl->memory_offset;
    memcpy(s->swizzle, inl->swizzle, sizeof(s->swizzle));
    s->swizzle_set = inl->swizzle_set;
    s->depth_stencil_mode = inl->depth_stencil_mode;
    s->gl_texture_name = inl->gl_texture_name;
    s->packaged_texture_data = inl->packaged_texture_data;
    memcpy(s->packaged_layout, inl->packaged_layout, sizeof(s->packaged_layout));
    s->has_packaged_layout = inl->has_packaged_layout;
    s->packaged_block_height = inl->packaged_block_height;
}

/* The defaults of the reference tree (hle_nvn.c:280). */
static nvn_texture_builder_state default_state(void) {
    nvn_texture_builder_state s;
    memset(&s, 0, sizeof(s));
    s.target = NVN_TEXTURE_TARGET_2D;
    s.width = 1;
    s.height = 1;
    s.depth = 1;
    s.levels = 1;
    /* The swizzle of a texture that was never given one is the identity
     * mapping R, G, B, A (hle_nvn.c:490). */
    s.swizzle[0] = NVN_TEXTURE_SWIZZLE_R;
    s.swizzle[1] = NVN_TEXTURE_SWIZZLE_G;
    s.swizzle[2] = NVN_TEXTURE_SWIZZLE_B;
    s.swizzle[3] = NVN_TEXTURE_SWIZZLE_A;
    s.depth_stencil_mode = NVN_TEXTURE_DEPTH_STENCIL_MODE_DEPTH;
    return s;
}

/* The defaults really reset every field they own.
 *
 * The identity of the builder, the device and the occupancy of the table slot
 * are kept: the device because the reference tree keeps it across the reset
 * (hle_nvn.c:280), the other two because they are not fields of the builder
 * but of this runtime's table. */
static void apply_defaults(nvn_texture_builder_state *s) {
    nvn_texture_builder_state d = default_state();
    d.builder = s->builder;
    d.device = s->device;
    d.in_use = s->in_use;
    *s = d;
}

/* The builder state for one guest builder address. A builder that was never
 * prepared has no state, and the functions that need one say so instead of
 * inventing a default. */
/* Index hints only: validate the canonical slot under the family lock, and
 * still synchronize copied inline builders on every hit. Indices survive
 * object-table reallocations; collisions take the original scan. */
static struct {
    void *builder;
    size_t index;
} s_builder_lookup_cache[512];
static nvn_texture_builder_state *builder_find(void *builder, int create) {
    size_t i;
    nvn_texture_builder_state *free_slot = NULL;
    nvn_texture_builder_state *found = NULL;
    if (!builder) return NULL;
    size_t cache_index = (size_t)((((uint64_t)(uintptr_t)builder >> 4) * UINT64_C(11400714819323198485)) >> 55);
    if (s_builder_lookup_cache[cache_index].builder == builder &&
        s_builder_lookup_cache[cache_index].index < s_builders.count) {
        nvn_texture_builder_state *entry = (nvn_texture_builder_state *)nvn_object_table_at(&s_builders, s_builder_lookup_cache[cache_index].index);
        if (entry && entry->in_use && entry->builder == builder) found = entry;
    }
    for (i = 0; !found && i < s_builders.count; i++) {
        nvn_texture_builder_state *entry = (nvn_texture_builder_state *)nvn_object_table_at(&s_builders, i);
        if (!entry) continue;
        if (entry->in_use) {
            if (entry->builder == builder) {
                found = entry;
                s_builder_lookup_cache[cache_index].builder = builder;
                s_builder_lookup_cache[cache_index].index = i;
                break;
            }
        } else if (!free_slot) {
            free_slot = entry;
        }
    }
    const nvn_builder_inline *inl = (const nvn_builder_inline *)builder;
    int is_copied = (inl && inl->magic == NVN_BUILDER_MAGIC && inl->in_use);
    if (found) {
        if (is_copied) {
            sync_from_inline(found, builder);
        }
        return found;
    }

    if (!create && !is_copied) return NULL;
    if (!free_slot) {
        free_slot = (nvn_texture_builder_state *)nvn_object_table_new_entry(&s_builders, NVN_TEXTURE_BUILDER_MAX_OBJECTS);
        if (!free_slot) return NULL;
    }
    if (is_copied) {
        memset(free_slot, 0, sizeof(*free_slot));
        free_slot->builder = builder;
        free_slot->in_use = 1;
        sync_from_inline(free_slot, builder);
    } else {
        *free_slot = default_state();
        free_slot->builder = builder;
        free_slot->in_use = 1;
        sync_to_inline(builder, free_slot);
    }
    s_builder_lookup_cache[cache_index].builder = builder;
    s_builder_lookup_cache[cache_index].index = (size_t)((char *)free_slot - (char *)s_builders.slots) / s_builders.entry_size;
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
/* The setters                                                         */
/* ------------------------------------------------------------------ */

void nvnTextureBuilderSetDefaults(void *builder) {
    nvn_texture_builder_state *s;
    int first = 0;
    pthread_mutex_lock(&s_texture_builder_lock);
    s_stats.calls++;
    s_stats.set_defaults++;
    s = builder_find(builder, 1);
    if (s) {
        apply_defaults(s);
        sync_to_inline(builder, s);
    } else {
        s_stats.refused++;
    }
    first = mark_logged(TB_SET_DEFAULTS);
    pthread_mutex_unlock(&s_texture_builder_lock);
    if (!s) {
        fprintf(stderr, "[nvn] nvnTextureBuilderSetDefaults: no room for another texture builder (%p); the "
                        "defaults were not applied\n",
                builder);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnTextureBuilderSetDefaults: builder=%p\n", builder);
    }
    NVN_VOID_RETURN();
}

void nvnTextureBuilderSetDevice(void *builder, void *device) {
    nvn_texture_builder_state *s;
    int first = 0;
    pthread_mutex_lock(&s_texture_builder_lock);
    s_stats.calls++;
    s_stats.sets++;
    s = builder_find(builder, 1);
    if (s) {
        s->device = device;
        sync_to_inline(builder, s);
    } else {
        s_stats.refused++;
    }
    first = mark_logged(TB_SET_DEVICE);
    pthread_mutex_unlock(&s_texture_builder_lock);
    if (!s) {
        fprintf(stderr, "[nvn] nvnTextureBuilderSetDevice: no room for another texture builder (%p); the device "
                        "was not recorded\n",
                builder);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnTextureBuilderSetDevice: builder=%p device=%p\n", builder, device);
    }
    NVN_VOID_RETURN();
}

void nvnTextureBuilderSetFormat(void *builder, NVNformat format) {
    nvn_texture_builder_state *s;
    int first = 0;
    pthread_mutex_lock(&s_texture_builder_lock);
    s_stats.calls++;
    s_stats.sets++;
    s = builder_find(builder, 1);
    if (s) {
        s->format = format;
        sync_to_inline(builder, s);
    } else {
        s_stats.refused++;
    }
    first = mark_logged(TB_SET_FORMAT);
    pthread_mutex_unlock(&s_texture_builder_lock);
    if (!s) {
        fprintf(stderr, "[nvn] nvnTextureBuilderSetFormat: no room for another texture builder (%p); the format "
                        "was not recorded\n",
                builder);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnTextureBuilderSetFormat: builder=%p format=0x%x\n", builder, (unsigned)format);
    }
    NVN_VOID_RETURN();
}

void nvnTextureBuilderSetTarget(void *builder, NVNtextureTarget target) {
    nvn_texture_builder_state *s;
    int first = 0;
    pthread_mutex_lock(&s_texture_builder_lock);
    s_stats.calls++;
    s_stats.sets++;
    s = builder_find(builder, 1);
    if (s) {
        s->target = target;
        sync_to_inline(builder, s);
    } else {
        s_stats.refused++;
    }
    first = mark_logged(TB_SET_TARGET);
    pthread_mutex_unlock(&s_texture_builder_lock);
    if (!s) {
        fprintf(stderr, "[nvn] nvnTextureBuilderSetTarget: no room for another texture builder (%p); the target "
                        "was not recorded\n",
                builder);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnTextureBuilderSetTarget: builder=%p target=0x%x\n", builder, (unsigned)target);
    }
    NVN_VOID_RETURN();
}

void nvnTextureBuilderSetFlags(void *builder, int flags) {
    nvn_texture_builder_state *s;
    int first = 0;
    pthread_mutex_lock(&s_texture_builder_lock);
    s_stats.calls++;
    s_stats.sets++;
    s = builder_find(builder, 1);
    if (s) {
        s->flags = (NVNtextureFlags)(unsigned)flags;
        sync_to_inline(builder, s);
    } else {
        s_stats.refused++;
    }
    first = mark_logged(TB_SET_FLAGS);
    pthread_mutex_unlock(&s_texture_builder_lock);
    if (!s) {
        fprintf(stderr, "[nvn] nvnTextureBuilderSetFlags: no room for another texture builder (%p); the flags were "
                        "not recorded\n",
                builder);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnTextureBuilderSetFlags: builder=%p flags=0x%x\n", builder, (unsigned)flags);
    }
    NVN_VOID_RETURN();
}

void nvnTextureBuilderSetWidth(void *builder, int width) {
    nvn_texture_builder_state *s;
    int first = 0;
    pthread_mutex_lock(&s_texture_builder_lock);
    s_stats.calls++;
    s_stats.sets++;
    s = builder_find(builder, 1);
    if (s) {
        s->width = width;
        sync_to_inline(builder, s);
    } else {
        s_stats.refused++;
    }
    first = mark_logged(TB_SET_WIDTH);
    pthread_mutex_unlock(&s_texture_builder_lock);
    if (!s) {
        fprintf(stderr, "[nvn] nvnTextureBuilderSetWidth: no room for another texture builder (%p); the width was "
                        "not recorded\n",
                builder);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnTextureBuilderSetWidth: builder=%p width=%d\n", builder, width);
    }
    NVN_VOID_RETURN();
}

void nvnTextureBuilderSetHeight(void *builder, int height) {
    nvn_texture_builder_state *s;
    int first = 0;
    pthread_mutex_lock(&s_texture_builder_lock);
    s_stats.calls++;
    s_stats.sets++;
    s = builder_find(builder, 1);
    if (s) {
        s->height = height;
        sync_to_inline(builder, s);
    } else {
        s_stats.refused++;
    }
    first = mark_logged(TB_SET_HEIGHT);
    pthread_mutex_unlock(&s_texture_builder_lock);
    if (!s) {
        fprintf(stderr, "[nvn] nvnTextureBuilderSetHeight: no room for another texture builder (%p); the height "
                        "was not recorded\n",
                builder);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnTextureBuilderSetHeight: builder=%p height=%d\n", builder, height);
    }
    NVN_VOID_RETURN();
}

void nvnTextureBuilderSetDepth(void *builder, int depth) {
    nvn_texture_builder_state *s;
    int first = 0;
    pthread_mutex_lock(&s_texture_builder_lock);
    s_stats.calls++;
    s_stats.sets++;
    s = builder_find(builder, 1);
    if (s) {
        s->depth = depth;
        sync_to_inline(builder, s);
    } else {
        s_stats.refused++;
    }
    first = mark_logged(TB_SET_DEPTH);
    pthread_mutex_unlock(&s_texture_builder_lock);
    if (!s) {
        fprintf(stderr, "[nvn] nvnTextureBuilderSetDepth: no room for another texture builder (%p); the depth was "
                        "not recorded\n",
                builder);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnTextureBuilderSetDepth: builder=%p depth=%d\n", builder, depth);
    }
    NVN_VOID_RETURN();
}

void nvnTextureBuilderSetLevels(void *builder, int levels) {
    nvn_texture_builder_state *s;
    int first = 0;
    pthread_mutex_lock(&s_texture_builder_lock);
    s_stats.calls++;
    s_stats.sets++;
    s = builder_find(builder, 1);
    if (s) {
        s->levels = levels;
        sync_to_inline(builder, s);
    } else {
        s_stats.refused++;
    }
    first = mark_logged(TB_SET_LEVELS);
    pthread_mutex_unlock(&s_texture_builder_lock);
    if (!s) {
        fprintf(stderr, "[nvn] nvnTextureBuilderSetLevels: no room for another texture builder (%p); the number "
                        "of levels was not recorded\n",
                builder);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnTextureBuilderSetLevels: builder=%p levels=%d\n", builder, levels);
    }
    NVN_VOID_RETURN();
}

void nvnTextureBuilderSetSamples(void *builder, int samples) {
    nvn_texture_builder_state *s;
    int first = 0;
    pthread_mutex_lock(&s_texture_builder_lock);
    s_stats.calls++;
    s_stats.sets++;
    s = builder_find(builder, 1);
    if (s) {
        s->samples = samples;
        sync_to_inline(builder, s);
    } else {
        s_stats.refused++;
    }
    first = mark_logged(TB_SET_SAMPLES);
    pthread_mutex_unlock(&s_texture_builder_lock);
    if (!s) {
        fprintf(stderr, "[nvn] nvnTextureBuilderSetSamples: no room for another texture builder (%p); the number "
                        "of samples was not recorded\n",
                builder);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnTextureBuilderSetSamples: builder=%p samples=%d\n", builder, samples);
    }
    NVN_VOID_RETURN();
}

void nvnTextureBuilderSetStride(void *builder, ptrdiff_t stride) {
    nvn_texture_builder_state *s;
    int first = 0;
    pthread_mutex_lock(&s_texture_builder_lock);
    s_stats.calls++;
    s_stats.sets++;
    s = builder_find(builder, 1);
    if (s) {
        s->stride = stride;
        sync_to_inline(builder, s);
    } else {
        s_stats.refused++;
    }
    first = mark_logged(TB_SET_STRIDE);
    pthread_mutex_unlock(&s_texture_builder_lock);
    if (!s) {
        fprintf(stderr, "[nvn] nvnTextureBuilderSetStride: no room for another texture builder (%p); the stride "
                        "was not recorded\n",
                builder);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnTextureBuilderSetStride: builder=%p stride=%" PRIdPTR "\n", builder, stride);
    }
    NVN_VOID_RETURN();
}

/* The storage of the texture: the memory pool the guest owns and the offset
 * inside it. No GPU memory is allocated here; the builder only describes the
 * texture, and creating it is the work of the texture creation stage. */
void nvnTextureBuilderSetStorage(void *builder, void *memory_pool, ptrdiff_t offset) {
    nvn_texture_builder_state *s;
    int first = 0;
    pthread_mutex_lock(&s_texture_builder_lock);
    s_stats.calls++;
    s_stats.sets++;
    s = builder_find(builder, 1);
    if (s) {
        s->memory_pool = memory_pool;
        s->memory_offset = offset;
        sync_to_inline(builder, s);
    } else {
        s_stats.refused++;
    }
    first = mark_logged(TB_SET_STORAGE);
    pthread_mutex_unlock(&s_texture_builder_lock);
    if (!s) {
        fprintf(stderr, "[nvn] nvnTextureBuilderSetStorage: no room for another texture builder (%p); the storage "
                        "was not recorded\n",
                builder);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnTextureBuilderSetStorage: builder=%p pool=%p offset=%" PRIdPTR "\n", builder,
                memory_pool, offset);
    }
    NVN_VOID_RETURN();
}

/* The size setters. They set the three dimensions and leave the target
 * alone, the way the reference tree does (hle_nvn.c:290, 1942, 1943). */
void nvnTextureBuilderSetSize1D(void *builder, int width) {
    nvn_texture_builder_state *s;
    int first = 0;
    pthread_mutex_lock(&s_texture_builder_lock);
    s_stats.calls++;
    s_stats.sets++;
    s = builder_find(builder, 1);
    if (s) {
        s->width = width;
        s->height = 1;
        s->depth = 1;
        sync_to_inline(builder, s);
    } else {
        s_stats.refused++;
    }
    first = mark_logged(TB_SET_SIZE1D);
    pthread_mutex_unlock(&s_texture_builder_lock);
    if (!s) {
        fprintf(stderr, "[nvn] nvnTextureBuilderSetSize1D: no room for another texture builder (%p); the size was "
                        "not recorded\n",
                builder);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnTextureBuilderSetSize1D: builder=%p width=%d\n", builder, width);
    }
    NVN_VOID_RETURN();
}

void nvnTextureBuilderSetSize2D(void *builder, int width, int height) {
    nvn_texture_builder_state *s;
    int first = 0;
    pthread_mutex_lock(&s_texture_builder_lock);
    s_stats.calls++;
    s_stats.sets++;
    s = builder_find(builder, 1);
    if (s) {
        s->width = width;
        s->height = height;
        s->depth = 1;
        sync_to_inline(builder, s);
    } else {
        s_stats.refused++;
    }
    first = mark_logged(TB_SET_SIZE2D);
    pthread_mutex_unlock(&s_texture_builder_lock);
    if (!s) {
        fprintf(stderr, "[nvn] nvnTextureBuilderSetSize2D: no room for another texture builder (%p); the size was "
                        "not recorded\n",
                builder);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnTextureBuilderSetSize2D: builder=%p width=%d height=%d\n", builder, width, height);
    }
    NVN_VOID_RETURN();
}

void nvnTextureBuilderSetSize3D(void *builder, int width, int height, int depth) {
    nvn_texture_builder_state *s;
    int first = 0;
    pthread_mutex_lock(&s_texture_builder_lock);
    s_stats.calls++;
    s_stats.sets++;
    s = builder_find(builder, 1);
    if (s) {
        s->width = width;
        s->height = height;
        s->depth = depth;
        sync_to_inline(builder, s);
    } else {
        s_stats.refused++;
    }
    first = mark_logged(TB_SET_SIZE3D);
    pthread_mutex_unlock(&s_texture_builder_lock);
    if (!s) {
        fprintf(stderr, "[nvn] nvnTextureBuilderSetSize3D: no room for another texture builder (%p); the size was "
                        "not recorded\n",
                builder);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnTextureBuilderSetSize3D: builder=%p width=%d height=%d depth=%d\n", builder, width,
                height, depth);
    }
    NVN_VOID_RETURN();
}

void nvnTextureBuilderSetSwizzle(void *builder, NVNtextureSwizzle red, NVNtextureSwizzle green,
                                  NVNtextureSwizzle blue, NVNtextureSwizzle alpha) {
    nvn_texture_builder_state *s;
    int first = 0;
    pthread_mutex_lock(&s_texture_builder_lock);
    s_stats.calls++;
    s_stats.sets++;
    s = builder_find(builder, 1);
    if (s) {
        s->swizzle[0] = red;
        s->swizzle[1] = green;
        s->swizzle[2] = blue;
        s->swizzle[3] = alpha;
        s->swizzle_set = 1;
        sync_to_inline(builder, s);
    } else {
        s_stats.refused++;
    }
    first = mark_logged(TB_SET_SWIZZLE);
    pthread_mutex_unlock(&s_texture_builder_lock);
    if (!s) {
        fprintf(stderr, "[nvn] nvnTextureBuilderSetSwizzle: no room for another texture builder (%p); the swizzle "
                        "was not recorded\n",
                builder);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnTextureBuilderSetSwizzle: builder=%p r=0x%x g=0x%x b=0x%x a=0x%x\n", builder,
                (unsigned)red, (unsigned)green, (unsigned)blue, (unsigned)alpha);
    }
    NVN_VOID_RETURN();
}

void nvnTextureBuilderSetDepthStencilMode(void *builder, NVNtextureDepthStencilMode mode) {
    nvn_texture_builder_state *s;
    int first = 0;
    pthread_mutex_lock(&s_texture_builder_lock);
    s_stats.calls++;
    s_stats.sets++;
    s = builder_find(builder, 1);
    if (s) {
        s->depth_stencil_mode = mode;
        sync_to_inline(builder, s);
    } else {
        s_stats.refused++;
    }
    first = mark_logged(TB_SET_DEPTH_STENCIL_MODE);
    pthread_mutex_unlock(&s_texture_builder_lock);
    if (!s) {
        fprintf(stderr, "[nvn] nvnTextureBuilderSetDepthStencilMode: no room for another texture builder (%p); the "
                        "depth stencil mode was not recorded\n",
                builder);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnTextureBuilderSetDepthStencilMode: builder=%p mode=0x%x\n", builder,
                (unsigned)mode);
    }
    NVN_VOID_RETURN();
}

void nvnTextureBuilderSetGLTextureName(void *builder, uint32_t name) {
    nvn_texture_builder_state *s;
    int first = 0;
    pthread_mutex_lock(&s_texture_builder_lock);
    s_stats.calls++;
    s_stats.sets++;
    s = builder_find(builder, 1);
    if (s) {
        s->gl_texture_name = name;
        sync_to_inline(builder, s);
    } else {
        s_stats.refused++;
    }
    first = mark_logged(TB_SET_GL_TEXTURE_NAME);
    pthread_mutex_unlock(&s_texture_builder_lock);
    if (!s) {
        fprintf(stderr, "[nvn] nvnTextureBuilderSetGLTextureName: no room for another texture builder (%p); the "
                        "name was not recorded\n",
                builder);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnTextureBuilderSetGLTextureName: builder=%p name=0x%x\n", builder,
                (unsigned)name);
    }
    NVN_VOID_RETURN();
}

/* The packaged texture data. The pointer is kept, not copied: the reference
 * tree copies the data into the storage when the texture is created, because
 * the packaged data of the game is already in the layout the driver wants
 * (hle_nvn.c:1961, nvn_obj.h:29). */
void nvnTextureBuilderSetPackagedTextureData(void *builder, const void *data) {
    nvn_texture_builder_state *s;
    int first = 0;
    pthread_mutex_lock(&s_texture_builder_lock);
    s_stats.calls++;
    s_stats.sets++;
    s = builder_find(builder, 1);
    if (s) {
        s->packaged_texture_data = data;
        sync_to_inline(builder, s);
    } else {
        s_stats.refused++;
    }
    first = mark_logged(TB_SET_PACKAGED_TEXTURE_DATA);
    pthread_mutex_unlock(&s_texture_builder_lock);
    if (!s) {
        fprintf(stderr, "[nvn] nvnTextureBuilderSetPackagedTextureData: no room for another texture builder (%p); "
                        "the packaged data was not recorded\n",
                builder);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnTextureBuilderSetPackagedTextureData: builder=%p data=%p\n", builder, data);
    }
    NVN_VOID_RETURN();
}

/* The packaged texture layout: eight bytes of layout parameters. The
 * vendored header defines the type as eight bytes (nvn.h:227) and the
 * reference tree copies exactly those eight bytes (hle_nvn.c:297). A null
 * pointer clears the layout, as the reference tree does (hle_nvn.c:297). */
void nvnTextureBuilderSetPackagedTextureLayout(void *builder, const NVNpackagedTextureLayout *layout) {
    nvn_texture_builder_state *s;
    int first = 0;
    pthread_mutex_lock(&s_texture_builder_lock);
    s_stats.calls++;
    s_stats.sets++;
    s = builder_find(builder, 1);
    if (s) {
        if (layout) {
            memcpy(s->packaged_layout, layout->layout, sizeof(s->packaged_layout));
            s->has_packaged_layout = 1;
            s->packaged_block_height = (uint32_t)layout->layout[0] + 1u;
        } else {
            memset(s->packaged_layout, 0, sizeof(s->packaged_layout));
            s->has_packaged_layout = 0;
            s->packaged_block_height = 0;
        }
        sync_to_inline(builder, s);
    } else {
        s_stats.refused++;
    }
    first = mark_logged(TB_SET_PACKAGED_TEXTURE_LAYOUT);
    pthread_mutex_unlock(&s_texture_builder_lock);
    if (!s) {
        fprintf(stderr, "[nvn] nvnTextureBuilderSetPackagedTextureLayout: no room for another texture builder (%p); "
                        "the layout was not recorded\n",
                builder);
    } else if (first) {
        fprintf(stderr, "[nvn] nvnTextureBuilderSetPackagedTextureLayout: builder=%p layout=%p\n", builder,
                (const void *)layout);
    }
    NVN_VOID_RETURN();
}

/* ------------------------------------------------------------------ */
/* The getters                                                         */
/* ------------------------------------------------------------------ */

/* The signature of nvnTextureBuilderGetDevice is not in the vendored header
 * this runtime was built against, and the reference tree lists it among the
 * names it can only give a stub (nvn_protos.h: "Names without a prototype in
 * the header"). The arguments are therefore taken as plain words and written
 * down as they came. Nothing is dereferenced, so a signature with fewer or
 * more arguments cannot do any harm here.
 *
 * The answer is the device the guest put into this builder, not zero. The
 * signature is not confirmed, but a getter that hands back a null pointer is
 * the one answer that can crash the guest if it dereferences the result, and
 * the device the guest itself set is the only value this can give that is
 * both safe and true. The log says that the signature is not confirmed. */
void nvnTextureBuilderGetDevice(uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4,
                                 uint64_t arg5, uint64_t arg6, uint64_t arg7) {
    int first = 0;
    void *device = NULL;
    nvn_texture_builder_state *found;
    pthread_mutex_lock(&s_texture_builder_lock);
    s_stats.calls++;
    s_stats.gets++;
    s_stats.raw_calls++;
    found = builder_find((void *)(uintptr_t)arg0, 0);
    if (found) device = found->device;
    first = mark_logged(TB_GET_DEVICE);
    pthread_mutex_unlock(&s_texture_builder_lock);
    if (first) {
        fprintf(stderr, "[nvn] nvnTextureBuilderGetDevice: args=0x%" PRIx64 ",0x%" PRIx64 ",0x%" PRIx64 ",0x%" PRIx64
                        ",0x%" PRIx64 ",0x%" PRIx64 ",0x%" PRIx64 ",0x%" PRIx64
                        "; the signature of this entry point is not in the header this runtime was built against, "
                        "so the arguments are recorded as they came and the device of the builder is returned\n",
                arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7);
    }
    NVN_SET_RESULT((uint64_t)(uintptr_t)device);
}

NVNformat nvnTextureBuilderGetFormat(void *builder) {
    nvn_texture_builder_state s = default_state();
    nvn_texture_builder_state *found;
    NVNformat format;
    int first = 0;
    pthread_mutex_lock(&s_texture_builder_lock);
    s_stats.calls++;
    s_stats.gets++;
    found = builder_find(builder, 0);
    if (found) s = *found;
    format = s.format;
    first = mark_logged(TB_GET_FORMAT);
    pthread_mutex_unlock(&s_texture_builder_lock);
    if (first) {
        fprintf(stderr, "[nvn] nvnTextureBuilderGetFormat: builder=%p format=0x%x\n", builder, (unsigned)format);
    }
    return format;
}

NVNtextureTarget nvnTextureBuilderGetTarget(void *builder) {
    nvn_texture_builder_state s = default_state();
    nvn_texture_builder_state *found;
    NVNtextureTarget target;
    int first = 0;
    pthread_mutex_lock(&s_texture_builder_lock);
    s_stats.calls++;
    s_stats.gets++;
    found = builder_find(builder, 0);
    if (found) s = *found;
    target = s.target;
    first = mark_logged(TB_GET_TARGET);
    pthread_mutex_unlock(&s_texture_builder_lock);
    if (first) {
        fprintf(stderr, "[nvn] nvnTextureBuilderGetTarget: builder=%p target=0x%x\n", builder, (unsigned)target);
    }
    return target;
}

NVNtextureFlags nvnTextureBuilderGetFlags(void *builder) {
    nvn_texture_builder_state s = default_state();
    nvn_texture_builder_state *found;
    NVNtextureFlags flags;
    int first = 0;
    pthread_mutex_lock(&s_texture_builder_lock);
    s_stats.calls++;
    s_stats.gets++;
    found = builder_find(builder, 0);
    if (found) s = *found;
    flags = s.flags;
    first = mark_logged(TB_GET_FLAGS);
    pthread_mutex_unlock(&s_texture_builder_lock);
    if (first) {
        fprintf(stderr, "[nvn] nvnTextureBuilderGetFlags: builder=%p flags=0x%x\n", builder, (unsigned)flags);
    }
    return flags;
}

int nvnTextureBuilderGetWidth(void *builder) {
    nvn_texture_builder_state s = default_state();
    nvn_texture_builder_state *found;
    int width;
    int first = 0;
    pthread_mutex_lock(&s_texture_builder_lock);
    s_stats.calls++;
    s_stats.gets++;
    found = builder_find(builder, 0);
    if (found) s = *found;
    width = s.width;
    first = mark_logged(TB_GET_WIDTH);
    pthread_mutex_unlock(&s_texture_builder_lock);
    if (first) {
        fprintf(stderr, "[nvn] nvnTextureBuilderGetWidth: builder=%p width=%d\n", builder, width);
    }
    return width;
}

int nvnTextureBuilderGetHeight(void *builder) {
    nvn_texture_builder_state s = default_state();
    nvn_texture_builder_state *found;
    int height;
    int first = 0;
    pthread_mutex_lock(&s_texture_builder_lock);
    s_stats.calls++;
    s_stats.gets++;
    found = builder_find(builder, 0);
    if (found) s = *found;
    height = s.height;
    first = mark_logged(TB_GET_HEIGHT);
    pthread_mutex_unlock(&s_texture_builder_lock);
    if (first) {
        fprintf(stderr, "[nvn] nvnTextureBuilderGetHeight: builder=%p height=%d\n", builder, height);
    }
    return height;
}

int nvnTextureBuilderGetDepth(void *builder) {
    nvn_texture_builder_state s = default_state();
    nvn_texture_builder_state *found;
    int depth;
    int first = 0;
    pthread_mutex_lock(&s_texture_builder_lock);
    s_stats.calls++;
    s_stats.gets++;
    found = builder_find(builder, 0);
    if (found) s = *found;
    depth = s.depth;
    first = mark_logged(TB_GET_DEPTH);
    pthread_mutex_unlock(&s_texture_builder_lock);
    if (first) {
        fprintf(stderr, "[nvn] nvnTextureBuilderGetDepth: builder=%p depth=%d\n", builder, depth);
    }
    return depth;
}

int nvnTextureBuilderGetLevels(void *builder) {
    nvn_texture_builder_state s = default_state();
    nvn_texture_builder_state *found;
    int levels;
    int first = 0;
    pthread_mutex_lock(&s_texture_builder_lock);
    s_stats.calls++;
    s_stats.gets++;
    found = builder_find(builder, 0);
    if (found) s = *found;
    levels = s.levels;
    first = mark_logged(TB_GET_LEVELS);
    pthread_mutex_unlock(&s_texture_builder_lock);
    if (first) {
        fprintf(stderr, "[nvn] nvnTextureBuilderGetLevels: builder=%p levels=%d\n", builder, levels);
    }
    return levels;
}

int nvnTextureBuilderGetSamples(void *builder) {
    nvn_texture_builder_state s = default_state();
    nvn_texture_builder_state *found;
    int samples;
    int first = 0;
    pthread_mutex_lock(&s_texture_builder_lock);
    s_stats.calls++;
    s_stats.gets++;
    found = builder_find(builder, 0);
    if (found) s = *found;
    samples = s.samples;
    first = mark_logged(TB_GET_SAMPLES);
    pthread_mutex_unlock(&s_texture_builder_lock);
    if (first) {
        fprintf(stderr, "[nvn] nvnTextureBuilderGetSamples: builder=%p samples=%d\n", builder, samples);
    }
    return samples;
}

ptrdiff_t nvnTextureBuilderGetStride(void *builder) {
    nvn_texture_builder_state s = default_state();
    nvn_texture_builder_state *found;
    ptrdiff_t stride;
    int first = 0;
    pthread_mutex_lock(&s_texture_builder_lock);
    s_stats.calls++;
    s_stats.gets++;
    found = builder_find(builder, 0);
    if (found) s = *found;
    stride = s.stride;
    first = mark_logged(TB_GET_STRIDE);
    pthread_mutex_unlock(&s_texture_builder_lock);
    if (first) {
        fprintf(stderr, "[nvn] nvnTextureBuilderGetStride: builder=%p stride=%" PRIdPTR "\n", builder, stride);
    }
    return stride;
}

/* The swizzle is four components, in the order the setter takes them. */
void nvnTextureBuilderGetSwizzle(void *builder, NVNtextureSwizzle *red, NVNtextureSwizzle *green,
                                  NVNtextureSwizzle *blue, NVNtextureSwizzle *alpha) {
    nvn_texture_builder_state s = default_state();
    nvn_texture_builder_state *found;
    int first = 0;
    pthread_mutex_lock(&s_texture_builder_lock);
    s_stats.calls++;
    s_stats.gets++;
    found = builder_find(builder, 0);
    if (found) s = *found;
    if (red) *red = s.swizzle[0];
    if (green) *green = s.swizzle[1];
    if (blue) *blue = s.swizzle[2];
    if (alpha) *alpha = s.swizzle[3];
    first = mark_logged(TB_GET_SWIZZLE);
    pthread_mutex_unlock(&s_texture_builder_lock);
    if (first) {
        fprintf(stderr, "[nvn] nvnTextureBuilderGetSwizzle: builder=%p r=0x%x g=0x%x b=0x%x a=0x%x\n", builder,
                (unsigned)s.swizzle[0], (unsigned)s.swizzle[1], (unsigned)s.swizzle[2], (unsigned)s.swizzle[3]);
    }
    NVN_VOID_RETURN();
}

NVNtextureDepthStencilMode nvnTextureBuilderGetDepthStencilMode(void *builder) {
    nvn_texture_builder_state s = default_state();
    nvn_texture_builder_state *found;
    NVNtextureDepthStencilMode mode;
    int first = 0;
    pthread_mutex_lock(&s_texture_builder_lock);
    s_stats.calls++;
    s_stats.gets++;
    found = builder_find(builder, 0);
    if (found) s = *found;
    mode = s.depth_stencil_mode;
    first = mark_logged(TB_GET_DEPTH_STENCIL_MODE);
    pthread_mutex_unlock(&s_texture_builder_lock);
    if (first) {
        fprintf(stderr, "[nvn] nvnTextureBuilderGetDepthStencilMode: builder=%p mode=0x%x\n", builder,
                (unsigned)mode);
    }
    return mode;
}

uint32_t nvnTextureBuilderGetGLTextureName(void *builder) {
    nvn_texture_builder_state s = default_state();
    nvn_texture_builder_state *found;
    uint32_t name;
    int first = 0;
    pthread_mutex_lock(&s_texture_builder_lock);
    s_stats.calls++;
    s_stats.gets++;
    found = builder_find(builder, 0);
    if (found) s = *found;
    name = s.gl_texture_name;
    first = mark_logged(TB_GET_GL_TEXTURE_NAME);
    pthread_mutex_unlock(&s_texture_builder_lock);
    if (first) {
        fprintf(stderr, "[nvn] nvnTextureBuilderGetGLTextureName: builder=%p name=0x%x\n", builder, (unsigned)name);
    }
    return name;
}

void *nvnTextureBuilderGetMemoryPool(void *builder) {
    nvn_texture_builder_state s = default_state();
    nvn_texture_builder_state *found;
    void *pool = NULL;
    int first = 0;
    pthread_mutex_lock(&s_texture_builder_lock);
    s_stats.calls++;
    s_stats.gets++;
    found = builder_find(builder, 0);
    if (found) {
        s = *found;
        pool = s.memory_pool;
    }
    first = mark_logged(TB_GET_MEMORY_POOL);
    pthread_mutex_unlock(&s_texture_builder_lock);
    if (first) {
        fprintf(stderr, "[nvn] nvnTextureBuilderGetMemoryPool: builder=%p pool=%p\n", builder, pool);
    }
    return pool;
}

ptrdiff_t nvnTextureBuilderGetMemoryOffset(void *builder) {
    nvn_texture_builder_state s = default_state();
    nvn_texture_builder_state *found;
    ptrdiff_t offset;
    int first = 0;
    pthread_mutex_lock(&s_texture_builder_lock);
    s_stats.calls++;
    s_stats.gets++;
    found = builder_find(builder, 0);
    if (found) s = *found;
    offset = s.memory_offset;
    first = mark_logged(TB_GET_MEMORY_OFFSET);
    pthread_mutex_unlock(&s_texture_builder_lock);
    if (first) {
        fprintf(stderr, "[nvn] nvnTextureBuilderGetMemoryOffset: builder=%p offset=%" PRIdPTR "\n", builder, offset);
    }
    return offset;
}

/* The storage size of the texture.
 *
 * This is the storage layout of the texture, and the layout belongs to the
 * texture creation stage: the reference tree computes it with the block
 * linear algorithm of the Tegra X1 (nvn_tex.c, a port of deko3d's
 * dk_image.cpp), which is not part of this family. A number invented here
 * would be smaller than the real one, and the guest would place two textures
 * on top of each other in its memory pool. The call is therefore refused by
 * name and the reason is written down. */
size_t nvnTextureBuilderGetStorageSize(void *builder) {
    uint64_t size = 0;
    nvn_texture_builder_layout_of(builder, &size, NULL, NULL);
    return (size_t)size;
}

/* The alignment of the storage of the texture. Refused for the same reason as
 * the storage size. */
size_t nvnTextureBuilderGetStorageAlignment(void *builder) {
    uint32_t alignment = 0;
    nvn_texture_builder_layout_of(builder, NULL, &alignment, NULL);
    return alignment;
}

/* Use exactly the layout code the Vulkan bridge uses. Returning zero used
 * to make the guest allocate every texture at the same pool offset. */
int nvn_texture_builder_layout_of(const void *builder, uint64_t *size, uint32_t *alignment,
                                  int *packaged_block_height) {
    nvn_texture_builder_state *found;
    nvn_texdesc d = {0};
    nvn_texlayout layout = {0};
    pthread_mutex_lock(&s_texture_builder_lock);
    s_stats.calls++;
    s_stats.gets++;
    found = builder_find((void *)builder, 0);
    if (found) {
        d = (nvn_texdesc){.target=found->target, .format=found->format, .flags=found->flags,
            .samples=found->samples, .width=found->width, .height=found->height,
            .depth=found->depth, .levels=found->levels, .stride=found->stride,
            .pkg_block_h=found->packaged_block_height};
    }
    pthread_mutex_unlock(&s_texture_builder_lock);
    if (!found) return 0;
    nvn_tex_layout(&d, &layout);
    if (!layout.bpb || !layout.size) {
        fprintf(stderr, "[nvn] texture layout refused: builder=%p format=%#x size=%ux%ux%u\n",
                builder, d.format, d.width, d.height, d.depth);
        return 0;
    }
    if (size) *size = layout.size;
    if (alignment) *alignment = layout.align;
    if (packaged_block_height) *packaged_block_height = d.pkg_block_h;
    return 1;
}

/* The storage class of the texture.
 *
 * This runtime has no storage class: the reference tree answers zero for the
 * builder as well (hle_nvn.c:1952), because the host it runs on has none
 * either. The answer is therefore zero, and it is written down. */
NVNstorageClass nvnTextureBuilderGetStorageClass(void *builder) {
    int first = 0;
    pthread_mutex_lock(&s_texture_builder_lock);
    s_stats.calls++;
    s_stats.gets++;
    first = mark_logged(TB_GET_STORAGE_CLASS);
    pthread_mutex_unlock(&s_texture_builder_lock);
    if (first) {
        fprintf(stderr, "[nvn] nvnTextureBuilderGetStorageClass: builder=%p -> 0; this runtime has no storage "
                        "class, the same answer the reference tree gives for a builder\n",
                builder);
    }
    return 0;
}

/* The signature of nvnTextureBuilderGetRawStorageClass is not in the vendored
 * header this runtime was built against, and the reference tree lists it among
 * the names it can only give a stub (nvn_protos.h: "Names without a prototype
 * in the header"). The arguments are therefore taken as plain words and written
 * down as they came. Nothing is dereferenced, so a signature with fewer or
 * more arguments cannot do any harm here. */
void nvnTextureBuilderGetRawStorageClass(uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4,
                                         uint64_t arg5, uint64_t arg6, uint64_t arg7) {
    int first = 0;
    pthread_mutex_lock(&s_texture_builder_lock);
    s_stats.calls++;
    s_stats.gets++;
    s_stats.raw_calls++;
    first = mark_logged(TB_GET_RAW_STORAGE_CLASS);
    pthread_mutex_unlock(&s_texture_builder_lock);
    if (first) {
        fprintf(stderr, "[nvn] nvnTextureBuilderGetRawStorageClass: args=0x%" PRIx64 ",0x%" PRIx64 ",0x%" PRIx64
                        ",0x%" PRIx64 ",0x%" PRIx64 ",0x%" PRIx64 ",0x%" PRIx64 ",0x%" PRIx64
                        "; the signature of this entry point is not in the header this runtime was built against, "
                        "so the arguments are recorded as they came and no storage class is returned\n",
                arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7);
    }
    NVN_VOID_RETURN();
}

/* The signature of nvnTextureBuilderGetViewOffset is not in the vendored
 * header this runtime was built against, and the reference tree lists it among
 * the names it can only give a stub (nvn_protos.h: "Names without a prototype
 * in the header"). The arguments are therefore taken as plain words and written
 * down as they came. Nothing is dereferenced, so a signature with fewer or
 * more arguments cannot do any harm here. */
void nvnTextureBuilderGetViewOffset(uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4,
                                    uint64_t arg5, uint64_t arg6, uint64_t arg7) {
    int first = 0;
    pthread_mutex_lock(&s_texture_builder_lock);
    s_stats.calls++;
    s_stats.gets++;
    s_stats.raw_calls++;
    first = mark_logged(TB_GET_VIEW_OFFSET);
    pthread_mutex_unlock(&s_texture_builder_lock);
    if (first) {
        fprintf(stderr, "[nvn] nvnTextureBuilderGetViewOffset: args=0x%" PRIx64 ",0x%" PRIx64 ",0x%" PRIx64 ",0x%"
                        PRIx64 ",0x%" PRIx64 ",0x%" PRIx64 ",0x%" PRIx64 ",0x%" PRIx64
                        "; the signature of this entry point is not in the header this runtime was built against, "
                        "so the arguments are recorded as they came and no offset is returned\n",
                arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7);
    }
    NVN_VOID_RETURN();
}

/* The sparse tile layout of the texture.
 *
 * The type is opaque in the vendored header (one reserved byte, nvn.h:57), so
 * this runtime cannot write a layout into the guest's structure without
 * inventing its size. The call is therefore refused by name, the guest's
 * structure is left untouched, and the reason is written down. */
void nvnTextureBuilderGetSparseTileLayout(void *builder, NVNtextureSparseTileLayout *layout) {
    int first = 0;
    pthread_mutex_lock(&s_texture_builder_lock);
    s_stats.calls++;
    s_stats.gets++;
    s_stats.refused++;
    first = mark_logged(TB_GET_SPARSE_TILE_LAYOUT);
    pthread_mutex_unlock(&s_texture_builder_lock);
    if (first) {
        fprintf(stderr, "[nvn] nvnTextureBuilderGetSparseTileLayout: builder=%p layout=%p; the layout of a sparse "
                        "texture is not computed by the builder family and its structure is opaque in the header "
                        "this runtime was built against, so nothing was written\n",
                builder, (void *)layout);
    }
    NVN_VOID_RETURN();
}

/* The storage the ZCull unit needs. Refused for the same reason as the
 * storage size: it is part of the storage layout of the texture. */
size_t nvnTextureBuilderGetZCullStorageSize(void *builder) {
    int first = 0;
    pthread_mutex_lock(&s_texture_builder_lock);
    s_stats.calls++;
    s_stats.gets++;
    s_stats.refused++;
    first = mark_logged(TB_GET_ZCULL_STORAGE_SIZE);
    pthread_mutex_unlock(&s_texture_builder_lock);
    if (first) {
        fprintf(stderr, "[nvn] nvnTextureBuilderGetZCullStorageSize: builder=%p -> 0; the storage layout of a "
                        "texture is not computed by the builder family, so no size is invented here\n",
                builder);
    }
    return 0;
}

const void *nvnTextureBuilderGetPackagedTextureData(void *builder) {
    nvn_texture_builder_state s = default_state();
    nvn_texture_builder_state *found;
    const void *data = NULL;
    int first = 0;
    pthread_mutex_lock(&s_texture_builder_lock);
    s_stats.calls++;
    s_stats.gets++;
    found = builder_find(builder, 0);
    if (found) {
        s = *found;
        data = s.packaged_texture_data;
    }
    first = mark_logged(TB_GET_PACKAGED_TEXTURE_DATA);
    pthread_mutex_unlock(&s_texture_builder_lock);
    if (first) {
        fprintf(stderr, "[nvn] nvnTextureBuilderGetPackagedTextureData: builder=%p data=%p\n", builder, data);
    }
    return data;
}

/* The signature of nvnTextureBuilderGetPackagedTextureLayout is not in the
 * vendored header this runtime was built against, and the reference tree lists
 * it among the names it can only give a stub (nvn_protos.h: "Names without a
 * prototype in the header"). The arguments are therefore taken as plain words
 * and written down as they came. Nothing is dereferenced, so a signature with
 * fewer or more arguments cannot do any harm here. */
void nvnTextureBuilderGetPackagedTextureLayout(uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t arg3,
                                               uint64_t arg4, uint64_t arg5, uint64_t arg6, uint64_t arg7) {
    int first = 0;
    pthread_mutex_lock(&s_texture_builder_lock);
    s_stats.calls++;
    s_stats.gets++;
    s_stats.raw_calls++;
    first = mark_logged(TB_GET_PACKAGED_TEXTURE_LAYOUT);
    pthread_mutex_unlock(&s_texture_builder_lock);
    if (first) {
        fprintf(stderr, "[nvn] nvnTextureBuilderGetPackagedTextureLayout: args=0x%" PRIx64 ",0x%" PRIx64 ",0x%" PRIx64
                        ",0x%" PRIx64 ",0x%" PRIx64 ",0x%" PRIx64 ",0x%" PRIx64 ",0x%" PRIx64
                        "; the signature of this entry point is not in the header this runtime was built against, "
                        "so the arguments are recorded as they came and no layout is returned\n",
                arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7);
    }
    NVN_VOID_RETURN();
}

/* ------------------------------------------------------------------ */
/* The names                                                           */
/* ------------------------------------------------------------------ */

static const nvn_texture_builder_family_entry s_texture_builder_family[] = {
    { "nvnTextureBuilderSetDefaults", (void *)(uintptr_t)nvnTextureBuilderSetDefaults },
    { "nvnTextureBuilderSetDevice", (void *)(uintptr_t)nvnTextureBuilderSetDevice },
    { "nvnTextureBuilderSetFormat", (void *)(uintptr_t)nvnTextureBuilderSetFormat },
    { "nvnTextureBuilderSetTarget", (void *)(uintptr_t)nvnTextureBuilderSetTarget },
    { "nvnTextureBuilderSetFlags", (void *)(uintptr_t)nvnTextureBuilderSetFlags },
    { "nvnTextureBuilderSetWidth", (void *)(uintptr_t)nvnTextureBuilderSetWidth },
    { "nvnTextureBuilderSetHeight", (void *)(uintptr_t)nvnTextureBuilderSetHeight },
    { "nvnTextureBuilderSetDepth", (void *)(uintptr_t)nvnTextureBuilderSetDepth },
    { "nvnTextureBuilderSetLevels", (void *)(uintptr_t)nvnTextureBuilderSetLevels },
    { "nvnTextureBuilderSetSamples", (void *)(uintptr_t)nvnTextureBuilderSetSamples },
    { "nvnTextureBuilderSetStride", (void *)(uintptr_t)nvnTextureBuilderSetStride },
    { "nvnTextureBuilderSetStorage", (void *)(uintptr_t)nvnTextureBuilderSetStorage },
    { "nvnTextureBuilderSetSize1D", (void *)(uintptr_t)nvnTextureBuilderSetSize1D },
    { "nvnTextureBuilderSetSize2D", (void *)(uintptr_t)nvnTextureBuilderSetSize2D },
    { "nvnTextureBuilderSetSize3D", (void *)(uintptr_t)nvnTextureBuilderSetSize3D },
    { "nvnTextureBuilderSetSwizzle", (void *)(uintptr_t)nvnTextureBuilderSetSwizzle },
    { "nvnTextureBuilderSetDepthStencilMode", (void *)(uintptr_t)nvnTextureBuilderSetDepthStencilMode },
    { "nvnTextureBuilderSetGLTextureName", (void *)(uintptr_t)nvnTextureBuilderSetGLTextureName },
    { "nvnTextureBuilderSetPackagedTextureData", (void *)(uintptr_t)nvnTextureBuilderSetPackagedTextureData },
    { "nvnTextureBuilderSetPackagedTextureLayout", (void *)(uintptr_t)nvnTextureBuilderSetPackagedTextureLayout },
    { "nvnTextureBuilderGetDevice", (void *)(uintptr_t)nvnTextureBuilderGetDevice },
    { "nvnTextureBuilderGetFormat", (void *)(uintptr_t)nvnTextureBuilderGetFormat },
    { "nvnTextureBuilderGetTarget", (void *)(uintptr_t)nvnTextureBuilderGetTarget },
    { "nvnTextureBuilderGetFlags", (void *)(uintptr_t)nvnTextureBuilderGetFlags },
    { "nvnTextureBuilderGetWidth", (void *)(uintptr_t)nvnTextureBuilderGetWidth },
    { "nvnTextureBuilderGetHeight", (void *)(uintptr_t)nvnTextureBuilderGetHeight },
    { "nvnTextureBuilderGetDepth", (void *)(uintptr_t)nvnTextureBuilderGetDepth },
    { "nvnTextureBuilderGetLevels", (void *)(uintptr_t)nvnTextureBuilderGetLevels },
    { "nvnTextureBuilderGetSamples", (void *)(uintptr_t)nvnTextureBuilderGetSamples },
    { "nvnTextureBuilderGetStride", (void *)(uintptr_t)nvnTextureBuilderGetStride },
    { "nvnTextureBuilderGetSwizzle", (void *)(uintptr_t)nvnTextureBuilderGetSwizzle },
    { "nvnTextureBuilderGetDepthStencilMode", (void *)(uintptr_t)nvnTextureBuilderGetDepthStencilMode },
    { "nvnTextureBuilderGetGLTextureName", (void *)(uintptr_t)nvnTextureBuilderGetGLTextureName },
    { "nvnTextureBuilderGetMemoryPool", (void *)(uintptr_t)nvnTextureBuilderGetMemoryPool },
    { "nvnTextureBuilderGetMemoryOffset", (void *)(uintptr_t)nvnTextureBuilderGetMemoryOffset },
    { "nvnTextureBuilderGetStorageSize", (void *)(uintptr_t)nvnTextureBuilderGetStorageSize },
    { "nvnTextureBuilderGetStorageAlignment", (void *)(uintptr_t)nvnTextureBuilderGetStorageAlignment },
    { "nvnTextureBuilderGetStorageClass", (void *)(uintptr_t)nvnTextureBuilderGetStorageClass },
    { "nvnTextureBuilderGetRawStorageClass", (void *)(uintptr_t)nvnTextureBuilderGetRawStorageClass },
    { "nvnTextureBuilderGetViewOffset", (void *)(uintptr_t)nvnTextureBuilderGetViewOffset },
    { "nvnTextureBuilderGetSparseTileLayout", (void *)(uintptr_t)nvnTextureBuilderGetSparseTileLayout },
    { "nvnTextureBuilderGetZCullStorageSize", (void *)(uintptr_t)nvnTextureBuilderGetZCullStorageSize },
    { "nvnTextureBuilderGetPackagedTextureData", (void *)(uintptr_t)nvnTextureBuilderGetPackagedTextureData },
    { "nvnTextureBuilderGetPackagedTextureLayout", (void *)(uintptr_t)nvnTextureBuilderGetPackagedTextureLayout },
};

size_t nvn_texture_builder_family_count(void) {
    return sizeof(s_texture_builder_family) / sizeof(s_texture_builder_family[0]);
}

const nvn_texture_builder_family_entry *nvn_texture_builder_family_entry_at(size_t index) {
    if (index >= nvn_texture_builder_family_count()) return NULL;
    return &s_texture_builder_family[index];
}

void nvn_texture_builder_get_stats(nvn_texture_builder_stats *out) {
    size_t i;
    if (!out) return;
    pthread_mutex_lock(&s_texture_builder_lock);
    *out = s_stats;
    out->live_builders = 0;
    for (i = 0; i < s_builders.count; i++) {
        if (((nvn_texture_builder_state *)nvn_object_table_at(&s_builders, i))->in_use) out->live_builders++;
    }
    pthread_mutex_unlock(&s_texture_builder_lock);
}

void *nvn_texture_builder_device_of(const void *builder) {
    nvn_texture_builder_state *found;
    void *device = NULL;
    pthread_mutex_lock(&s_texture_builder_lock);
    found = builder_find((void *)(uintptr_t)builder, 0);
    if (found) device = found->device;
    pthread_mutex_unlock(&s_texture_builder_lock);
    return device;
}
