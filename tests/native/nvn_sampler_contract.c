/* The contract of the NVN sampler object family.
 *
 * A sampler is what the guest makes out of a sampler builder: it fills a
 * builder, hands it to nvnSamplerInitialize, and from then on the sampler is
 * the object its draws refer to. This case checks that the sampler really
 * holds the parameters the guest put into the builder, that the guest's own
 * sampler memory is not written, that a sampler is a snapshot of the builder
 * it was made from, and that two samplers can be compared.
 *
 * This case is in a translation unit of its own, with its own main, so that
 * two families can be worked on at the same time without writing into the
 * same file. The build script finds it by its name and runs it as a second
 * contract binary.
 *
 * The family is driven through the guest's own lookup, the way the guest
 * drives it: the bootstrap hands out nvnDeviceGetProcAddress, and every name
 * of the family is resolved through it. The implementation's functions are
 * not called directly, so that a name that the device does not hand out is a
 * failure of the contract and not a silent success.
 *
 * The expectations are written out as plain numbers, not as the constants of
 * the implementation, so that a change of the implementation that moves a
 * value is caught here.
 */
#define _GNU_SOURCE
#include "../../src/runtime/hle/guest_process.h"
#include "../../src/graphics/nvn_device/nvn_sampler.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do {if(!(x)) {fprintf(stderr,"sampler contract line=%d: %s\n",__LINE__,#x);return 1;}} while(0)

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

/* The bits of a float, so that a float can be compared with expect_value. */
static uint64_t float_bits(float value) {
    uint32_t bits;
    memcpy(&bits,&value,sizeof(bits));
    return (uint64_t)bits;
}

typedef void *(*bootstrap_fn)(const char *);
typedef void *(*lookup_fn)(void *, const char *);

/* The fifteen entry points of the sampler family. The argument types are the
 * ones of the vendored NVN header (src/graphics/nvn/include/nvn/nvn.h:
 * 1391-1406); the enumerations are carried as plain words here, because the
 * values that matter are the numbers themselves. */
typedef int (*sampler_initialize_fn)(void *, const void *);
typedef void (*sampler_finalize_fn)(void *);
typedef void (*sampler_set_debug_label_fn)(void *, const char *);
typedef void (*sampler_get_min_mag_fn)(const void *, uint32_t *, uint32_t *);
typedef void (*sampler_get_wrap_fn)(const void *, uint32_t *, uint32_t *, uint32_t *);
typedef void (*sampler_get_lod_clamp_fn)(const void *, float *, float *);
typedef float (*sampler_get_lod_bias_fn)(const void *);
typedef void (*sampler_get_compare_fn)(const void *, uint32_t *, uint32_t *);
typedef void (*sampler_get_border_color_fn)(const void *, float *);
typedef void (*sampler_get_border_colori_fn)(const void *, int *);
typedef void (*sampler_get_border_colorui_fn)(const void *, uint32_t *);
typedef float (*sampler_get_max_anisotropy_fn)(const void *);
typedef uint32_t (*sampler_get_reduction_filter_fn)(const void *);
typedef int (*sampler_compare_fn)(const void *, const void *);
typedef uint64_t (*sampler_get_debug_id_fn)(const void *);

/* The builder family, which the sampler is made from. Its signatures are the
 * ones of the same header (lines 1363-1390). */
typedef void (*builder_set_defaults_fn)(void *);
typedef void (*builder_set_device_fn)(void *, void *);
typedef void (*builder_set_min_mag_fn)(void *, uint32_t, uint32_t);
typedef void (*builder_set_wrap_fn)(void *, uint32_t, uint32_t, uint32_t);
typedef void (*builder_set_lod_clamp_fn)(void *, float, float);
typedef void (*builder_set_lod_bias_fn)(void *, float);
typedef void (*builder_set_compare_fn)(void *, uint32_t, uint32_t);
typedef void (*builder_set_border_color_fn)(void *, const float *);
typedef void (*builder_set_border_colori_fn)(void *, const int *);
typedef void (*builder_set_border_colorui_fn)(void *, const uint32_t *);
typedef void (*builder_set_max_anisotropy_fn)(void *, float);
typedef void (*builder_set_reduction_fn)(void *, uint32_t);

