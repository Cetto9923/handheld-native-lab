/* The contract of the NVN texture object family.
 *
 * A texture is what the guest makes out of a texture builder: it fills a
 * builder, hands it to nvnTextureInitialize, and from then on the texture is
 * the object its draws refer to. This case checks that the texture really
 * holds the description the guest put into the builder, that the guest's own
 * texture memory is not written, that a texture is a snapshot of the builder it
 * was made from, that two textures can be compared, and that the three device
 * handle functions hand out the numbers of the descriptors and not pointers.
 *
 * This case is in a translation unit of its own, with its own main, so that
 * two families can be worked on at the same time without writing into the same
 * file. The build script finds it by its name and runs it as a separate
 * contract binary.
 *
 * The family is driven through the guest's own lookup, the way the guest
 * drives it: the bootstrap hands out nvnDeviceGetProcAddress, and every name
 * of the family is resolved through it. The implementation's functions are not
 * called directly, so that a name that the device does not hand out is a
 * failure of the contract and not a silent success.
 *
 * The expectations are written out as plain numbers, not as the constants of
 * the implementation, so that a change of the implementation that moves a
 * value is caught here.
 */
#define _GNU_SOURCE
#include "../../src/runtime/hle/guest_process.h"
#include "../../src/graphics/nvn_device/nvn_texture.h"
#include "../../src/graphics/nvn_device/nvn_texture_view.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do {if(!(x)) {fprintf(stderr,"texture contract line=%d: %s\n",__LINE__,#x);return 1;}} while(0)

static unsigned errors;

static void expect_value(const char *tag,uint64_t actual,uint64_t expected) {
    if(actual!=expected) {
        errors++;
        fprintf(stderr,"[check-fail] %s actual=0x%" PRIx64 " expected=0x%" PRIx64 "\n",tag,actual,expected);
    }
}

static void expect_condition(const char *tag,int condition) {
    if(!condition) {errors++;fprintf(stderr,"[check-fail] %s\n",tag);}
}

static int all_bytes(const void *p,size_t n,unsigned char value) {
    const unsigned char *b=p;for(size_t i=0;i<n;i++)if(b[i]!=value)return 0;return 1;
}

typedef void *(*bootstrap_fn)(const char *);
typedef void *(*lookup_fn)(void *, const char *);
typedef uint64_t (*init_fn)(void *, void *);

/* The thirty one entry points of the texture family. The argument types are
 * the ones of the vendored NVN header (src/graphics/nvn/include/nvn/nvn.h:
 * 1320-1360); the enumerations are carried as plain words here, because the
 * values that matter are the numbers themselves. */
typedef int (*texture_initialize_fn)(void *, const void *);
typedef void (*texture_finalize_fn)(void *);
typedef void (*texture_set_debug_label_fn)(void *, const char *);
typedef uint64_t (*texture_get_debug_id_fn)(const void *);
typedef uint32_t (*texture_get_flags_fn)(const void *);
typedef uint32_t (*texture_get_target_fn)(const void *);
typedef uint32_t (*texture_get_format_fn)(const void *);
typedef int (*texture_get_int_fn)(const void *);
typedef ptrdiff_t (*texture_get_offset_fn)(const void *);
typedef void *(*texture_get_pointer_fn)(const void *);
typedef uint64_t (*texture_get_address_fn)(const void *);
typedef ptrdiff_t (*texture_get_view_offset_fn)(const void *, const void *);
typedef int (*texture_get_storage_class_fn)(const void *);
typedef int (*texture_get_raw_storage_class_fn)(const void *);
typedef size_t (*texture_get_storage_size_fn)(const void *);
typedef size_t (*texture_get_zcull_storage_size_fn)(const void *);
typedef void (*texture_get_swizzle_fn)(const void *, uint32_t *, uint32_t *, uint32_t *, uint32_t *);
typedef uint32_t (*texture_get_dsmode_fn)(const void *);
typedef void (*texture_get_sparse_tile_layout_fn)(const void *, void *);
typedef int (*texture_compare_fn)(const void *, const void *);
typedef void (*texture_texels_fn)(const void *, const void *, const void *, void *);
typedef void (*texture_texels_strided_fn)(const void *, const void *, const void *, void *, ptrdiff_t, ptrdiff_t);
typedef void (*texture_write_fn)(const void *, const void *, const void *, const void *);
typedef void (*texture_write_strided_fn)(const void *, const void *, const void *, const void *, ptrdiff_t, ptrdiff_t);
typedef void (*texture_flush_fn)(const void *, const void *, const void *);

/* The three device handle functions (nvn.h:1130, 1132, 1151). The separate
 * texture handle is a structure of one word in the vendored header, and the
 * reference host returns it by value as well (hle_nvn.c:183). */
typedef uint64_t (*device_get_texture_handle_fn)(const void *, int, int);
typedef uint64_t (*device_get_image_handle_fn)(const void *, int);
typedef struct {
    uint64_t value;
} separate_texture_handle;
typedef separate_texture_handle (*device_get_separate_texture_handle_fn)(const void *, int);
typedef separate_texture_handle (*device_get_separate_sampler_handle_fn)(const void *, int);
typedef uint64_t (*device_get_texel_fetch_handle_fn)(const void *, int);

/* The builder family, which the texture is made from. Its signatures are the
 * ones of the same header (lines 1252-1277). */
typedef void (*builder_set_defaults_fn)(void *);
typedef void (*builder_set_device_fn)(void *, void *);
typedef void (*builder_set_format_fn)(void *, uint32_t);
typedef void (*builder_set_target_fn)(void *, uint32_t);
typedef void (*builder_set_flags_fn)(void *, int);
typedef void (*builder_set_dimension_fn)(void *, int);
typedef void (*builder_set_stride_fn)(void *, ptrdiff_t);
typedef void (*builder_set_storage_fn)(void *, void *, ptrdiff_t);
typedef void (*builder_set_swizzle_fn)(void *, uint32_t, uint32_t, uint32_t, uint32_t);
typedef void (*builder_set_dsmode_fn)(void *, uint32_t);

