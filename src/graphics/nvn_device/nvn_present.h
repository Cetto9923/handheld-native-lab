/*
 * nvn_present.h - the present path of the NVN device abstraction.
 *
 * The device abstraction owns the whole present path: surface, swapchain,
 * swapchain images, image views, framebuffers, render pass, command pool and
 * one command buffer plus one fence per swapchain image.
 *
 * The single public entry point is nvn_present_frame().  It acquires a
 * swapchain image, opens a render pass, clears the image with the colour the
 * caller supplied, submits the command buffer and presents.
 *
 * There is exactly one present path per device.  Multiple windows are not
 * supported.
 */

#ifndef NVN_PRESENT_H
#define NVN_PRESENT_H

#include <stddef.h>

#include "nvn_device.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Diagnostics for the present path.  All counters are monotonic and start at
 * zero.  They are filled in by nvn_present_get_stats(). */
typedef struct nvn_present_stats {
    unsigned long frames_requested;   /* nvn_present_frame() calls                 */
    unsigned long frames_submitted;   /* command buffers handed to vkQueueSubmit  */
    unsigned long frames_presented;   /* vkQueuePresentKHR calls that succeeded   */
    unsigned long acquire_failures;   /* vkAcquireNextImageKHR failures           */
    unsigned long present_failures;   /* vkQueuePresentKHR failures               */
    unsigned long submit_failures;    /* vkQueueSubmit failures                   */
    unsigned long fence_timeouts;     /* vkWaitForFences hits the timeout         */
    unsigned long render_passes_built; /* render passes created                    */
    unsigned long framebuffers_built;  /* framebuffers created                     */
    unsigned long render_passes_reused;/* render pass lookups served from cache   */
    unsigned long framebuffers_reused; /* framebuffer lookups served from cache   */
    unsigned long images_acquired;     /* successful image acquisitions            */
    double last_fence_wait_ms;        /* duration of the last fence wait          */
    double max_fence_wait_ms;         /* worst fence wait seen so far             */
    double total_fence_wait_ms;        /* sum of all fence waits                  */
} nvn_present_stats;

/*
 * Present one frame.
 *
 * rgba is a four component clear colour in the 0.0f .. 1.0f range.  The
 * components are used bit for bit: the value is not clamped, not rounded and
 * not altered.  A NULL rgba is rejected and the failure is named in reason.
 *
 * Returns 0 when the frame was acquired, rendered, submitted and presented.
 * Returns non-zero when the frame could not be completed.  In that case
 * nothing was presented and reason holds a human readable name of the
 * failure.  A failing call never reports a presented frame.
 */
int nvn_present_frame(nvn_device_state *dev, const float rgba[4]);

/* Fill stats with the present path counters of dev.  Returns 0 on success,
 * non-zero when dev is NULL. */
int nvn_present_get_stats(nvn_device_state *dev, nvn_present_stats *stats);

#ifdef __cplusplus
}
#endif

#endif /* NVN_PRESENT_H */