/* The values the guest puts into a builder in this case. They are the
 * enumeration values of the vendored header, written out:
 *   min filter 0x2 = NEAREST_MIPMAP_NEAREST, mag filter 0x0 = NEAREST,
 *   wrap 0x6 = MIRRORED_REPEAT, 0x0 = CLAMP, 0x7 = CLAMP_TO_EDGE,
 *   compare mode 0x1 = COMPARE_R_TO_TEXTURE, compare function 0x7 = GEQUAL,
 *   reduction filter 0x2 = MAX. */
#define TEST_MIN_FILTER 0x2u
#define TEST_MAG_FILTER 0x0u
#define TEST_WRAP_S 0x6u
#define TEST_WRAP_T 0x0u
#define TEST_WRAP_R 0x7u
#define TEST_COMPARE_MODE 0x1u
#define TEST_COMPARE_FUNC 0x7u
#define TEST_REDUCTION 0x2u
#define TEST_LOD_MIN (-4.0f)
#define TEST_LOD_MAX 12.5f
#define TEST_LOD_BIAS 0.25f
#define TEST_ANISOTROPY 4.0f
/* The anisotropy of the third sampler, the one that differs. It takes part in
 * the comparison (hle_nvn.c:1935), so the two samplers must not be equal. */
#define TEST_ANISOTROPY_OTHER 8.0f

/* Fill a builder with the values of this case. */
static void fill_builder(builder_set_defaults_fn set_defaults, builder_set_device_fn set_device,
                         builder_set_min_mag_fn set_min_mag, builder_set_wrap_fn set_wrap,
                         builder_set_lod_clamp_fn set_lod_clamp, builder_set_lod_bias_fn set_lod_bias,
                         builder_set_compare_fn set_compare, builder_set_border_color_fn set_border_color,
                         builder_set_border_colori_fn set_border_colori,
                         builder_set_border_colorui_fn set_border_colorui,
                         builder_set_max_anisotropy_fn set_anisotropy, builder_set_reduction_fn set_reduction,
                         void *builder, float anisotropy) {
    static const float border[4] = { 0.25f, 0.5f, 0.75f, 1.0f };
    static const int borderi[4] = { 1, 2, 3, 4 };
    static const uint32_t borderui[4] = { 0x11223344u, 0x55667788u, 0x99aabbccu, 0xddeeff00u };
    set_defaults(builder);
    set_device(builder, (void *)(uintptr_t)0x5639a03810ULL);
    set_min_mag(builder, TEST_MIN_FILTER, TEST_MAG_FILTER);
    set_wrap(builder, TEST_WRAP_S, TEST_WRAP_T, TEST_WRAP_R);
    set_lod_clamp(builder, TEST_LOD_MIN, TEST_LOD_MAX);
    set_lod_bias(builder, TEST_LOD_BIAS);
    set_compare(builder, TEST_COMPARE_MODE, TEST_COMPARE_FUNC);
    set_border_color(builder, border);
    set_border_colori(builder, borderi);
    set_border_colorui(builder, borderui);
    set_anisotropy(builder, anisotropy);
    set_reduction(builder, TEST_REDUCTION);
}

