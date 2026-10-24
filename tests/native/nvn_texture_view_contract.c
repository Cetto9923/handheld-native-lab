/* The contract of the NVN texture view family.
 *
 * A texture view says which part of a texture a draw refers to. This case
 * checks that the view really holds what the guest put into it, that the
 * guest's own view memory is not written, that the defaults clear the
 * description, that the getters say whether the guest set a field at all, and
 * that two views can be compared.
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
#include "../../src/graphics/nvn_device/nvn_texture_view.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do {if(!(x)) {fprintf(stderr,"texture view contract line=%d: %s\n",__LINE__,#x);return 1;}} while(0)

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

/* The fourteen entry points of the view family. The argument types are the
 * ones of the vendored NVN header (src/graphics/nvn/include/nvn/nvn.h:
 * 1300-1319); the enumerations are carried as plain words here, because the
 * values that matter are the numbers themselves. The getters answer with a
 * boolean, which the header calls an NVNboolean: one byte. */
typedef void (*view_set_defaults_fn)(void *);
typedef void (*view_set_levels_fn)(void *, int, int);
typedef void (*view_set_layers_fn)(void *, int, int);
typedef void (*view_set_format_fn)(void *, uint32_t);
typedef void (*view_set_target_fn)(void *, uint32_t);
typedef void (*view_set_swizzle_fn)(void *, uint32_t, uint32_t, uint32_t, uint32_t);
typedef void (*view_set_dsmode_fn)(void *, uint32_t);
typedef uint8_t (*view_get_levels_fn)(const void *, int *, int *);
typedef uint8_t (*view_get_layers_fn)(const void *, int *, int *);
typedef uint8_t (*view_get_format_fn)(const void *, uint32_t *);
typedef uint8_t (*view_get_target_fn)(const void *, uint32_t *);
typedef uint8_t (*view_get_swizzle_fn)(const void *, uint32_t *, uint32_t *, uint32_t *, uint32_t *);
typedef uint8_t (*view_get_dsmode_fn)(const void *, uint32_t *);
typedef uint8_t (*view_compare_fn)(const void *, const void *);

/* The values the guest puts into a view in this case. They are the
 * enumeration values of the vendored header, written out:
 *   format 0x38 = RGBA8_SRGB, target 0x8 = CUBEMAP,
 *   swizzle 0x0 = ZERO, 0x1 = ONE, 0x2 = R, 0x5 = A,
 *   depth stencil mode 0x1 = STENCIL. */
#define TEST_FORMAT 0x38u
#define TEST_TARGET 0x8u
#define TEST_BASE_LEVEL 3
#define TEST_LEVELS 2
#define TEST_FIRST_LAYER 5
#define TEST_LAYERS 4
#define TEST_SWIZZLE_R 0x0u
#define TEST_SWIZZLE_G 0x1u
#define TEST_SWIZZLE_B 0x2u
#define TEST_SWIZZLE_A 0x5u
#define TEST_DSMODE 0x1u

/* The identity swizzle of a view that was never given one: R, G, B, A
 * (hle_nvn.c:2034). */
#define IDENTITY_SWIZZLE_R 0x2u
#define IDENTITY_SWIZZLE_G 0x3u
#define IDENTITY_SWIZZLE_B 0x4u
#define IDENTITY_SWIZZLE_A 0x5u

