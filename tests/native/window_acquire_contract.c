/* Test the production window/sync bridge against a controlled GPU timeline.
 * This is an ABI/ordering contract, not a rendering or device acceptance test. */
#include "../../src/graphics/nvn_device/nvn_device.h"
#include "../../src/graphics/nvn_device/nvn_window.h"
#include "../../src/graphics/nvn_device/nvn_sync.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

void nvn_window_builder_set_defaults(void *);
void nvn_window_builder_set_device(void *, void *);
void nvn_window_builder_set_native_window(void *, void *);
void nvn_window_builder_set_textures(void *, int, void *);
int nvn_window_initialize(void *, const void *, int);
int nvn_window_acquire_texture(void *, void *, int *);
int nvnSyncWait(const void *, uint64_t);

static int gpu_ready = 1, allow_wait = 1;
static uint64_t submitted = 7, completed = 6;
int nvn_engine_ready(void) { return gpu_ready; }
uint64_t nvn_engine_last_fence(void) { return submitted; }
int nvn_engine_wait(uint64_t fence, uint64_t timeout) {
    if (fence <= completed) return 1;
    if (timeout && allow_wait) { completed = fence; return 1; }
    return 0;
}
static int surface(nvn_device_state *d, void *w, uint32_t *x, uint32_t *y, void **s, void **c) {
    (void)d; (void)w; *x = 1280; *y = 960; *s = (void *)3; *c = (void *)4; return 1;
}
static const nvn_backend backend = {.create_window_surface = surface};
const nvn_backend *nvn_device_backend(void) { return &backend; }
nvn_device_state *nvn_device_state_handle(void) { return (nvn_device_state *)1; }
int nvn_device_is_ready(const void *d) { return d == (void *)1; }

static int errors;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); errors++; } } while (0)
int main(void) {
    unsigned char builder[64], window[64], sync[64];
    void *textures[2] = {(void *)0x1110, (void *)0x2220};
    struct { uint32_t before; int index; uint32_t after; } out = {0xaabbccdd, -99, 0x11223344};
    memset(sync, 0xaa, sizeof(sync));
    nvn_window_builder_set_defaults(builder);
    nvn_window_builder_set_device(builder, (void *)1);
    nvn_window_builder_set_native_window(builder, (void *)2);
    nvn_window_builder_set_textures(builder, 2, textures);
    CHECK(nvn_window_initialize(window, builder, 0) == 1);
    CHECK(nvn_window_acquire_texture(window, sync, &out.index) == 0);
    CHECK(out.index == 0 && completed == submitted);
    CHECK(nvn_sync_family_fence_value(sync) == 7);
    CHECK(nvnSyncWait(sync, 0) == 0);
    CHECK(nvn_window_acquire_texture(window, sync, &out.index) == 0 && out.index == 1);
    CHECK(nvn_window_acquire_texture(window, sync, &out.index) == 0 && out.index == 0);
    CHECK(out.before == 0xaabbccdd && out.after == 0x11223344);
    for (unsigned i = 0; i < sizeof(sync); i++) CHECK(sync[i] == 0xaa);
    submitted = 8;
    nvn_sync_family_fence(sync);
    CHECK(nvnSyncWait(sync, 0) == 2); /* A fenced object is not necessarily completed. */
    CHECK(nvnSyncWait(sync, 1000) == 1);
    CHECK(nvnSyncWait(sync, 0) == 0);
    out.index = -99;
    CHECK(nvn_window_acquire_texture((void *)0xdead, sync, &out.index) == 1 && out.index == -99);
    CHECK(nvn_window_acquire_texture(window, sync, NULL) == 1);
    gpu_ready = 0;
    CHECK(nvn_window_acquire_texture(window, sync, &out.index) == 1 && out.index == -99);
    gpu_ready = 1; allow_wait = 0; submitted++;
    CHECK(nvn_window_acquire_texture(window, sync, &out.index) == 1 && out.index == -99);
    printf("window_acquire_contract errors=%d\n", errors);
    return errors ? 1 : 0;
}
