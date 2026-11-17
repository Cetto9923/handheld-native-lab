#include "../../src/graphics/nvn_device/nvn_device.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>

typedef uintptr_t VkImage;
typedef uint32_t VkFormat;
static int outcome;

static int present(nvn_device_state *device, void *image, uint32_t format,
                   uint32_t width, uint32_t height, const int32_t crop[4],
                   int32_t interval, char *reason, size_t size) {
    (void)device; (void)image; (void)format; (void)width; (void)height;
    (void)crop; (void)interval; (void)reason; (void)size;
    return outcome;
}

#include PRODUCTION_HEADER

static const nvn_backend backend = {.present_image = present};
static nvn_device_state device = {.backend = &backend};

static void *present_many(void *unused) {
    (void)unused;
    for (unsigned i = 0; i < 5000; ++i)
        assert(engine_present(&device, NULL, 1, 0, 1, 1, NULL, 0) == 0);
    return NULL;
}

int main(void) {
    assert(nvn_engine_presented_frames() == 0);
    outcome = NVN_PRESENT_SKIPPED;
    assert(engine_present(&device, NULL, 1, 0, 1, 1, NULL, 0) == 1);
    assert(nvn_engine_presented_frames() == 0);
    outcome = NVN_PRESENT_FAILED;
    assert(engine_present(&device, NULL, 1, 0, 1, 1, NULL, 0) == -1);
    assert(engine_present(NULL, NULL, 1, 0, 1, 1, NULL, 0) == -1);
    assert(nvn_engine_presented_frames() == 0);
    outcome = NVN_PRESENT_QUEUED;
    pthread_t threads[4];
    for (unsigned i = 0; i < 4; ++i)
        assert(pthread_create(&threads[i], NULL, present_many, NULL) == 0);
    for (unsigned i = 0; i < 4; ++i)
        assert(pthread_join(threads[i], NULL) == 0);
    assert(nvn_engine_presented_frames() == 20000);
    return 0;
}