static int nvn_texture_view_contract(void) {
    bootstrap_fn bootstrap;
    lookup_fn lookup;
    view_set_defaults_fn view_set_defaults;
    view_set_levels_fn view_set_levels;
    view_set_layers_fn view_set_layers;
    view_set_format_fn view_set_format;
    view_set_target_fn view_set_target;
    view_set_swizzle_fn view_set_swizzle;
    view_set_dsmode_fn view_set_dsmode;
    view_get_levels_fn view_get_levels;
    view_get_layers_fn view_get_layers;
    view_get_format_fn view_get_format;
    view_get_target_fn view_get_target;
    view_get_swizzle_fn view_get_swizzle;
    view_get_dsmode_fn view_get_dsmode;
    view_compare_fn view_compare;
    nvn_texture_view_stats stats;
    /* The guest's own objects. The layout of a view is a property of the
     * Nintendo SDK, which this runtime does not have, so nothing may be
     * written into them. */
    unsigned char view_one[64];
    unsigned char view_two[64];
    unsigned char view_three[64];
    unsigned char view_unknown[64];
    int base_level = 0;
    int levels = 0;
    int first_layer = 0;
    int layers = 0;
    uint32_t format = 0;
    uint32_t target = 0;
    uint32_t swizzle_r = 0;
    uint32_t swizzle_g = 0;
    uint32_t swizzle_b = 0;
    uint32_t swizzle_a = 0;
    uint32_t dsmode = 0;
    size_t count;
    size_t i;
    size_t j;

    memset(view_one, 0xaa, sizeof(view_one));
    memset(view_two, 0xaa, sizeof(view_two));
    memset(view_three, 0xaa, sizeof(view_three));
    memset(view_unknown, 0xaa, sizeof(view_unknown));

    /* 1. Every name of the family resolves, resolves to the same entry point
     *    every time, and the family table holds each of them once. */
    bootstrap = (bootstrap_fn)(uintptr_t)guest_process_resolve(0, "nvnBootstrapLoader");
    CHECK(bootstrap != NULL);
    lookup = (lookup_fn)(uintptr_t)bootstrap("nvnDeviceGetProcAddress");
    CHECK(lookup != NULL);
    count = nvn_texture_view_family_count();
    expect_condition("nvn-texture-view-family-has-fourteen-names", count == 14);
    for (i = 0; i < count; i++) {
        const nvn_texture_view_family_entry *entry = nvn_texture_view_family_entry_at(i);
        void *resolved;
        CHECK(entry != NULL);
        CHECK(entry->name != NULL);
        resolved = lookup(NULL, entry->name);
        expect_condition("nvn-texture-view-name-resolves", resolved != NULL);
        expect_condition("nvn-texture-view-name-is-stable", resolved == lookup(NULL, entry->name));
        expect_condition("nvn-texture-view-name-matches-the-table", resolved == entry->fn);
        for (j = i + 1; j < count; j++) {
            const nvn_texture_view_family_entry *other = nvn_texture_view_family_entry_at(j);
            expect_condition("nvn-texture-view-family-holds-each-name-once",
                             other == NULL || other->name == NULL || strcmp(entry->name, other->name) != 0);
        }
    }

    /* The family itself, through the guest's lookup. */
    view_set_defaults = (view_set_defaults_fn)(uintptr_t)lookup(NULL, "nvnTextureViewSetDefaults");
    view_set_levels = (view_set_levels_fn)(uintptr_t)lookup(NULL, "nvnTextureViewSetLevels");
    view_set_layers = (view_set_layers_fn)(uintptr_t)lookup(NULL, "nvnTextureViewSetLayers");
    view_set_format = (view_set_format_fn)(uintptr_t)lookup(NULL, "nvnTextureViewSetFormat");
    view_set_target = (view_set_target_fn)(uintptr_t)lookup(NULL, "nvnTextureViewSetTarget");
    view_set_swizzle = (view_set_swizzle_fn)(uintptr_t)lookup(NULL, "nvnTextureViewSetSwizzle");
    view_set_dsmode = (view_set_dsmode_fn)(uintptr_t)lookup(NULL, "nvnTextureViewSetDepthStencilMode");
    view_get_levels = (view_get_levels_fn)(uintptr_t)lookup(NULL, "nvnTextureViewGetLevels");
    view_get_layers = (view_get_layers_fn)(uintptr_t)lookup(NULL, "nvnTextureViewGetLayers");
    view_get_format = (view_get_format_fn)(uintptr_t)lookup(NULL, "nvnTextureViewGetFormat");
    view_get_target = (view_get_target_fn)(uintptr_t)lookup(NULL, "nvnTextureViewGetTarget");
    view_get_swizzle = (view_get_swizzle_fn)(uintptr_t)lookup(NULL, "nvnTextureViewGetSwizzle");
    view_get_dsmode = (view_get_dsmode_fn)(uintptr_t)lookup(NULL, "nvnTextureViewGetDepthStencilMode");
    view_compare = (view_compare_fn)(uintptr_t)lookup(NULL, "nvnTextureViewCompare");
    CHECK(view_set_defaults && view_set_levels && view_set_layers && view_set_format);
    CHECK(view_set_target && view_set_swizzle && view_set_dsmode);
    CHECK(view_get_levels && view_get_layers && view_get_format && view_get_target);
    CHECK(view_get_swizzle && view_get_dsmode && view_compare);

    /* 2. A view is described, and every field is read back. The getters also
     *    say whether the guest set the field at all, which is what lets the
     *    guest tell a view that describes one level from a view that
     *    describes all of them (hle_nvn.c:1965, 2032, 2033, 2034). */
    view_set_defaults(view_one);
    view_set_levels(view_one, TEST_BASE_LEVEL, TEST_LEVELS);
    view_set_layers(view_one, TEST_FIRST_LAYER, TEST_LAYERS);
    view_set_format(view_one, TEST_FORMAT);
    view_set_target(view_one, TEST_TARGET);
    view_set_swizzle(view_one, TEST_SWIZZLE_R, TEST_SWIZZLE_G, TEST_SWIZZLE_B, TEST_SWIZZLE_A);
    view_set_dsmode(view_one, TEST_DSMODE);

    expect_condition("nvn-texture-view-get-levels-says-it-was-set",
                     view_get_levels(view_one, &base_level, &levels) == 1);
    expect_value("nvn-texture-view-base-level-round-trips", (uint64_t)(uint32_t)base_level,
                 (uint64_t)TEST_BASE_LEVEL);
    expect_value("nvn-texture-view-levels-round-trip", (uint64_t)(uint32_t)levels, (uint64_t)TEST_LEVELS);
    expect_condition("nvn-texture-view-get-layers-says-it-was-set",
                     view_get_layers(view_one, &first_layer, &layers) == 1);
    expect_value("nvn-texture-view-first-layer-round-trips", (uint64_t)(uint32_t)first_layer,
                 (uint64_t)TEST_FIRST_LAYER);
    expect_value("nvn-texture-view-layers-round-trip", (uint64_t)(uint32_t)layers, (uint64_t)TEST_LAYERS);
    expect_condition("nvn-texture-view-get-format-says-it-was-set", view_get_format(view_one, &format) == 1);
    expect_value("nvn-texture-view-format-round-trips", (uint64_t)format, (uint64_t)TEST_FORMAT);
    expect_condition("nvn-texture-view-get-target-says-it-was-set", view_get_target(view_one, &target) == 1);
    expect_value("nvn-texture-view-target-round-trips", (uint64_t)target, (uint64_t)TEST_TARGET);
    expect_condition("nvn-texture-view-get-swizzle-says-it-was-set",
                     view_get_swizzle(view_one, &swizzle_r, &swizzle_g, &swizzle_b, &swizzle_a) == 1);
    expect_value("nvn-texture-view-swizzle-r-round-trips", (uint64_t)swizzle_r, (uint64_t)TEST_SWIZZLE_R);
    expect_value("nvn-texture-view-swizzle-g-round-trips", (uint64_t)swizzle_g, (uint64_t)TEST_SWIZZLE_G);
    expect_value("nvn-texture-view-swizzle-b-round-trips", (uint64_t)swizzle_b, (uint64_t)TEST_SWIZZLE_B);
    expect_value("nvn-texture-view-swizzle-a-round-trips", (uint64_t)swizzle_a, (uint64_t)TEST_SWIZZLE_A);
    expect_condition("nvn-texture-view-get-depth-stencil-mode-says-it-was-set",
                     view_get_dsmode(view_one, &dsmode) == 1);
    expect_value("nvn-texture-view-depth-stencil-mode-round-trips", (uint64_t)dsmode, (uint64_t)TEST_DSMODE);

    /* 3. The guest's own view was not written to. */
    expect_condition("nvn-texture-view-object-not-written", all_bytes(view_one, sizeof(view_one), 0xaa));

    /* 4. A view that was never described has no field set, and the getters
     *    say so. The swizzle of such a view is the identity R, G, B, A
     *    (hle_nvn.c:2034). */
    expect_condition("nvn-texture-view-get-levels-of-an-unknown-view-says-it-was-not-set",
                     view_get_levels(view_unknown, &base_level, &levels) == 0);
    expect_condition("nvn-texture-view-get-layers-of-an-unknown-view-says-it-was-not-set",
                     view_get_layers(view_unknown, &first_layer, &layers) == 0);
    expect_condition("nvn-texture-view-get-format-of-an-unknown-view-says-it-was-not-set",
                     view_get_format(view_unknown, &format) == 0);
    expect_condition("nvn-texture-view-get-target-of-an-unknown-view-says-it-was-not-set",
                     view_get_target(view_unknown, &target) == 0);
    expect_condition("nvn-texture-view-get-depth-stencil-mode-of-an-unknown-view-says-it-was-not-set",
                     view_get_dsmode(view_unknown, &dsmode) == 0);
    view_get_swizzle(view_unknown, &swizzle_r, &swizzle_g, &swizzle_b, &swizzle_a);
    expect_value("nvn-texture-view-swizzle-of-an-unknown-view-is-the-identity-r", (uint64_t)swizzle_r,
                 (uint64_t)IDENTITY_SWIZZLE_R);
    expect_value("nvn-texture-view-swizzle-of-an-unknown-view-is-the-identity-g", (uint64_t)swizzle_g,
                 (uint64_t)IDENTITY_SWIZZLE_G);
    expect_value("nvn-texture-view-swizzle-of-an-unknown-view-is-the-identity-b", (uint64_t)swizzle_b,
                 (uint64_t)IDENTITY_SWIZZLE_B);
    expect_value("nvn-texture-view-swizzle-of-an-unknown-view-is-the-identity-a", (uint64_t)swizzle_a,
                 (uint64_t)IDENTITY_SWIZZLE_A);
    expect_condition("nvn-texture-view-get-swizzle-of-an-unknown-view-says-it-was-not-set",
                     view_get_swizzle(view_unknown, &swizzle_r, &swizzle_g, &swizzle_b, &swizzle_a) == 0);

    /* 5. The defaults clear the description of a view that had one
     *    (hle_nvn.c:499). */
    view_set_defaults(view_one);
    expect_condition("nvn-texture-view-defaults-clear-the-levels",
                     view_get_levels(view_one, &base_level, &levels) == 0);
    expect_condition("nvn-texture-view-defaults-clear-the-layers",
                     view_get_layers(view_one, &first_layer, &layers) == 0);
    expect_condition("nvn-texture-view-defaults-clear-the-format", view_get_format(view_one, &format) == 0);
    expect_condition("nvn-texture-view-defaults-clear-the-target", view_get_target(view_one, &target) == 0);
    expect_condition("nvn-texture-view-defaults-clear-the-swizzle",
                     view_get_swizzle(view_one, &swizzle_r, &swizzle_g, &swizzle_b, &swizzle_a) == 0);
    expect_condition("nvn-texture-view-defaults-clear-the-depth-stencil-mode",
                     view_get_dsmode(view_one, &dsmode) == 0);

    /* 6. Two views with the same description are the same, and two that
     *    differ are not. The reference host does not have this entry point,
     *    so the meaning is this runtime's own: the same fields set with the
     *    same values. */
    view_set_defaults(view_one);
    view_set_levels(view_one, TEST_BASE_LEVEL, TEST_LEVELS);
    view_set_layers(view_one, TEST_FIRST_LAYER, TEST_LAYERS);
    view_set_format(view_one, TEST_FORMAT);
    view_set_target(view_one, TEST_TARGET);
    view_set_swizzle(view_one, TEST_SWIZZLE_R, TEST_SWIZZLE_G, TEST_SWIZZLE_B, TEST_SWIZZLE_A);
    view_set_dsmode(view_one, TEST_DSMODE);
    view_set_defaults(view_two);
    view_set_levels(view_two, TEST_BASE_LEVEL, TEST_LEVELS);
    view_set_layers(view_two, TEST_FIRST_LAYER, TEST_LAYERS);
    view_set_format(view_two, TEST_FORMAT);
    view_set_target(view_two, TEST_TARGET);
    view_set_swizzle(view_two, TEST_SWIZZLE_R, TEST_SWIZZLE_G, TEST_SWIZZLE_B, TEST_SWIZZLE_A);
    view_set_dsmode(view_two, TEST_DSMODE);
    expect_condition("nvn-texture-view-compare-equal", view_compare(view_one, view_two) == 1);

    view_set_defaults(view_three);
    view_set_levels(view_three, TEST_BASE_LEVEL, TEST_LEVELS);
    view_set_layers(view_three, TEST_FIRST_LAYER, TEST_LAYERS);
    view_set_format(view_three, TEST_FORMAT);
    view_set_target(view_three, TEST_TARGET);
    view_set_swizzle(view_three, TEST_SWIZZLE_R, TEST_SWIZZLE_G, TEST_SWIZZLE_B, TEST_SWIZZLE_A);
    view_set_dsmode(view_three, TEST_DSMODE);
    view_set_levels(view_three, TEST_BASE_LEVEL + 1, TEST_LEVELS);
    expect_condition("nvn-texture-view-compare-different", view_compare(view_one, view_three) == 0);

    /* A view that was never described and a view that was described are not
     * the same view. */
    expect_condition("nvn-texture-view-compare-an-unknown-view", view_compare(view_one, view_unknown) == 0);

    /* 7. The guest's objects are still untouched after all of that. */
    expect_condition("nvn-texture-view-object-not-written-afterwards",
                     all_bytes(view_one, sizeof(view_one), 0xaa) &&
                         all_bytes(view_two, sizeof(view_two), 0xaa) &&
                         all_bytes(view_three, sizeof(view_three), 0xaa));

    /* 8. The counters. */
    nvn_texture_view_get_stats(&stats);
    expect_condition("nvn-texture-view-stats-count-calls", stats.calls > 0);
    expect_condition("nvn-texture-view-stats-count-set-defaults", stats.set_defaults >= 4);
    expect_condition("nvn-texture-view-stats-count-sets", stats.sets > 0);
    expect_condition("nvn-texture-view-stats-count-gets", stats.gets > 0);
    expect_condition("nvn-texture-view-stats-count-compares", stats.compares >= 3);
    expect_condition("nvn-texture-view-stats-have-live-views", stats.live_views >= 3);

    /* The final gate. Without it the contract could exit 0 while the
     * expectations had failed. */
    CHECK(!errors);
    printf("{\"nvn_texture_view_errors\":%u,\"names\":%zu,\"set_defaults\":%llu,\"sets\":%llu,\"gets\":%llu,"
           "\"compared\":%llu,\"live_views\":%d}\n",
           errors, count, (unsigned long long)stats.set_defaults, (unsigned long long)stats.sets,
           (unsigned long long)stats.gets, (unsigned long long)stats.compares, stats.live_views);
    return 0;
}

int main(int argc,char **argv) {
    if(argc!=2 || strcmp(argv[1],"--nvn-texture-view")) {
        fprintf(stderr,"usage: %s --nvn-texture-view\n",argv[0]);
        return 2;
    }
    return nvn_texture_view_contract();
}
