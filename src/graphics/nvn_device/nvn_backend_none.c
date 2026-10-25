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

/* The three steps of the present path. This build has no Vulkan
 * implementation, so there is no swapchain, no image and no queue to present
 * with. Every one of the three refuses, and every one of them says why in
 * the reason it is given. A backend that returned 1 here would report a
 * frame that was never shown, which is the one thing this layer must never
 * do. */
static int nvn_backend_none_acquire_image(nvn_device_state *device, uint32_t *out_index, void **out_image,
                                          char *reason, size_t reason_size) {
    (void)device;
    if (out_index) *out_index = 0;
    if (out_image) *out_image = NULL;
    if (reason && reason_size) {
        snprintf(reason, reason_size,
                 "this runtime build has no Vulkan implementation, so there is no swapchain image to take");
    }
    fprintf(stderr, "[nvn] none backend: there is no swapchain image to take in this build\n");
    return 0;
}

static int nvn_backend_none_begin_render_pass(nvn_device_state *device, uint32_t index, void *image,
                                               const float rgba[4], char *reason, size_t reason_size) {
    (void)device;
    (void)index;
    (void)image;
    (void)rgba;
    if (reason && reason_size) {
        snprintf(reason, reason_size,
                 "this runtime build has no Vulkan implementation, so there is no render pass to open");
    }
    fprintf(stderr, "[nvn] none backend: there is no render pass to open in this build\n");
    return 0;
}

static int nvn_backend_none_submit_and_present(nvn_device_state *device, uint32_t index, void *image,
                                                char *reason, size_t reason_size) {
    (void)device;
    (void)index;
    (void)image;
    if (reason && reason_size) {
        snprintf(reason, reason_size,
                 "this runtime build has no Vulkan implementation, so there is nothing to submit or to show");
    }
    fprintf(stderr, "[nvn] none backend: there is nothing to submit or to show in this build\n");
    return 0;
}

static const nvn_backend s_none_backend = {
    "none",
    nvn_backend_none_create,
    nvn_backend_none_destroy,
    nvn_backend_none_describe,
    nvn_backend_none_get_queue,
    nvn_backend_none_create_window_surface,
    nvn_backend_none_destroy_window_surface,
    nvn_backend_none_acquire_image,
    nvn_backend_none_begin_render_pass,
    nvn_backend_none_submit_and_present,
};

const nvn_backend *nvn_device_default_backend(void) { return &s_none_backend; }