/* The memory pool family, so that the texture can live in a real pool. */
typedef void (*pool_builder_set_defaults_fn)(void *);
typedef void (*pool_builder_set_device_fn)(void *, void *);
typedef void (*pool_builder_set_storage_fn)(void *, void *, uint64_t);
typedef int (*pool_initialize_fn)(void *, const void *);
typedef uint64_t (*pool_get_buffer_address_fn)(const void *);

/* The values the guest puts into a builder in this case. They are the
 * enumeration values of the vendored header, written out:
 *   format 0x38 = RGBA8_SRGB, target 0x8 = CUBEMAP,
 *   flags 0x50 = LINEAR | LINEAR_RENDER_TARGET,
 *   swizzle 0x0 = ZERO, 0x1 = ONE, 0x2 = R, 0x5 = A,
 *   depth stencil mode 0x1 = STENCIL. */
#define TEST_FORMAT 0x38u
#define TEST_TARGET 0x8u
#define TEST_FLAGS 0x50
#define TEST_WIDTH 256
#define TEST_HEIGHT 128
#define TEST_DEPTH 4
#define TEST_LEVELS 5
#define TEST_SAMPLES 2
#define TEST_STRIDE 1024
#define TEST_MEMORY_OFFSET 4096
#define TEST_SWIZZLE_R 0x0u
#define TEST_SWIZZLE_G 0x1u
#define TEST_SWIZZLE_B 0x2u
#define TEST_SWIZZLE_A 0x5u
#define TEST_DSMODE 0x1u

/* A memory pool is built from memory that the guest itself allocated and needs
 * a device that really exists, so on a build without a graphics device there is
 * no pool. The same rule the memory pool contract of this runtime uses. */
#if defined(__ANDROID__)
#define NVN_TEXTURE_EXPECT_REAL_POOL 1
#else
#define NVN_TEXTURE_EXPECT_REAL_POOL 0
#endif

/* The ZCull save area of a texture of 256 by 128 by 4: eight tiles of 32 by 32
 * in a row, four of them in a column, four layers, eight bytes per tile,
 * rounded up to 256 bytes: 1024 bytes. The formula is the one of the
 * reference host (hle_nvn.c:489). */
#define TEST_ZCULL_SIZE 1024u

/* Fill a builder with the values of this case. */
static void fill_builder(builder_set_defaults_fn set_defaults, builder_set_device_fn set_device,
                         builder_set_format_fn set_format, builder_set_target_fn set_target,
                         builder_set_flags_fn set_flags, builder_set_dimension_fn set_width,
                         builder_set_dimension_fn set_height, builder_set_dimension_fn set_depth,
                         builder_set_dimension_fn set_levels, builder_set_dimension_fn set_samples,
                         builder_set_stride_fn set_stride, builder_set_storage_fn set_storage,
                         builder_set_swizzle_fn set_swizzle, builder_set_dsmode_fn set_dsmode,
                         void *builder, void *device, void *memory_pool) {
    set_defaults(builder);
    set_device(builder, device);
    set_format(builder, TEST_FORMAT);
    set_target(builder, TEST_TARGET);
    set_flags(builder, TEST_FLAGS);
    set_width(builder, TEST_WIDTH);
    set_height(builder, TEST_HEIGHT);
    set_depth(builder, TEST_DEPTH);
    set_levels(builder, TEST_LEVELS);
    set_samples(builder, TEST_SAMPLES);
    set_stride(builder, TEST_STRIDE);
    set_storage(builder, memory_pool, TEST_MEMORY_OFFSET);
    set_swizzle(builder, TEST_SWIZZLE_R, TEST_SWIZZLE_G, TEST_SWIZZLE_B, TEST_SWIZZLE_A);
    set_dsmode(builder, TEST_DSMODE);
}

