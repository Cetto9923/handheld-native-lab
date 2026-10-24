/* The NVN device backend for a build without a Vulkan implementation.
 *
 * This is the host side of the runtime. There is no Vulkan implementation in
 * this build, so there is no NVN device to create, and nvnDeviceInitialize
 * reports failure. That is the truth of this build and it is the same
 * behaviour the game saw before the device layer existed: the guest turns the
 * failure into its own fatal error, with its own message.
 *
 * The device layer itself is not limited to this: the dispatch, the builder
 * and the timestamp are the same code in both builds, and the Android build
 * swaps this backend for the Vulkan one.
 */
#include "nvn_device.h"

#include <stdio.h>

static int nvn_backend_none_create(nvn_device_state *device, const void *builder) {
    (void)builder;
    fprintf(stderr, "[nvn] this runtime build has no Vulkan implementation, so there is no NVN device\n");
    snprintf(device->description, sizeof(device->description),
             "no Vulkan implementation in this runtime build");
    return 0;
}

static void nvn_backend_none_destroy(nvn_device_state *device) { (void)device; }

static void nvn_backend_none_describe(const nvn_device_state *device, char *out, size_t out_size) {
    (void)device;
    snprintf(out, out_size, "no Vulkan implementation in this runtime build");
}

static int nvn_backend_none_get_queue(nvn_device_state *device, void **out_queue) {
    (void)device;
    (void)out_queue;
    /* There is no device in this build, so there is no queue either. */
    return 0;
}

static int nvn_backend_none_create_window_surface(nvn_device_state *device, void *native_window,
                                                   uint32_t *out_width, uint32_t *out_height,
                                                   void **out_surface, void **out_swapchain) {
    (void)device;
    (void)native_window;
    if (out_width) *out_width = 0;
    if (out_height) *out_height = 0;
    if (out_surface) *out_surface = NULL;
    if (out_swapchain) *out_swapchain = NULL;
    /* There is no platform window in this build. */
    return 0;
}

static void nvn_backend_none_destroy_window_surface(nvn_device_state *device, void *surface, void *swapchain) {
    (void)device;
    (void)surface;
    (void)swapchain;
}

static const nvn_backend s_none_backend = {
    "none",
    nvn_backend_none_create,
    nvn_backend_none_destroy,
    nvn_backend_none_describe,
    nvn_backend_none_get_queue,
    nvn_backend_none_create_window_surface,
    nvn_backend_none_destroy_window_surface,
};

const nvn_backend *nvn_device_default_backend(void) { return &s_none_backend; }
