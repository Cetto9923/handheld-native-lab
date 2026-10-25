/*
 * nvn_present.c - the present path of the NVN device layer.
 *
 * This is the thin side of the present path. The heavy side is the backend:
 * the Vulkan backend owns the surface, the swapchain, the images, the views,
 * the framebuffers, the render pass, the command buffers and the fences. This
 * file only decides the order of the three steps and keeps the truth about
 * what happened.
 *
 * The rules of this file:
 *
 *   - The clear colour comes from the caller. There is no colour in this file.
 *     The four floats are handed to the backend unchanged, bit for bit, and
 *     the backend puts them into the clear value of the render pass. A colour
 *     that this file invented would be a colour the guest never asked for.
 *   - A failure is named. Every step that fails writes a concrete reason, and
 *     the reason carries the name of the step in front of it, so that a log
 *     says which of the three steps it was.
 *   - A frame that did not reach the screen is never counted as one that did.
 *     The presented counter only moves after the present step succeeded.
 */
#include "nvn_present.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

static nvn_present_stats s_stats;
static pthread_mutex_t s_stats_lock = PTHREAD_MUTEX_INITIALIZER;

/* Record a failure: count it, keep the reason and write it to the log. The
 * reason that is kept and the reason that is logged are the same text, so
 * that a log line and the recorded reason can never disagree. */
static void nvn_present_note_failure(const char *step, const char *detail) {
    char composed[NVN_PRESENT_REASON_SIZE];
    snprintf(composed, sizeof(composed), "%s: %s", step,
             detail ? detail : "the backend gave no reason for the refusal");
    pthread_mutex_lock(&s_stats_lock);
    s_stats.failures++;
    snprintf(s_stats.last_reason, sizeof(s_stats.last_reason), "%s", composed);
    pthread_mutex_unlock(&s_stats_lock);
    fprintf(stderr, "[nvn] present: %s\n", composed);
}

static void nvn_present_count_acquire_failure(void) {
    pthread_mutex_lock(&s_stats_lock);
    s_stats.acquire_failures++;
    pthread_mutex_unlock(&s_stats_lock);
}

static void nvn_present_count_render_pass_failure(void) {
    pthread_mutex_lock(&s_stats_lock);
    s_stats.render_pass_failures++;
    pthread_mutex_unlock(&s_stats_lock);
}

static void nvn_present_count_submit_failure(void) {
    pthread_mutex_lock(&s_stats_lock);
    s_stats.submit_failures++;
    pthread_mutex_unlock(&s_stats_lock);
}

int nvn_present_frame(nvn_device_state *device, const float rgba[4]) {
    const nvn_backend *backend;
    uint32_t index = 0;
    void *image = NULL;
    char reason[NVN_PRESENT_REASON_SIZE];

    pthread_mutex_lock(&s_stats_lock);
    s_stats.frames_requested++;
    pthread_mutex_unlock(&s_stats_lock);

    reason[0] = '\0';

    if (!device) {
        nvn_present_note_failure("present", "the device is NULL");
        return 0;
    }
    if (!rgba) {
        nvn_present_note_failure("present", "the clear colour is NULL");
        return 0;
    }

    backend = device->backend;
    if (!backend) {
        nvn_present_note_failure("present", "the device has no backend");
        return 0;
    }
    if (!backend->acquire_image || !backend->begin_render_pass || !backend->submit_and_present) {
        nvn_present_note_failure("present", "the backend does not implement the three steps of the present path");
        return 0;
    }

    /* Step one: take the next swapchain image. */
    if (!backend->acquire_image(device, &index, &image, reason, sizeof(reason))) {
        nvn_present_count_acquire_failure();
        nvn_present_note_failure("acquire", reason);
        return 0;
    }
    pthread_mutex_lock(&s_stats_lock);
    s_stats.frames_acquired++;
    pthread_mutex_unlock(&s_stats_lock);

    /* Step two: open the render pass and clear the image with the colour the
     * caller gave. The colour is only passed on here; it is not touched. */
    if (!backend->begin_render_pass(device, index, image, rgba, reason, sizeof(reason))) {
        nvn_present_count_render_pass_failure();
        nvn_present_note_failure("render pass", reason);
        return 0;
    }
    pthread_mutex_lock(&s_stats_lock);
    s_stats.frames_rendered++;
    pthread_mutex_unlock(&s_stats_lock);

    /* Step three: submit and present. */
    if (!backend->submit_and_present(device, index, image, reason, sizeof(reason))) {
        nvn_present_count_submit_failure();
        nvn_present_note_failure("submit and present", reason);
        return 0;
    }
    pthread_mutex_lock(&s_stats_lock);
    s_stats.frames_submitted++;
    s_stats.frames_presented++;
    pthread_mutex_unlock(&s_stats_lock);
    return 1;
}

void nvn_present_get_stats(nvn_present_stats *out) {
    if (!out) return;
    pthread_mutex_lock(&s_stats_lock);
    *out = s_stats;
    pthread_mutex_unlock(&s_stats_lock);
}

void nvn_present_reset_stats(void) {
    pthread_mutex_lock(&s_stats_lock);
    memset(&s_stats, 0, sizeof(s_stats));
    pthread_mutex_unlock(&s_stats_lock);
}