static int nvn_sampler_contract(void) {
    bootstrap_fn bootstrap;
    lookup_fn lookup;
    sampler_initialize_fn sampler_initialize;
    sampler_finalize_fn sampler_finalize;
    sampler_set_debug_label_fn sampler_set_debug_label;
    sampler_get_min_mag_fn sampler_get_min_mag;
    sampler_get_wrap_fn sampler_get_wrap;
    sampler_get_lod_clamp_fn sampler_get_lod_clamp;
    sampler_get_lod_bias_fn sampler_get_lod_bias;
    sampler_get_compare_fn sampler_get_compare;
    sampler_get_border_color_fn sampler_get_border_color;
    sampler_get_border_colori_fn sampler_get_border_colori;
    sampler_get_border_colorui_fn sampler_get_border_colorui;
    sampler_get_max_anisotropy_fn sampler_get_max_anisotropy;
    sampler_get_reduction_filter_fn sampler_get_reduction_filter;
    sampler_compare_fn sampler_compare;
    sampler_get_debug_id_fn sampler_get_debug_id;
    builder_set_defaults_fn builder_set_defaults;
    builder_set_device_fn builder_set_device;
    builder_set_min_mag_fn builder_set_min_mag;
    builder_set_wrap_fn builder_set_wrap;
    builder_set_lod_clamp_fn builder_set_lod_clamp;
    builder_set_lod_bias_fn builder_set_lod_bias;
    builder_set_compare_fn builder_set_compare;
    builder_set_border_color_fn builder_set_border_color;
    builder_set_border_colori_fn builder_set_border_colori;
    builder_set_border_colorui_fn builder_set_border_colorui;
    builder_set_max_anisotropy_fn builder_set_anisotropy;
    builder_set_reduction_fn builder_set_reduction;
    nvn_sampler_stats stats;
    /* The guest's own objects. The layout of a builder and of a sampler is a
     * property of the Nintendo SDK, which this runtime does not have, so
     * nothing may be written into them. */
    unsigned char builder_one[64];
    unsigned char builder_two[64];
    unsigned char builder_three[64];
    unsigned char builder_four[64];
    unsigned char builder_unprepared[64];
    unsigned char sampler_one[64];
    unsigned char sampler_two[64];
    unsigned char sampler_three[64];
    unsigned char sampler_four[64];
    unsigned char sampler_unknown[64];
    uint32_t min_filter = 0;
    uint32_t mag_filter = 0;
    uint32_t wrap_s = 0;
    uint32_t wrap_t = 0;
    uint32_t wrap_r = 0;
    uint32_t compare_mode = 0;
    uint32_t compare_func = 0;
    uint32_t reduction = 0;
    float lod_min = 0.0f;
    float lod_max = 0.0f;
    float lod_bias = 0.0f;
    float anisotropy = 0.0f;
    float border[4];
    int borderi[4];
    uint32_t borderui[4];
    uint64_t debug_id_one = 0;
    uint64_t debug_id_two = 0;
    size_t count;
    size_t i;
    size_t j;

    memset(builder_one, 0xaa, sizeof(builder_one));
    memset(builder_two, 0xaa, sizeof(builder_two));
    memset(builder_three, 0xaa, sizeof(builder_three));
    memset(builder_four, 0xaa, sizeof(builder_four));
    memset(builder_unprepared, 0xaa, sizeof(builder_unprepared));
    memset(sampler_one, 0xaa, sizeof(sampler_one));
    memset(sampler_two, 0xaa, sizeof(sampler_two));
    memset(sampler_three, 0xaa, sizeof(sampler_three));
    memset(sampler_four, 0xaa, sizeof(sampler_four));
    memset(sampler_unknown, 0xaa, sizeof(sampler_unknown));

    /* 1. Every name of the family resolves, resolves to the same entry point
     *    every time, and the family table holds each of them once. */
    bootstrap = (bootstrap_fn)(uintptr_t)guest_process_resolve(0, "nvnBootstrapLoader");
    CHECK(bootstrap != NULL);
    lookup = (lookup_fn)(uintptr_t)bootstrap("nvnDeviceGetProcAddress");
    CHECK(lookup != NULL);
    count = nvn_sampler_family_count();
    expect_condition("nvn-sampler-family-has-fifteen-names", count == 15);
    for (i = 0; i < count; i++) {
        const nvn_sampler_family_entry *entry = nvn_sampler_family_entry_at(i);
        void *resolved;
        CHECK(entry != NULL);
        CHECK(entry->name != NULL);
        resolved = lookup(NULL, entry->name);
        expect_condition("nvn-sampler-name-resolves", resolved != NULL);
        expect_condition("nvn-sampler-name-is-stable", resolved == lookup(NULL, entry->name));
        expect_condition("nvn-sampler-name-matches-the-table", resolved == entry->fn);
        for (j = i + 1; j < count; j++) {
            const nvn_sampler_family_entry *other = nvn_sampler_family_entry_at(j);
            expect_condition("nvn-sampler-family-holds-each-name-once",
                             other == NULL || other->name == NULL || strcmp(entry->name, other->name) != 0);
        }
    }

    /* The family itself, through the guest's lookup. */
    sampler_initialize = (sampler_initialize_fn)(uintptr_t)lookup(NULL, "nvnSamplerInitialize");
    sampler_finalize = (sampler_finalize_fn)(uintptr_t)lookup(NULL, "nvnSamplerFinalize");
    sampler_set_debug_label = (sampler_set_debug_label_fn)(uintptr_t)lookup(NULL, "nvnSamplerSetDebugLabel");
    sampler_get_min_mag = (sampler_get_min_mag_fn)(uintptr_t)lookup(NULL, "nvnSamplerGetMinMagFilter");
    sampler_get_wrap = (sampler_get_wrap_fn)(uintptr_t)lookup(NULL, "nvnSamplerGetWrapMode");
    sampler_get_lod_clamp = (sampler_get_lod_clamp_fn)(uintptr_t)lookup(NULL, "nvnSamplerGetLodClamp");
    sampler_get_lod_bias = (sampler_get_lod_bias_fn)(uintptr_t)lookup(NULL, "nvnSamplerGetLodBias");
    sampler_get_compare = (sampler_get_compare_fn)(uintptr_t)lookup(NULL, "nvnSamplerGetCompare");
    sampler_get_border_color = (sampler_get_border_color_fn)(uintptr_t)lookup(NULL, "nvnSamplerGetBorderColor");
    sampler_get_border_colori = (sampler_get_border_colori_fn)(uintptr_t)lookup(NULL, "nvnSamplerGetBorderColori");
    sampler_get_border_colorui =
        (sampler_get_border_colorui_fn)(uintptr_t)lookup(NULL, "nvnSamplerGetBorderColorui");
    sampler_get_max_anisotropy =
        (sampler_get_max_anisotropy_fn)(uintptr_t)lookup(NULL, "nvnSamplerGetMaxAnisotropy");
    sampler_get_reduction_filter =
        (sampler_get_reduction_filter_fn)(uintptr_t)lookup(NULL, "nvnSamplerGetReductionFilter");
    sampler_compare = (sampler_compare_fn)(uintptr_t)lookup(NULL, "nvnSamplerCompare");
    sampler_get_debug_id = (sampler_get_debug_id_fn)(uintptr_t)lookup(NULL, "nvnSamplerGetDebugID");
    CHECK(sampler_initialize && sampler_finalize && sampler_set_debug_label);
    CHECK(sampler_get_min_mag && sampler_get_wrap && sampler_get_lod_clamp && sampler_get_lod_bias);
    CHECK(sampler_get_compare && sampler_get_border_color && sampler_get_border_colori);
    CHECK(sampler_get_border_colorui && sampler_get_max_anisotropy && sampler_get_reduction_filter);
    CHECK(sampler_compare && sampler_get_debug_id);

    /* The builder family, through the same lookup. */
    builder_set_defaults = (builder_set_defaults_fn)(uintptr_t)lookup(NULL, "nvnSamplerBuilderSetDefaults");
    builder_set_device = (builder_set_device_fn)(uintptr_t)lookup(NULL, "nvnSamplerBuilderSetDevice");
    builder_set_min_mag = (builder_set_min_mag_fn)(uintptr_t)lookup(NULL, "nvnSamplerBuilderSetMinMagFilter");
    builder_set_wrap = (builder_set_wrap_fn)(uintptr_t)lookup(NULL, "nvnSamplerBuilderSetWrapMode");
    builder_set_lod_clamp = (builder_set_lod_clamp_fn)(uintptr_t)lookup(NULL, "nvnSamplerBuilderSetLodClamp");
    builder_set_lod_bias = (builder_set_lod_bias_fn)(uintptr_t)lookup(NULL, "nvnSamplerBuilderSetLodBias");
    builder_set_compare = (builder_set_compare_fn)(uintptr_t)lookup(NULL, "nvnSamplerBuilderSetCompare");
    builder_set_border_color = (builder_set_border_color_fn)(uintptr_t)lookup(NULL, "nvnSamplerBuilderSetBorderColor");
    builder_set_border_colori =
        (builder_set_border_colori_fn)(uintptr_t)lookup(NULL, "nvnSamplerBuilderSetBorderColori");
    builder_set_border_colorui =
        (builder_set_border_colorui_fn)(uintptr_t)lookup(NULL, "nvnSamplerBuilderSetBorderColorui");
    builder_set_anisotropy =
        (builder_set_max_anisotropy_fn)(uintptr_t)lookup(NULL, "nvnSamplerBuilderSetMaxAnisotropy");
    builder_set_reduction =
        (builder_set_reduction_fn)(uintptr_t)lookup(NULL, "nvnSamplerBuilderSetReductionFilter");
    CHECK(builder_set_defaults && builder_set_device && builder_set_min_mag && builder_set_wrap);
    CHECK(builder_set_lod_clamp && builder_set_lod_bias && builder_set_compare && builder_set_border_color);
    CHECK(builder_set_border_colori && builder_set_border_colorui && builder_set_anisotropy);
    CHECK(builder_set_reduction);

    /* 2. A builder is filled, a sampler is made out of it, and every
     *    parameter is read back. */
    fill_builder(builder_set_defaults, builder_set_device, builder_set_min_mag, builder_set_wrap,
                 builder_set_lod_clamp, builder_set_lod_bias, builder_set_compare, builder_set_border_color,
                 builder_set_border_colori, builder_set_border_colorui, builder_set_anisotropy,
                 builder_set_reduction, builder_one, TEST_ANISOTROPY);
    expect_condition("nvn-sampler-initialize-succeeds", sampler_initialize(sampler_one, builder_one) == 1);
    sampler_set_debug_label(sampler_one, "contract-sampler-one");

    sampler_get_min_mag(sampler_one, &min_filter, &mag_filter);
    expect_value("nvn-sampler-min-filter-round-trips", (uint64_t)min_filter, (uint64_t)TEST_MIN_FILTER);
    expect_value("nvn-sampler-mag-filter-round-trips", (uint64_t)mag_filter, (uint64_t)TEST_MAG_FILTER);
    sampler_get_wrap(sampler_one, &wrap_s, &wrap_t, &wrap_r);
    expect_value("nvn-sampler-wrap-s-round-trips", (uint64_t)wrap_s, (uint64_t)TEST_WRAP_S);
    expect_value("nvn-sampler-wrap-t-round-trips", (uint64_t)wrap_t, (uint64_t)TEST_WRAP_T);
    expect_value("nvn-sampler-wrap-r-round-trips", (uint64_t)wrap_r, (uint64_t)TEST_WRAP_R);
    sampler_get_lod_clamp(sampler_one, &lod_min, &lod_max);
    expect_value("nvn-sampler-lod-min-round-trips", float_bits(lod_min), float_bits(TEST_LOD_MIN));
    expect_value("nvn-sampler-lod-max-round-trips", float_bits(lod_max), float_bits(TEST_LOD_MAX));
    lod_bias = sampler_get_lod_bias(sampler_one);
    expect_value("nvn-sampler-lod-bias-round-trips", float_bits(lod_bias), float_bits(TEST_LOD_BIAS));
    sampler_get_compare(sampler_one, &compare_mode, &compare_func);
    expect_value("nvn-sampler-compare-mode-round-trips", (uint64_t)compare_mode, (uint64_t)TEST_COMPARE_MODE);
    expect_value("nvn-sampler-compare-func-round-trips", (uint64_t)compare_func, (uint64_t)TEST_COMPARE_FUNC);
    memset(border, 0, sizeof(border));
    memset(borderi, 0, sizeof(borderi));
    memset(borderui, 0, sizeof(borderui));
    sampler_get_border_color(sampler_one, border);
    expect_value("nvn-sampler-border-color-r-round-trips", float_bits(border[0]), float_bits(0.25f));
    expect_value("nvn-sampler-border-color-g-round-trips", float_bits(border[1]), float_bits(0.5f));
    expect_value("nvn-sampler-border-color-b-round-trips", float_bits(border[2]), float_bits(0.75f));
    expect_value("nvn-sampler-border-color-a-round-trips", float_bits(border[3]), float_bits(1.0f));
    sampler_get_border_colori(sampler_one, borderi);
    expect_value("nvn-sampler-border-colori-0-round-trips", (uint64_t)(uint32_t)borderi[0], 1);
    expect_value("nvn-sampler-border-colori-1-round-trips", (uint64_t)(uint32_t)borderi[1], 2);
    expect_value("nvn-sampler-border-colori-2-round-trips", (uint64_t)(uint32_t)borderi[2], 3);
    expect_value("nvn-sampler-border-colori-3-round-trips", (uint64_t)(uint32_t)borderi[3], 4);
    sampler_get_border_colorui(sampler_one, borderui);
    expect_value("nvn-sampler-border-colorui-0-round-trips", (uint64_t)borderui[0], 0x11223344ULL);
    expect_value("nvn-sampler-border-colorui-1-round-trips", (uint64_t)borderui[1], 0x55667788ULL);
    expect_value("nvn-sampler-border-colorui-2-round-trips", (uint64_t)borderui[2], 0x99aabbccULL);
    expect_value("nvn-sampler-border-colorui-3-round-trips", (uint64_t)borderui[3], 0xddeeff00ULL);
    anisotropy = sampler_get_max_anisotropy(sampler_one);
    expect_value("nvn-sampler-max-anisotropy-round-trips", float_bits(anisotropy), float_bits(TEST_ANISOTROPY));
    reduction = sampler_get_reduction_filter(sampler_one);
    expect_value("nvn-sampler-reduction-filter-round-trips", (uint64_t)reduction, (uint64_t)TEST_REDUCTION);

    /* 3. The guest's own objects were not written to. The layout of a sampler
     *    is a property of the Nintendo SDK, so the runtime keeps its state on
     *    the host. */
    expect_condition("nvn-sampler-builder-not-written", all_bytes(builder_one, sizeof(builder_one), 0xaa));
    expect_condition("nvn-sampler-object-not-written", all_bytes(sampler_one, sizeof(sampler_one), 0xaa));

    /* 4. A sampler is a snapshot of the builder it was made from. The builder
     *    changes here, and the sampler must not. */
    builder_set_min_mag(builder_one, 0x5u, 0x1u);
    builder_set_wrap(builder_one, 0x0u, 0x7u, 0x6u);
    builder_set_lod_bias(builder_one, 9.0f);
    builder_set_anisotropy(builder_one, 16.0f);
    {
        static const float other_border[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        builder_set_border_color(builder_one, other_border);
    }
    sampler_get_min_mag(sampler_one, &min_filter, &mag_filter);
    expect_value("nvn-sampler-snapshot-keeps-the-min-filter", (uint64_t)min_filter, (uint64_t)TEST_MIN_FILTER);
    expect_value("nvn-sampler-snapshot-keeps-the-mag-filter", (uint64_t)mag_filter, (uint64_t)TEST_MAG_FILTER);
    sampler_get_wrap(sampler_one, &wrap_s, &wrap_t, &wrap_r);
    expect_value("nvn-sampler-snapshot-keeps-the-wrap-s", (uint64_t)wrap_s, (uint64_t)TEST_WRAP_S);
    expect_value("nvn-sampler-snapshot-keeps-the-wrap-t", (uint64_t)wrap_t, (uint64_t)TEST_WRAP_T);
    expect_value("nvn-sampler-snapshot-keeps-the-wrap-r", (uint64_t)wrap_r, (uint64_t)TEST_WRAP_R);
    lod_bias = sampler_get_lod_bias(sampler_one);
    expect_value("nvn-sampler-snapshot-keeps-the-lod-bias", float_bits(lod_bias), float_bits(TEST_LOD_BIAS));
    anisotropy = sampler_get_max_anisotropy(sampler_one);
    expect_value("nvn-sampler-snapshot-keeps-the-anisotropy", float_bits(anisotropy), float_bits(TEST_ANISOTROPY));
    memset(border, 0, sizeof(border));
    sampler_get_border_color(sampler_one, border);
    expect_value("nvn-sampler-snapshot-keeps-the-border-color", float_bits(border[0]), float_bits(0.25f));

    /* 5. Two samplers with the same parameters are the same, and two that
     *    differ in a parameter that takes part in the comparison are not. The
     *    parameter that differs here is the anisotropy, which the reference
     *    compares (hle_nvn.c:1935). */
    fill_builder(builder_set_defaults, builder_set_device, builder_set_min_mag, builder_set_wrap,
                 builder_set_lod_clamp, builder_set_lod_bias, builder_set_compare, builder_set_border_color,
                 builder_set_border_colori, builder_set_border_colorui, builder_set_anisotropy,
                 builder_set_reduction, builder_two, TEST_ANISOTROPY);
    expect_condition("nvn-sampler-initialize-succeeds-again", sampler_initialize(sampler_two, builder_two) == 1);
    expect_condition("nvn-sampler-compare-equal", sampler_compare(sampler_one, sampler_two) == 1);

    fill_builder(builder_set_defaults, builder_set_device, builder_set_min_mag, builder_set_wrap,
                 builder_set_lod_clamp, builder_set_lod_bias, builder_set_compare, builder_set_border_color,
                 builder_set_border_colori, builder_set_border_colorui, builder_set_anisotropy,
                 builder_set_reduction, builder_three, TEST_ANISOTROPY_OTHER);
    expect_condition("nvn-sampler-initialize-succeeds-a-third-time",
                     sampler_initialize(sampler_three, builder_three) == 1);
    expect_condition("nvn-sampler-compare-different", sampler_compare(sampler_one, sampler_three) == 0);

    /* The int form of the border colour is not part of the comparison: the
     * reference's sampler holds the colour as four floats and nothing else
     * (nvn_obj.h:49), so a sampler whose int border colour differs is still
     * the same sampler as far as the comparison is concerned. */
    {
        static const int other_borderi[4] = { 9, 9, 9, 9 };
        fill_builder(builder_set_defaults, builder_set_device, builder_set_min_mag, builder_set_wrap,
                     builder_set_lod_clamp, builder_set_lod_bias, builder_set_compare, builder_set_border_color,
                     builder_set_border_colori, builder_set_border_colorui, builder_set_anisotropy,
                     builder_set_reduction, builder_four, TEST_ANISOTROPY);
        builder_set_border_colori(builder_four, other_borderi);
        expect_condition("nvn-sampler-initialize-succeeds-a-fourth-time",
                         sampler_initialize(sampler_four, builder_four) == 1);
        expect_condition("nvn-sampler-compare-ignores-the-int-border-colour",
                         sampler_compare(sampler_one, sampler_four) == 1);
        memset(borderi, 0, sizeof(borderi));
        sampler_get_border_colori(sampler_four, borderi);
        expect_value("nvn-sampler-int-border-colour-is-its-own-form", (uint64_t)(uint32_t)borderi[0], 9);
    }

    /* 6. The refusals. A builder that was never prepared has no parameters,
     *    and a sampler that was never initialized does not exist. */
    expect_condition("nvn-sampler-initialize-refuses-an-unprepared-builder",
                     sampler_initialize(sampler_unknown, builder_unprepared) == 0);
    expect_condition("nvn-sampler-compare-refuses-an-unknown-sampler",
                     sampler_compare(sampler_one, sampler_unknown) == 0);
    expect_condition("nvn-sampler-debug-id-of-an-unknown-sampler-is-zero",
                     sampler_get_debug_id(sampler_unknown) == 0);

    /* 7. The debug id of a sampler that exists is not zero, and two samplers
     *    do not share one. */
    debug_id_one = sampler_get_debug_id(sampler_one);
    debug_id_two = sampler_get_debug_id(sampler_two);
    expect_condition("nvn-sampler-debug-id-is-not-zero", debug_id_one != 0);
    expect_condition("nvn-sampler-debug-ids-are-per-sampler", debug_id_one != debug_id_two);

    /* 8. Finalizing a sampler takes it out of the runtime's table. */
    sampler_finalize(sampler_three);
    expect_condition("nvn-sampler-compare-refuses-a-finalized-sampler",
                     sampler_compare(sampler_one, sampler_three) == 0);

    /* 9. The guest's objects are still untouched after all of that. */
    expect_condition("nvn-sampler-builder-not-written-afterwards",
                     all_bytes(builder_one, sizeof(builder_one), 0xaa) &&
                         all_bytes(builder_two, sizeof(builder_two), 0xaa) &&
                         all_bytes(builder_three, sizeof(builder_three), 0xaa) &&
                         all_bytes(builder_four, sizeof(builder_four), 0xaa));
    expect_condition("nvn-sampler-object-not-written-afterwards",
                     all_bytes(sampler_one, sizeof(sampler_one), 0xaa) &&
                         all_bytes(sampler_two, sizeof(sampler_two), 0xaa) &&
                         all_bytes(sampler_three, sizeof(sampler_three), 0xaa) &&
                         all_bytes(sampler_four, sizeof(sampler_four), 0xaa));

    /* 10. The counters. */
    nvn_sampler_get_stats(&stats);
    expect_condition("nvn-sampler-stats-count-calls", stats.calls > 0);
    expect_condition("nvn-sampler-stats-count-initialized", stats.initialized >= 4);
    expect_condition("nvn-sampler-stats-count-finalized", stats.finalized >= 1);
    expect_condition("nvn-sampler-stats-count-gets", stats.gets > 0);
    expect_condition("nvn-sampler-stats-count-compares", stats.compares >= 5);
    expect_condition("nvn-sampler-stats-count-refusals", stats.refused > 0);
    expect_condition("nvn-sampler-stats-have-live-samplers", stats.live_samplers >= 3);

    /* The final gate. Without it the contract could exit 0 while the
     * expectations had failed. */
    CHECK(!errors);
    printf("{\"nvn_sampler_errors\":%u,\"names\":%zu,\"initialized\":%llu,\"finalized\":%llu,"
           "\"compared\":%llu,\"gets\":%llu,\"refused\":%llu,\"live_samplers\":%d}\n",
           errors, count, (unsigned long long)stats.initialized, (unsigned long long)stats.finalized,
           (unsigned long long)stats.compares, (unsigned long long)stats.gets, (unsigned long long)stats.refused,
           stats.live_samplers);
    return 0;
}

int main(int argc,char **argv) {
    if(argc!=2 || strcmp(argv[1],"--nvn-sampler")) {
        fprintf(stderr,"usage: %s --nvn-sampler\n",argv[0]);
        return 2;
    }
    return nvn_sampler_contract();
}