static int nvn_texture_contract(void) {
    bootstrap_fn bootstrap;
    lookup_fn lookup;
    init_fn init;
    texture_initialize_fn texture_initialize;
    texture_finalize_fn texture_finalize;
    texture_set_debug_label_fn texture_set_debug_label;
    texture_get_debug_id_fn texture_get_debug_id;
    texture_get_flags_fn texture_get_flags;
    texture_get_target_fn texture_get_target;
    texture_get_format_fn texture_get_format;
    texture_get_int_fn texture_get_width;
    texture_get_int_fn texture_get_height;
    texture_get_int_fn texture_get_depth;
    texture_get_int_fn texture_get_levels;
    texture_get_int_fn texture_get_samples;
    texture_get_offset_fn texture_get_stride;
    texture_get_pointer_fn texture_get_memory_pool;
    texture_get_offset_fn texture_get_memory_offset;
    texture_get_address_fn texture_get_texture_address;
    texture_get_view_offset_fn texture_get_view_offset;
    texture_get_storage_class_fn texture_get_storage_class;
    texture_get_raw_storage_class_fn texture_get_raw_storage_class;
    texture_get_storage_size_fn texture_get_storage_size;
    texture_get_zcull_storage_size_fn texture_get_zcull_storage_size;
    texture_get_swizzle_fn texture_get_swizzle;
    texture_get_dsmode_fn texture_get_depth_stencil_mode;
    texture_get_sparse_tile_layout_fn texture_get_sparse_tile_layout;
    texture_compare_fn texture_compare;
    texture_texels_fn texture_read_texels;
    texture_texels_strided_fn texture_read_texels_strided;
    texture_write_fn texture_write_texels;
    texture_write_strided_fn texture_write_texels_strided;
    texture_flush_fn texture_flush_texels;
    device_get_texture_handle_fn device_get_texture_handle;
    device_get_image_handle_fn device_get_image_handle;
    device_get_separate_texture_handle_fn device_get_separate_texture_handle;
    device_get_separate_sampler_handle_fn device_get_separate_sampler_handle;
    device_get_texel_fetch_handle_fn device_get_texel_fetch_handle;
    builder_set_defaults_fn builder_set_defaults;
    builder_set_device_fn builder_set_device;
    builder_set_format_fn builder_set_format;
    builder_set_target_fn builder_set_target;
    builder_set_flags_fn builder_set_flags;
    builder_set_dimension_fn builder_set_width;
    builder_set_dimension_fn builder_set_height;
    builder_set_dimension_fn builder_set_depth;
    builder_set_dimension_fn builder_set_levels;
    builder_set_dimension_fn builder_set_samples;
    builder_set_stride_fn builder_set_stride;
    builder_set_storage_fn builder_set_storage;
    builder_set_swizzle_fn builder_set_swizzle;
    builder_set_dsmode_fn builder_set_dsmode;
    pool_builder_set_defaults_fn pool_builder_set_defaults;
    pool_builder_set_device_fn pool_builder_set_device;
    pool_builder_set_storage_fn pool_builder_set_storage;
    pool_initialize_fn pool_initialize;
    pool_get_buffer_address_fn pool_get_buffer_address;
    nvn_texture_stats stats;
    /* The guest's own objects. The layout of a builder and of a texture is a
     * property of the Nintendo SDK, which this runtime does not have, so
     * nothing may be written into them. */
    unsigned char builder_one[64];
    unsigned char builder_two[64];
    unsigned char builder_three[64];
    unsigned char builder_unprepared[64];
    unsigned char texture_one[64];
    unsigned char texture_two[64];
    unsigned char texture_three[64];
    unsigned char texture_unknown[64];
    unsigned char pool_object[256];
    unsigned char pool_builder[64];
    unsigned char pool_storage[4096];
    unsigned char sparse_layout[8];
    uint32_t flags = 0;
    uint32_t target = 0;
    uint32_t format = 0;
    uint32_t swizzle_r = 0;
    uint32_t swizzle_g = 0;
    uint32_t swizzle_b = 0;
    uint32_t swizzle_a = 0;
    uint32_t dsmode = 0;
    uint64_t address = 0;
    uint64_t pool_address = 0;
    uint64_t debug_id_one = 0;
    uint64_t debug_id_two = 0;
    uint64_t handle = 0;
    separate_texture_handle separate;
    void *device = (void *)(uintptr_t)0x9000;
    size_t count;
    size_t i;
    size_t j;

    memset(builder_one, 0xaa, sizeof(builder_one));
    memset(builder_two, 0xaa, sizeof(builder_two));
    memset(builder_three, 0xaa, sizeof(builder_three));
    memset(builder_unprepared, 0xaa, sizeof(builder_unprepared));
    memset(texture_one, 0xaa, sizeof(texture_one));
    memset(texture_two, 0xaa, sizeof(texture_two));
    memset(texture_three, 0xaa, sizeof(texture_three));
    memset(texture_unknown, 0xaa, sizeof(texture_unknown));
    memset(pool_object, 0xaa, sizeof(pool_object));
    memset(pool_builder, 0xaa, sizeof(pool_builder));
    memset(pool_storage, 0xcc, sizeof(pool_storage));
    memset(sparse_layout, 0xdd, sizeof(sparse_layout));

    /* 1. Every name of the three families resolves, resolves to the same
     *    entry point every time, and the family tables hold each of them
     *    once. There are thirty one texture names, fourteen view names and
     *    three device handle names: forty eight in all. */
    bootstrap = (bootstrap_fn)(uintptr_t)guest_process_resolve(0, "nvnBootstrapLoader");
    CHECK(bootstrap != NULL);
    lookup = (lookup_fn)(uintptr_t)bootstrap("nvnDeviceGetProcAddress");
    CHECK(lookup != NULL);
    count = nvn_texture_family_count();
    expect_condition("nvn-texture-family-has-thirty-one-names", count == 31);
    for (i = 0; i < count; i++) {
        const nvn_texture_family_entry *entry = nvn_texture_family_entry_at(i);
        void *resolved;
        CHECK(entry != NULL);
        CHECK(entry->name != NULL);
        resolved = lookup(NULL, entry->name);
        expect_condition("nvn-texture-name-resolves", resolved != NULL);
        expect_condition("nvn-texture-name-is-stable", resolved == lookup(NULL, entry->name));
        expect_condition("nvn-texture-name-matches-the-table", resolved == entry->fn);
        for (j = i + 1; j < count; j++) {
            const nvn_texture_family_entry *other = nvn_texture_family_entry_at(j);
            expect_condition("nvn-texture-family-holds-each-name-once",
                             other == NULL || other->name == NULL || strcmp(entry->name, other->name) != 0);
        }
    }
    {
        size_t handle_count = nvn_device_handle_family_count();
        /* The guest resolves five device handle functions, not three: the
         * separate sampler handle and the texel fetch handle were missing
         * from the first card and are answered now. */
        expect_condition("nvn-device-handle-family-has-five-names", handle_count == 5);
        {
            int has_separate_sampler = 0;
            int has_texel_fetch = 0;
            for (i = 0; i < handle_count; i++) {
                const nvn_device_handle_family_entry *entry = nvn_device_handle_family_entry_at(i);
                if (!entry || !entry->name) continue;
                if (!strcmp(entry->name, "nvnDeviceGetSeparateSamplerHandle")) has_separate_sampler = 1;
                if (!strcmp(entry->name, "nvnDeviceGetTexelFetchHandle")) has_texel_fetch = 1;
            }
            expect_condition("nvn-device-handle-family-has-the-separate-sampler-handle", has_separate_sampler);
            expect_condition("nvn-device-handle-family-has-the-texel-fetch-handle", has_texel_fetch);
        }
        for (i = 0; i < handle_count; i++) {
            const nvn_device_handle_family_entry *entry = nvn_device_handle_family_entry_at(i);
            void *resolved;
            CHECK(entry != NULL);
            CHECK(entry->name != NULL);
            resolved = lookup(NULL, entry->name);
            expect_condition("nvn-device-handle-name-resolves", resolved != NULL);
            expect_condition("nvn-device-handle-name-is-stable", resolved == lookup(NULL, entry->name));
            expect_condition("nvn-device-handle-name-matches-the-table", resolved == entry->fn);
            for (j = i + 1; j < handle_count; j++) {
                const nvn_device_handle_family_entry *other = nvn_device_handle_family_entry_at(j);
                expect_condition("nvn-device-handle-family-holds-each-name-once",
                                 other == NULL || other->name == NULL || strcmp(entry->name, other->name) != 0);
            }
        }
    }
    {
        size_t view_count = nvn_texture_view_family_count();
        expect_condition("nvn-texture-view-family-has-fourteen-names", view_count == 14);
        for (i = 0; i < view_count; i++) {
            const nvn_texture_view_family_entry *entry = nvn_texture_view_family_entry_at(i);
            void *resolved;
            CHECK(entry != NULL);
            CHECK(entry->name != NULL);
            resolved = lookup(NULL, entry->name);
            expect_condition("nvn-texture-view-name-resolves", resolved != NULL);
            expect_condition("nvn-texture-view-name-is-stable", resolved == lookup(NULL, entry->name));
            expect_condition("nvn-texture-view-name-matches-the-table", resolved == entry->fn);
            for (j = i + 1; j < view_count; j++) {
                const nvn_texture_view_family_entry *other = nvn_texture_view_family_entry_at(j);
                expect_condition("nvn-texture-view-family-holds-each-name-once",
                                 other == NULL || other->name == NULL || strcmp(entry->name, other->name) != 0);
            }
        }
    }
    /* No name of the three families is the name of another one. */
    {
        size_t handle_count = nvn_device_handle_family_count();
        size_t view_count = nvn_texture_view_family_count();
        for (i = 0; i < count; i++) {
            const char *name = nvn_texture_family_entry_at(i)->name;
            for (j = 0; j < handle_count; j++) {
                const char *other = nvn_device_handle_family_entry_at(j)->name;
                expect_condition("nvn-texture-and-handle-families-do-not-share-a-name", strcmp(name, other) != 0);
            }
            for (j = 0; j < view_count; j++) {
                const char *other = nvn_texture_view_family_entry_at(j)->name;
                expect_condition("nvn-texture-and-view-families-do-not-share-a-name", strcmp(name, other) != 0);
            }
        }
    }

    /* The family itself, through the guest's lookup. */
    texture_initialize = (texture_initialize_fn)(uintptr_t)lookup(NULL, "nvnTextureInitialize");
    texture_finalize = (texture_finalize_fn)(uintptr_t)lookup(NULL, "nvnTextureFinalize");
    texture_set_debug_label = (texture_set_debug_label_fn)(uintptr_t)lookup(NULL, "nvnTextureSetDebugLabel");
    texture_get_debug_id = (texture_get_debug_id_fn)(uintptr_t)lookup(NULL, "nvnTextureGetDebugID");
    texture_get_flags = (texture_get_flags_fn)(uintptr_t)lookup(NULL, "nvnTextureGetFlags");
    texture_get_target = (texture_get_target_fn)(uintptr_t)lookup(NULL, "nvnTextureGetTarget");
    texture_get_width = (texture_get_int_fn)(uintptr_t)lookup(NULL, "nvnTextureGetWidth");
    texture_get_height = (texture_get_int_fn)(uintptr_t)lookup(NULL, "nvnTextureGetHeight");
    texture_get_depth = (texture_get_int_fn)(uintptr_t)lookup(NULL, "nvnTextureGetDepth");
    texture_get_levels = (texture_get_int_fn)(uintptr_t)lookup(NULL, "nvnTextureGetLevels");
    texture_get_samples = (texture_get_int_fn)(uintptr_t)lookup(NULL, "nvnTextureGetSamples");
    texture_get_format = (texture_get_format_fn)(uintptr_t)lookup(NULL, "nvnTextureGetFormat");
    texture_get_stride = (texture_get_offset_fn)(uintptr_t)lookup(NULL, "nvnTextureGetStride");
    texture_get_memory_pool = (texture_get_pointer_fn)(uintptr_t)lookup(NULL, "nvnTextureGetMemoryPool");
    texture_get_memory_offset = (texture_get_offset_fn)(uintptr_t)lookup(NULL, "nvnTextureGetMemoryOffset");
    texture_get_texture_address = (texture_get_address_fn)(uintptr_t)lookup(NULL, "nvnTextureGetTextureAddress");
    texture_get_view_offset = (texture_get_view_offset_fn)(uintptr_t)lookup(NULL, "nvnTextureGetViewOffset");
    texture_get_storage_class = (texture_get_storage_class_fn)(uintptr_t)lookup(NULL, "nvnTextureGetStorageClass");
    texture_get_raw_storage_class =
        (texture_get_raw_storage_class_fn)(uintptr_t)lookup(NULL, "nvnTextureGetRawStorageClass");
    texture_get_storage_size = (texture_get_storage_size_fn)(uintptr_t)lookup(NULL, "nvnTextureGetStorageSize");
    texture_get_zcull_storage_size =
        (texture_get_zcull_storage_size_fn)(uintptr_t)lookup(NULL, "nvnTextureGetZCullStorageSize");
    texture_get_swizzle = (texture_get_swizzle_fn)(uintptr_t)lookup(NULL, "nvnTextureGetSwizzle");
    texture_get_depth_stencil_mode =
        (texture_get_dsmode_fn)(uintptr_t)lookup(NULL, "nvnTextureGetDepthStencilMode");
    texture_get_sparse_tile_layout =
        (texture_get_sparse_tile_layout_fn)(uintptr_t)lookup(NULL, "nvnTextureGetSparseTileLayout");
    texture_compare = (texture_compare_fn)(uintptr_t)lookup(NULL, "nvnTextureCompare");
    texture_read_texels = (texture_texels_fn)(uintptr_t)lookup(NULL, "nvnTextureReadTexels");
    texture_read_texels_strided = (texture_texels_strided_fn)(uintptr_t)lookup(NULL, "nvnTextureReadTexelsStrided");
    texture_write_texels = (texture_write_fn)(uintptr_t)lookup(NULL, "nvnTextureWriteTexels");
    texture_write_texels_strided = (texture_write_strided_fn)(uintptr_t)lookup(NULL, "nvnTextureWriteTexelsStrided");
    texture_flush_texels = (texture_flush_fn)(uintptr_t)lookup(NULL, "nvnTextureFlushTexels");
    CHECK(texture_initialize && texture_finalize && texture_set_debug_label && texture_get_debug_id);
    CHECK(texture_get_flags && texture_get_target && texture_get_width && texture_get_height);
    CHECK(texture_get_depth && texture_get_levels && texture_get_samples && texture_get_format);
    CHECK(texture_get_stride && texture_get_memory_pool && texture_get_memory_offset);
    CHECK(texture_get_texture_address && texture_get_view_offset && texture_get_storage_class);
    CHECK(texture_get_raw_storage_class && texture_get_storage_size && texture_get_zcull_storage_size);
    CHECK(texture_get_swizzle && texture_get_depth_stencil_mode && texture_get_sparse_tile_layout);
    CHECK(texture_compare && texture_read_texels && texture_read_texels_strided);
    CHECK(texture_write_texels && texture_write_texels_strided && texture_flush_texels);

    device_get_texture_handle = (device_get_texture_handle_fn)(uintptr_t)lookup(NULL, "nvnDeviceGetTextureHandle");
    device_get_image_handle = (device_get_image_handle_fn)(uintptr_t)lookup(NULL, "nvnDeviceGetImageHandle");
    device_get_separate_texture_handle =
        (device_get_separate_texture_handle_fn)(uintptr_t)lookup(NULL, "nvnDeviceGetSeparateTextureHandle");
    device_get_separate_sampler_handle =
        (device_get_separate_sampler_handle_fn)(uintptr_t)lookup(NULL, "nvnDeviceGetSeparateSamplerHandle");
    device_get_texel_fetch_handle =
        (device_get_texel_fetch_handle_fn)(uintptr_t)lookup(NULL, "nvnDeviceGetTexelFetchHandle");
    CHECK(device_get_texture_handle && device_get_image_handle && device_get_separate_texture_handle);
    CHECK(device_get_separate_sampler_handle && device_get_texel_fetch_handle);

    /* The builder family, through the same lookup. */
    builder_set_defaults = (builder_set_defaults_fn)(uintptr_t)lookup(NULL, "nvnTextureBuilderSetDefaults");
    builder_set_device = (builder_set_device_fn)(uintptr_t)lookup(NULL, "nvnTextureBuilderSetDevice");
    builder_set_format = (builder_set_format_fn)(uintptr_t)lookup(NULL, "nvnTextureBuilderSetFormat");
    builder_set_target = (builder_set_target_fn)(uintptr_t)lookup(NULL, "nvnTextureBuilderSetTarget");
    builder_set_flags = (builder_set_flags_fn)(uintptr_t)lookup(NULL, "nvnTextureBuilderSetFlags");
    builder_set_width = (builder_set_dimension_fn)(uintptr_t)lookup(NULL, "nvnTextureBuilderSetWidth");
    builder_set_height = (builder_set_dimension_fn)(uintptr_t)lookup(NULL, "nvnTextureBuilderSetHeight");
    builder_set_depth = (builder_set_dimension_fn)(uintptr_t)lookup(NULL, "nvnTextureBuilderSetDepth");
    builder_set_levels = (builder_set_dimension_fn)(uintptr_t)lookup(NULL, "nvnTextureBuilderSetLevels");
    builder_set_samples = (builder_set_dimension_fn)(uintptr_t)lookup(NULL, "nvnTextureBuilderSetSamples");
    builder_set_stride = (builder_set_stride_fn)(uintptr_t)lookup(NULL, "nvnTextureBuilderSetStride");
    builder_set_storage = (builder_set_storage_fn)(uintptr_t)lookup(NULL, "nvnTextureBuilderSetStorage");
    builder_set_swizzle = (builder_set_swizzle_fn)(uintptr_t)lookup(NULL, "nvnTextureBuilderSetSwizzle");
    builder_set_dsmode = (builder_set_dsmode_fn)(uintptr_t)lookup(NULL, "nvnTextureBuilderSetDepthStencilMode");
    CHECK(builder_set_defaults && builder_set_device && builder_set_format && builder_set_target);
    CHECK(builder_set_flags && builder_set_width && builder_set_height && builder_set_depth);
    CHECK(builder_set_levels && builder_set_samples && builder_set_stride && builder_set_storage);
    CHECK(builder_set_swizzle && builder_set_dsmode);

    /* The memory pool family, so that the texture lives in a real pool and the
     * address of the texture is a real address. */
    pool_builder_set_defaults = (pool_builder_set_defaults_fn)(uintptr_t)lookup(NULL, "nvnMemoryPoolBuilderSetDefaults");
    pool_builder_set_device = (pool_builder_set_device_fn)(uintptr_t)lookup(NULL, "nvnMemoryPoolBuilderSetDevice");
    pool_builder_set_storage = (pool_builder_set_storage_fn)(uintptr_t)lookup(NULL, "nvnMemoryPoolBuilderSetStorage");
    pool_initialize = (pool_initialize_fn)(uintptr_t)lookup(NULL, "nvnMemoryPoolInitialize");
    pool_get_buffer_address = (pool_get_buffer_address_fn)(uintptr_t)lookup(NULL, "nvnMemoryPoolGetBufferAddress");
    CHECK(pool_builder_set_defaults && pool_builder_set_device && pool_builder_set_storage);
    CHECK(pool_initialize && pool_get_buffer_address);

    /* 2. The device and the memory pool. A memory pool needs a device that
     *    really exists, so on a build without a graphics device there is no
     *    pool, and a texture that lives in it has no address. That is the
     *    same rule the memory pool contract of this runtime uses. */
    init = (init_fn)(uintptr_t)bootstrap("nvnDeviceInitialize");
    CHECK(init != NULL);
    init(device, NULL);
    pool_builder_set_defaults(pool_builder);
    pool_builder_set_device(pool_builder, device);
    pool_builder_set_storage(pool_builder, pool_storage, (uint64_t)sizeof(pool_storage));
    expect_value("nvn-texture-pool-init-matches-real-device", (uint64_t)(uint32_t)pool_initialize(pool_object, pool_builder),
                 NVN_TEXTURE_EXPECT_REAL_POOL);
    if (NVN_TEXTURE_EXPECT_REAL_POOL) {
        pool_address = pool_get_buffer_address(pool_object);
        expect_condition("nvn-texture-pool-has-a-buffer-address", pool_address != 0);
    }

    /* 3. A builder is filled, a texture is made out of it, and every value of
     *    the description is read back. */
    fill_builder(builder_set_defaults, builder_set_device, builder_set_format, builder_set_target, builder_set_flags,
                 builder_set_width, builder_set_height, builder_set_depth, builder_set_levels, builder_set_samples,
                 builder_set_stride, builder_set_storage, builder_set_swizzle, builder_set_dsmode, builder_one, device,
                 pool_object);
    expect_condition("nvn-texture-initialize-succeeds", texture_initialize(texture_one, builder_one) == 1);
    texture_set_debug_label(texture_one, "contract-texture-one");

    flags = texture_get_flags(texture_one);
    expect_value("nvn-texture-flags-round-trip", (uint64_t)flags, (uint64_t)TEST_FLAGS);
    target = texture_get_target(texture_one);
    expect_value("nvn-texture-target-round-trips", (uint64_t)target, (uint64_t)TEST_TARGET);
    format = texture_get_format(texture_one);
    expect_value("nvn-texture-format-round-trips", (uint64_t)format, (uint64_t)TEST_FORMAT);
    expect_value("nvn-texture-width-round-trips", (uint64_t)(uint32_t)texture_get_width(texture_one), (uint64_t)TEST_WIDTH);
    expect_value("nvn-texture-height-round-trips", (uint64_t)(uint32_t)texture_get_height(texture_one),
                 (uint64_t)TEST_HEIGHT);
    expect_value("nvn-texture-depth-round-trips", (uint64_t)(uint32_t)texture_get_depth(texture_one), (uint64_t)TEST_DEPTH);
    expect_value("nvn-texture-levels-round-trip", (uint64_t)(uint32_t)texture_get_levels(texture_one),
                 (uint64_t)TEST_LEVELS);
    expect_value("nvn-texture-samples-round-trip", (uint64_t)(uint32_t)texture_get_samples(texture_one),
                 (uint64_t)TEST_SAMPLES);
    expect_value("nvn-texture-stride-round-trips", (uint64_t)(uint32_t)texture_get_stride(texture_one),
                 (uint64_t)TEST_STRIDE);
    expect_condition("nvn-texture-memory-pool-round-trips", texture_get_memory_pool(texture_one) == pool_object);
    expect_value("nvn-texture-memory-offset-round-trips", (uint64_t)(uint32_t)texture_get_memory_offset(texture_one),
                 (uint64_t)TEST_MEMORY_OFFSET);
    texture_get_swizzle(texture_one, &swizzle_r, &swizzle_g, &swizzle_b, &swizzle_a);
    expect_value("nvn-texture-swizzle-r-round-trips", (uint64_t)swizzle_r, (uint64_t)TEST_SWIZZLE_R);
    expect_value("nvn-texture-swizzle-g-round-trips", (uint64_t)swizzle_g, (uint64_t)TEST_SWIZZLE_G);
    expect_value("nvn-texture-swizzle-b-round-trips", (uint64_t)swizzle_b, (uint64_t)TEST_SWIZZLE_B);
    expect_value("nvn-texture-swizzle-a-round-trips", (uint64_t)swizzle_a, (uint64_t)TEST_SWIZZLE_A);
    dsmode = texture_get_depth_stencil_mode(texture_one);
    expect_value("nvn-texture-depth-stencil-mode-round-trips", (uint64_t)dsmode, (uint64_t)TEST_DSMODE);

    /* The address of the texture is the address of the pool plus the offset of
     * the texture in it (hle_nvn.c:478). */
    address = texture_get_texture_address(texture_one);
    if (NVN_TEXTURE_EXPECT_REAL_POOL) {
        expect_value("nvn-texture-address-is-the-pool-address-plus-the-offset", address,
                     pool_address + (uint64_t)TEST_MEMORY_OFFSET);
    } else {
        /* Without a device there is no pool, so the texture has no address. */
        expect_value("nvn-texture-address-without-a-pool-is-zero", address, 0);
    }

    /* The values that this runtime cannot compute are the ones the reference
     * host answers with zero, and the raw storage class is the one the
     * reference host answers with 1 (hle_nvn.c:497). */
    expect_value("nvn-texture-storage-class-is-zero", (uint64_t)(uint32_t)texture_get_storage_class(texture_one), 0);
    expect_value("nvn-texture-raw-storage-class-is-one",
                 (uint64_t)(uint32_t)texture_get_raw_storage_class(texture_one), 1);
    expect_value("nvn-texture-storage-size-is-zero", (uint64_t)texture_get_storage_size(texture_one), 0);
    expect_value("nvn-texture-zcull-storage-size-matches-the-formula",
                 (uint64_t)texture_get_zcull_storage_size(texture_one), (uint64_t)TEST_ZCULL_SIZE);
    expect_value("nvn-texture-view-offset-is-zero-without-a-layout",
                 (uint64_t)(uint32_t)texture_get_view_offset(texture_one, texture_unknown), 0);

    /* The sparse tile layout is opaque in the header, so the structure of the
     * caller is left alone. */
    texture_get_sparse_tile_layout(texture_one, sparse_layout);
    expect_condition("nvn-texture-sparse-tile-layout-is-not-filled-in",
                     all_bytes(sparse_layout, sizeof(sparse_layout), 0xdd));

    /* 4. The guest's own objects were not written to. The layout of a texture
     *    is a property of the Nintendo SDK, so the runtime keeps its state on
     *    the host. */
    expect_condition("nvn-texture-builder-not-written", all_bytes(builder_one, sizeof(builder_one), 0xaa));
    expect_condition("nvn-texture-object-not-written", all_bytes(texture_one, sizeof(texture_one), 0xaa));
    expect_condition("nvn-texture-pool-object-not-written", all_bytes(pool_object, sizeof(pool_object), 0xaa));

    /* 5. A texture is a snapshot of the builder it was made from. The builder
     *    changes here, and the texture must not. */
    builder_set_format(builder_one, 0x1u);
    builder_set_target(builder_one, 0x2u);
    builder_set_flags(builder_one, 0x8);
    builder_set_width(builder_one, 64);
    builder_set_height(builder_one, 32);
    builder_set_depth(builder_one, 1);
    builder_set_levels(builder_one, 1);
    builder_set_samples(builder_one, 0);
    builder_set_stride(builder_one, 256);
    builder_set_storage(builder_one, (void *)(uintptr_t)0xdeadbeefULL, 0);
    builder_set_swizzle(builder_one, 0x2u, 0x3u, 0x4u, 0x5u);
    builder_set_dsmode(builder_one, 0x0u);
    format = texture_get_format(texture_one);
    expect_value("nvn-texture-snapshot-keeps-the-format", (uint64_t)format, (uint64_t)TEST_FORMAT);
    target = texture_get_target(texture_one);
    expect_value("nvn-texture-snapshot-keeps-the-target", (uint64_t)target, (uint64_t)TEST_TARGET);
    flags = texture_get_flags(texture_one);
    expect_value("nvn-texture-snapshot-keeps-the-flags", (uint64_t)flags, (uint64_t)TEST_FLAGS);
    expect_value("nvn-texture-snapshot-keeps-the-width", (uint64_t)(uint32_t)texture_get_width(texture_one),
                 (uint64_t)TEST_WIDTH);
    expect_value("nvn-texture-snapshot-keeps-the-height", (uint64_t)(uint32_t)texture_get_height(texture_one),
                 (uint64_t)TEST_HEIGHT);
    expect_value("nvn-texture-snapshot-keeps-the-depth", (uint64_t)(uint32_t)texture_get_depth(texture_one),
                 (uint64_t)TEST_DEPTH);
    expect_value("nvn-texture-snapshot-keeps-the-levels", (uint64_t)(uint32_t)texture_get_levels(texture_one),
                 (uint64_t)TEST_LEVELS);
    expect_value("nvn-texture-snapshot-keeps-the-samples", (uint64_t)(uint32_t)texture_get_samples(texture_one),
                 (uint64_t)TEST_SAMPLES);
    expect_value("nvn-texture-snapshot-keeps-the-stride", (uint64_t)(uint32_t)texture_get_stride(texture_one),
                 (uint64_t)TEST_STRIDE);
    expect_condition("nvn-texture-snapshot-keeps-the-memory-pool",
                     texture_get_memory_pool(texture_one) == pool_object);
    expect_value("nvn-texture-snapshot-keeps-the-memory-offset",
                 (uint64_t)(uint32_t)texture_get_memory_offset(texture_one), (uint64_t)TEST_MEMORY_OFFSET);
    texture_get_swizzle(texture_one, &swizzle_r, &swizzle_g, &swizzle_b, &swizzle_a);
    expect_value("nvn-texture-snapshot-keeps-the-swizzle-r", (uint64_t)swizzle_r, (uint64_t)TEST_SWIZZLE_R);
    expect_value("nvn-texture-snapshot-keeps-the-swizzle-a", (uint64_t)swizzle_a, (uint64_t)TEST_SWIZZLE_A);
    dsmode = texture_get_depth_stencil_mode(texture_one);
    expect_value("nvn-texture-snapshot-keeps-the-depth-stencil-mode", (uint64_t)dsmode, (uint64_t)TEST_DSMODE);

    /* 6. Two textures with the same description are the same, and two that
     *    differ in a field that takes part in the comparison are not. */
    fill_builder(builder_set_defaults, builder_set_device, builder_set_format, builder_set_target, builder_set_flags,
                 builder_set_width, builder_set_height, builder_set_depth, builder_set_levels, builder_set_samples,
                 builder_set_stride, builder_set_storage, builder_set_swizzle, builder_set_dsmode, builder_two, device,
                 pool_object);
    expect_condition("nvn-texture-initialize-succeeds-again", texture_initialize(texture_two, builder_two) == 1);
    expect_condition("nvn-texture-compare-equal", texture_compare(texture_one, texture_two) == 1);

    fill_builder(builder_set_defaults, builder_set_device, builder_set_format, builder_set_target, builder_set_flags,
                 builder_set_width, builder_set_height, builder_set_depth, builder_set_levels, builder_set_samples,
                 builder_set_stride, builder_set_storage, builder_set_swizzle, builder_set_dsmode, builder_three, device,
                 pool_object);
    builder_set_width(builder_three, 64);
    expect_condition("nvn-texture-initialize-succeeds-a-third-time",
                     texture_initialize(texture_three, builder_three) == 1);
    expect_condition("nvn-texture-compare-different", texture_compare(texture_one, texture_three) == 0);

    /* The debug label and the debug id are not part of the state of a texture,
     * so two textures that differ only in them are the same texture. */
    texture_set_debug_label(texture_two, "another-label");
    expect_condition("nvn-texture-compare-ignores-the-debug-label",
                     texture_compare(texture_one, texture_two) == 1);

    /* 7. The handles. A handle is the number of a descriptor inside a pool,
     *    not a pointer, and the numbering base of a pool is not part of it. */
    handle = device_get_texture_handle(device, 7, 3);
    expect_value("nvn-texture-handle-packs-the-texture-and-the-sampler", handle,
                 (uint64_t)7 | ((uint64_t)3 << 20));
    handle = device_get_texture_handle(device, 0, 0);
    expect_value("nvn-texture-handle-of-zero-is-zero", handle, 0);
    handle = device_get_image_handle(device, 11);
    expect_value("nvn-image-handle-is-the-number-alone", handle, 11);
    separate = device_get_separate_texture_handle(device, 11);
    expect_value("nvn-separate-texture-handle-is-the-number-alone", separate.value, 11);
    /* The sampler of a separate pair goes into bit twenty and upwards, so a
     * texture handle with a sampler is not the same as the separate texture
     * handle of the same texture. */
    handle = device_get_texture_handle(device, 11, 1);
    expect_condition("nvn-texture-handle-and-separate-texture-handle-differ", handle != (uint64_t)11);
    expect_value("nvn-texture-handle-does-not-carry-the-pool-base", device_get_texture_handle(device, 7, 3),
                 (uint64_t)7 | ((uint64_t)3 << 20));
    /* The separate handle of a sampler is its number counted from bit twenty
     * upwards, because the number of the texture of the pair lives in the
     * bits below. A texel fetch handle names a texture and nothing else. */
    separate = device_get_separate_sampler_handle(device, 11);
    expect_value("nvn-separate-sampler-handle-counts-from-bit-twenty", separate.value,
                 (uint64_t)11 << 20);
    expect_value("nvn-texel-fetch-handle-is-the-number-alone",
                 device_get_texel_fetch_handle(device, 11), 11);

    /* 8. The refusals. A texture that was never initialized does not exist,
     *    and the answers for it are the defaults and not values of a texture. */
    expect_condition("nvn-texture-initialize-refuses-a-null-texture",
                     texture_initialize(NULL, builder_one) == 0);
    expect_value("nvn-texture-width-of-an-unknown-texture-is-the-default",
                 (uint64_t)(uint32_t)texture_get_width(texture_unknown), 1);
    expect_value("nvn-texture-debug-id-of-an-unknown-texture-is-zero", texture_get_debug_id(texture_unknown), 0);
    expect_condition("nvn-texture-compare-refuses-an-unknown-texture",
                     texture_compare(texture_one, texture_unknown) == 0);
    expect_value("nvn-texture-raw-storage-class-of-an-unknown-texture-is-zero",
                 (uint64_t)(uint32_t)texture_get_raw_storage_class(texture_unknown), 0);

    /* A builder that was never prepared has no description of its own, so the
     * texture answers with the defaults of the builder family. */
    expect_condition("nvn-texture-initialize-takes-an-unprepared-builder",
                     texture_initialize(texture_unknown, builder_unprepared) == 1);
    expect_value("nvn-texture-of-an-unprepared-builder-has-the-default-width",
                 (uint64_t)(uint32_t)texture_get_width(texture_unknown), 1);

    /* 9. The texel entry points run and leave the buffers of the caller
     *    alone. They are refused by name, because this runtime has no created
     *    texture and no table of the block size of a format. */
    {
        unsigned char source[64];
        unsigned char destination[64];
        memset(source, 0x11, sizeof(source));
        memset(destination, 0x22, sizeof(destination));
        texture_read_texels(texture_one, texture_unknown, NULL, destination);
        expect_condition("nvn-texture-read-texels-leaves-the-destination-alone",
                         all_bytes(destination, sizeof(destination), 0x22));
        texture_read_texels_strided(texture_one, texture_unknown, NULL, destination, 0, 0);
        expect_condition("nvn-texture-read-texels-strided-leaves-the-destination-alone",
                         all_bytes(destination, sizeof(destination), 0x22));
        texture_write_texels(texture_one, texture_unknown, NULL, source);
        expect_condition("nvn-texture-write-texels-leaves-the-source-alone",
                         all_bytes(source, sizeof(source), 0x11));
        texture_write_texels_strided(texture_one, texture_unknown, NULL, source, 0, 0);
        expect_condition("nvn-texture-write-texels-strided-leaves-the-source-alone",
                         all_bytes(source, sizeof(source), 0x11));
        texture_flush_texels(texture_one, texture_unknown, NULL);
    }

    /* 10. The debug id of a texture that exists is not zero, and two textures
     *     do not share one. */
    debug_id_one = texture_get_debug_id(texture_one);
    debug_id_two = texture_get_debug_id(texture_two);
    expect_condition("nvn-texture-debug-id-is-not-zero", debug_id_one != 0);
    expect_condition("nvn-texture-debug-ids-are-per-texture", debug_id_one != debug_id_two);

    /* 11. Finalizing a texture takes it out of the runtime's table. */
    texture_finalize(texture_three);
    expect_condition("nvn-texture-compare-refuses-a-finalized-texture",
                     texture_compare(texture_one, texture_three) == 0);

    /* 12. The guest's objects are still untouched after all of that. */
    expect_condition("nvn-texture-builder-not-written-afterwards",
                     all_bytes(builder_one, sizeof(builder_one), 0xaa) &&
                         all_bytes(builder_two, sizeof(builder_two), 0xaa) &&
                         all_bytes(builder_three, sizeof(builder_three), 0xaa));
    expect_condition("nvn-texture-object-not-written-afterwards",
                     all_bytes(texture_one, sizeof(texture_one), 0xaa) &&
                         all_bytes(texture_two, sizeof(texture_two), 0xaa) &&
                         all_bytes(texture_three, sizeof(texture_three), 0xaa));

    /* 13. The counters. */
    nvn_texture_get_stats(&stats);
    expect_condition("nvn-texture-stats-count-calls", stats.calls > 0);
    expect_condition("nvn-texture-stats-count-initialized", stats.initialized >= 3);
    expect_condition("nvn-texture-stats-count-finalized", stats.finalized >= 1);
    expect_condition("nvn-texture-stats-count-gets", stats.gets > 0);
    expect_condition("nvn-texture-stats-count-compares", stats.compares >= 4);
    expect_condition("nvn-texture-stats-count-texel-calls", stats.texel_calls >= 5);
    expect_condition("nvn-texture-stats-count-refusals", stats.refused > 0);
    expect_condition("nvn-texture-stats-count-handle-calls", stats.handle_calls >= 6);
    expect_condition("nvn-texture-stats-have-live-textures", stats.live_textures >= 3);

    /* The final gate. Without it the contract could exit 0 while the
     * expectations had failed. */
    CHECK(!errors);
    printf("{\"nvn_texture_errors\":%u,\"names\":%zu,\"initialized\":%llu,\"finalized\":%llu,"
           "\"compared\":%llu,\"gets\":%llu,\"texel_calls\":%llu,\"refused\":%llu,\"handle_calls\":%llu,"
           "\"live_textures\":%d}\n",
           errors, count, (unsigned long long)stats.initialized, (unsigned long long)stats.finalized,
           (unsigned long long)stats.compares, (unsigned long long)stats.gets,
           (unsigned long long)stats.texel_calls, (unsigned long long)stats.refused,
           (unsigned long long)stats.handle_calls, stats.live_textures);
    return 0;
}

int main(int argc,char **argv) {
    if(argc!=2 || strcmp(argv[1],"--nvn-texture")) {
        fprintf(stderr,"usage: %s --nvn-texture\n",argv[0]);
        return 2;
    }
    return nvn_texture_contract();
}
