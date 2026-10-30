/*
 * nvn_present.h - the present path of the NVN device abstraction.
 *
 * The device abstraction owns the whole present path: surface, swapchain,
 * swapchain images, image views, framebuffers, render pass, command pool and
 * one command buffer plus one fence per swapchain image.
 *
 * The public entry point is nvn_present_frame().  It acquires a swapchain
 * image, opens a render pass, clears the image with the colour the caller
 * supplied, submits the command buffer and presents.
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

/* How long a reason may be. */
#define NVN_PRESENT_REASON_SIZE 256

/* Diagnostics for the present path.  All counters are monotonic and start at
 * zero.  They are filled in by nvn_present_get_stats(). */
typedef struct nvn_present_stats {
    unsigned long frames_requested;    /* nvn_present_frame() calls                  */
    unsigned long frames_acquired;     /* the acquire step succeeded                */
    unsigned long frames_rendered;     /* the render pass was opened                */
    unsigned long frames_submitted;    /* the submit and present step succeeded     */
    unsigned long frames_presented;    /* the frame reached the screen              */
    unsigned long failures;            /* frames that did not reach the screen      */
    unsigned long acquire_failures;    /* the acquire step failed                   */
    unsigned long render_pass_failures;/* the render pass step failed                */
    unsigned long submit_failures;     /* the submit and present step failed        */
    /* The reason of the last failure, with the name of the step in front of
     * it, so that a failure is named and not just counted. */
    char last_reason[NVN_PRESENT_REASON_SIZE];
} nvn_present_stats;

/*
 * Present one frame.
 *
 * rgba is a four component clear colour.  The components are used bit for
 * bit: the value is not clamped, not rounded and not altered.  A NULL rgba is
 * rejected and the failure is named.
 *
 * Returns 1 when the frame was acquired, rendered, submitted and presented.
 * Returns 0 when the frame could not be completed.  In that case nothing was
 * presented, the step that failed is named in the reason, and the reason is
 * written to the log.  A failing call never reports a presented frame.
 */
int nvn_present_frame(nvn_device_state *dev, const float rgba[4]);

/* Fill stats with the counters of the present path.  Safe to call without a
 * device. */
void nvn_present_get_stats(nvn_present_stats *out);

/* Put the counters back to zero.  Only the contract and the diagnostics use
 * this. */
void nvn_present_reset_stats(void);

#ifdef __cplusplus
}
#endif

#endif /* NVN_PRESENT_H */
