/* The NVN objects of this game in the layout the replay layer reads.
 *
 * The reference host keeps its NVN objects in place in the memory the guest
 * allocated, and the replay layer reads them there
 * (.local/refs/nxnative/dm/gfx/nvn/nvn_obj.h). This runtime keeps the state
 * of the objects on the host, keyed by the guest's address, so the objects
 * the replay layer sees are copies that this runtime makes.
 *
 * The copies are made in the layout of the reference, because that is the
 * layout the Vulkan engine that replays the commands was written against:
 * the engine reads a texture's description out of the copy and takes the
 * shader's control section out of the program's copy. A copy in another
 * layout would be read as another object.
 *
 * The sizes of these structures are the reference's own: they were measured
 * by compiling against the reference's headers, and the static assertions
 * below hold those numbers in place. The Vulkan engine's copy of the same
 * layout is checked against this one where the two meet.
 */
#ifndef FFRS_NVN_OBJ_FFRS_H
#define FFRS_NVN_OBJ_FFRS_H

#include <stddef.h>
#include <stdint.h>

/* The magic of an object, so that a pointer that is not an object of this
 * kind is refused instead of read. */
#define FFRS_NVN_MAGIC(a, b, c, d) \
    ((uint32_t)(a) | (uint32_t)(b) << 8 | (uint32_t)(c) << 16 | (uint32_t)(d) << 24)

/* The description of a texture: the format, the shape and the flags the
 * guest gave it (nvn_tex.h of the reference). */
typedef struct ffrs_texdesc {
    int target, format, flags, samples;
    uint32_t width, height, depth, levels, stride;
    int pkg_block_h;
} ffrs_texdesc;

/* The size of the storage of a texture (nvn_tex.h of the reference). */
typedef struct ffrs_texlayout {
    uint64_t size, layer_size;
    uint32_t align, layers, bpb;
    unsigned tileH, tileD;
} ffrs_texlayout;

/* A memory pool: the memory the guest gave the pool and its size. */
typedef struct ffrs_pool {
    uint32_t magic, flags;
    uint8_t *mem;
    uint64_t size;
} ffrs_pool;

/* A descriptor pool: the memory pool the descriptors live in, where in it,
 * and how many descriptors it holds. */
typedef struct ffrs_dpool {
    const ffrs_pool *pool;
    int64_t off;
    int32_t n;
} ffrs_dpool;

/* A texture: what the guest's texture object says, where its storage is,
 * and the number that tells the replay layer which host texture it is.
 *
 * The layout is the newer SDK's, whose view object is sixteen bytes; the
 * reference packs the swizzle and the depth-stencil mode into the bytes
 * that the older layout used for the levels and the layers. */
typedef struct ffrs_tex {
    uint32_t magic;
    ffrs_texdesc d;
    uint8_t has_layout, set_swz : 1, stencil : 1;
    uint16_t swz;
    ffrs_texlayout L;
    uint8_t *mem;
    ffrs_pool *pool;
    int64_t off;
    uint8_t layout[8];
    uint64_t serial;
} ffrs_tex;

/* A view of a texture: the levels, the layers, the format and the target it
 * shows, packed the way the newer SDK packs them. */
typedef struct ffrs_view {
    uint8_t set_levels : 1, set_layers : 1, set_format : 1, set_target : 1, set_swizzle : 1,
        set_dsmode : 1, dsmode : 2;
    uint8_t target, base_level, nlevels;
    uint16_t format, min_layer, nlayers;
    uint8_t swz[4];
    uint16_t _rsv;
} ffrs_view;

/* The parameters of a sampler. */
typedef struct ffrs_samp_par {
    uint8_t minf, magf, ws, wt, wr, cmp_mode, cmp_func, reduction;
    float lod_min, lod_max, lod_bias, aniso, border[4];
} ffrs_samp_par;

/* A sync object: whether it has been fenced and the value it was fenced
 * with. The replay layer writes the fence into this object when it reaches
 * the point of the command stream where the guest fenced it, so the host and
 * the replay layer have to be looking at the same object. */
typedef struct ffrs_sync {
    uint32_t magic, fenced;
    uint64_t fence;
} ffrs_sync;

_Static_assert(sizeof(ffrs_sync) == 16, "the sync object changed");

/* The shader of one stage: the address of its code area and the control
 * section that describes it. This is the NVN shader data of the guest, whose
 * own header cannot be included here: the memory pool header of this runtime
 * defines macros with the names of the enumerators of that header, and
 * whichever of the two is read second does not compile. */
typedef struct ffrs_shader_data {
    uint64_t data;
    const void *control;
} ffrs_shader_data;

/* A program: the shaders of its stages and the number that tells the replay
 * layer which host program it is. */
typedef struct ffrs_prog {
    uint32_t magic, nstage;
    ffrs_shader_data sd[6];
    uint64_t serial;
} ffrs_prog;

/* The sizes the reference's own headers give these structures. */
_Static_assert(sizeof(ffrs_texdesc) == 40, "the texture description changed");
_Static_assert(sizeof(ffrs_texlayout) == 40, "the texture layout changed");
_Static_assert(sizeof(ffrs_pool) == 24, "the memory pool changed");
_Static_assert(sizeof(ffrs_dpool) == 24, "the descriptor pool changed");
_Static_assert(sizeof(ffrs_tex) == 128, "the texture object changed");
_Static_assert(sizeof(ffrs_view) == 16, "the texture view changed");
_Static_assert(sizeof(ffrs_samp_par) == 40, "the sampler parameters changed");
_Static_assert(sizeof(ffrs_prog) == 112, "the program object changed");
_Static_assert(sizeof(ffrs_shader_data) == 16, "the shader data changed");
_Static_assert(offsetof(ffrs_tex, d) == 4, "the texture description moved");
_Static_assert(offsetof(ffrs_tex, L) == 48, "the texture layout moved");
_Static_assert(offsetof(ffrs_tex, mem) == 88, "the texture storage moved");
_Static_assert(offsetof(ffrs_tex, pool) == 96, "the texture pool moved");
_Static_assert(offsetof(ffrs_tex, off) == 104, "the texture offset moved");
_Static_assert(offsetof(ffrs_tex, serial) == 120, "the texture serial moved");
_Static_assert(offsetof(ffrs_prog, sd) == 8, "the shader data moved");
_Static_assert(offsetof(ffrs_prog, serial) == 104, "the program serial moved");
_Static_assert(offsetof(ffrs_dpool, off) == 8, "the pool offset moved");
_Static_assert(offsetof(ffrs_dpool, n) == 16, "the pool count moved");

/* ------------------------------------------------------------------ */
/* What the families know                                              */
/* ------------------------------------------------------------------ */

/* Each family reports what it knows about one of its objects. The state of
 * a family is the family's own, so it is the family that answers; the bridge
 * that assembles the objects above is a separate thing. */

/* What a texture is: its description and where its storage is. */
typedef struct nvn_texture_info {
    ffrs_texdesc d;
    const void *memory_pool; /* the guest's memory pool that holds the storage */
    int64_t memory_offset;   /* where in that pool the storage starts */
    uint64_t serial;         /* the generation of the texture object */
} nvn_texture_info;

/* What a program is: how many stages it has and, per stage, the address of
 * the code area and the control section that describes it. */
typedef struct nvn_program_info {
    uint32_t nstage;
    ffrs_shader_data sd[6];
    uint64_t serial; /* the generation of the program object */
} nvn_program_info;

/* What a memory pool is: the memory the guest gave it and its size. */
typedef struct nvn_memory_pool_info {
    uint8_t *mem;
    uint64_t size;
    uint32_t flags;
} nvn_memory_pool_info;

/* What a descriptor pool is: the memory pool it is bound to and how many
 * descriptors it holds. */
typedef struct nvn_pool_info {
    const void *memory_pool;
    int64_t memory_offset;
    int32_t size;
} nvn_pool_info;

int nvn_texture_info_of(const void *texture, nvn_texture_info *out);
int nvn_program_info_of(const void *program, nvn_program_info *out);
int nvn_memory_pool_info_of(const void *pool, nvn_memory_pool_info *out);
int nvn_texture_pool_info_of(const void *pool, nvn_pool_info *out);
int nvn_sampler_pool_info_of(const void *pool, nvn_pool_info *out);
int nvn_sampler_info_of(const void *sampler, ffrs_samp_par *out, uint64_t *serial);
int nvn_texture_view_info_of(const void *view, ffrs_view *out, uint64_t *serial);

#endif /* FFRS_NVN_OBJ_FFRS_H */

