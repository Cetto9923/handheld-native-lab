/* The contract of the present path.
 *
 * The present path is the last step before the guest's picture reaches the
 * screen: take a swapchain image, open a render pass, clear the image with
 * the colour the guest asked for, submit and present.
 *
 * This host has no Vulkan implementation, so the contract is driven against
 * the backend of a host build, the one that has no swapchain and says so.
 * That is the honest way to test the path here: a backend that pretended to
 * have a swapchain would let a contract pass on a frame that was never
 * shown.
 *
 * The contract proves four things, each on its own:
 *
 *   1. the three steps of the path exist and refuse, with a reason;
 *   2. the arguments are checked, so a NULL is refused and not a crash;
 *   3. a failure is named: the reason says which step failed, and the log
 *      says the same thing;
 *   4. a failure never leaves the impression that a frame was shown.
 *
 * On top of that it proves that the clear colour is the colour of the
 * caller: a fake backend records the colour it was given and refuses to
 * present, and the contract checks the four floats bit for bit and checks
 * that the frame was still not reported as shown.
 *
 * The fake backend only records. It never presents, so the contract cannot
 * pass on a frame that was not shown.
 *
 * The case is in a translation unit of its own, with its own main, so that
 * two families can be worked on at the same time without writing into the
 * same file. The build script finds it by its name and runs it as a separate
 * contract binary, and the switch comes from the name: this file answers to
 * --nvn-present.
 */
#define _GNU_SOURCE
/* The present path of the device layer is not part of the host build of the
 * runtime, so the contract brings its own copy of the module. The module is
 * the one in the tree, not a copy of it: the file is included, not
 * rewritten. */
#include "../../src/graphics/nvn_device/nvn_present.c"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(x) do {if(!(x)) {fprintf(stderr,"present contract line=%d: %s\n",__LINE__,#x);return 1;}} while(0)

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

/* The stderr of the runtime is the second place where a failure has to be
 * named. The contract captures it around the call it makes, so that it can
 * compare what was logged with what was recorded. */
static FILE *s_capture_file;
static int s_capture_saved_fd = -1;
static char s_capture_text[8192];

static void capture_start(void) {
    fflush(stderr);
    s_capture_file = tmpfile();
    if(!s_capture_file) return;
    s_capture_saved_fd = dup(fileno(stderr));
    if(s_capture_saved_fd<0) { fclose(s_capture_file); s_capture_file = NULL; return; }
    dup2(fileno(s_capture_file), fileno(stderr));
}

static void capture_stop(void) {
    size_t length;
    fflush(stderr);
    if(s_capture_saved_fd>=0) {
        dup2(s_capture_saved_fd, fileno(stderr));
        close(s_capture_saved_fd);
        s_capture_saved_fd = -1;
    }
    s_capture_text[0] = '\0';
    if(!s_capture_file) return;
    fflush(s_capture_file);
    rewind(s_capture_file);
    length = fread(s_capture_text, 1, sizeof(s_capture_text) - 1, s_capture_file);
    s_capture_text[length] = '\0';
    fclose(s_capture_file);
    s_capture_file = NULL;
}

/* The fake backend.
 *
 * It records what it was given and it refuses to present. The refusal is
 * the point: a backend that returned 1 here would tell the present path
 * that a frame was shown when nothing was shown at all, and the contract
 * would pass on a picture that does not exist. */
static float s_fake_colour[4];
static void *s_fake_image;
static unsigned long s_fake_acquire_calls;
static unsigned long s_fake_render_calls;
static unsigned long s_fake_submit_calls;

static int fake_acquire_image(nvn_device_state *device, uint32_t *out_index, void **out_image,
                              char *reason, size_t reason_size) {
    (void)device;
    if(!out_index || !out_image) {
        if(reason && reason_size) snprintf(reason, reason_size,"the outputs of the fake acquire are NULL");
        return 0;
    }
    s_fake_acquire_calls++;
    *out_index = 0;
    *out_image = &s_fake_image;
    if(reason && reason_size) snprintf(reason, reason_size,"the fake backend has no swapchain");
    return 1;
}

static int fake_begin_render_pass(nvn_device_state *device, uint32_t index, void *image,
                                  const float rgba[4], char *reason, size_t reason_size) {
    (void)device;
    (void)index;
    (void)image;
    if(!rgba) {
        if(reason && reason_size) snprintf(reason, reason_size,"the colour of the fake render pass is NULL");
        return 0;
    }
    s_fake_render_calls++;
    /* The colour is recorded bit for bit. Nothing here rounds it or picks
     * another one. */
    memcpy(s_fake_colour, rgba, sizeof(s_fake_colour));
    if(reason && reason_size) snprintf(reason, reason_size,"the fake backend has no render pass");
    return 1;
}

static int fake_submit_and_present(nvn_device_state *device, uint32_t index, void *image,
                                   char *reason, size_t reason_size) {
    (void)device;
    (void)index;
    (void)image;
    s_fake_submit_calls++;
    if(reason && reason_size) snprintf(reason, reason_size,"the fake backend never presents anything");
    /* The fake backend refuses to present: it says why and it returns 0, so
     * that nothing claims a frame was shown. */
    return 0; /* FAKE_SUBMIT_REFUSES */
}

static const nvn_backend s_fake_backend = {
    .name = "fake",
    .acquire_image = fake_acquire_image,
    .begin_render_pass = fake_begin_render_pass,
    .submit_and_present = fake_submit_and_present,
};

/* A backend that does not implement the present path at all. */
static const nvn_backend s_empty_backend = {
    .name = "empty",
};

/* The clear colour of the caller. The four values are different from each
 * other, so a colour with swapped channels cannot pass the comparison. */
static const float s_test_colour[4] = { 0.25f, 0.5f, 0.75f, 1.0f };

static int nvn_present_contract(void) {
    const nvn_backend *none_backend;
    nvn_device_state none_device;
    nvn_device_state fake_device;
    nvn_present_stats stats;
    char reason[NVN_PRESENT_REASON_SIZE];
    uint32_t index = 0;
    void *image = NULL;
    int result;

    none_backend = nvn_device_default_backend();
    CHECK(none_backend != NULL);
    memset(&none_device, 0, sizeof(none_device));
    none_device.backend = none_backend;
    memset(&fake_device, 0, sizeof(fake_device));
    fake_device.backend = &s_fake_backend;

    /* 1. The three steps of the path exist and they refuse, with a reason.
     *    The backend of a host build has no swapchain, so every step of the
     *    path has to say that it cannot do its part. */
    CHECK(none_backend->acquire_image != NULL);
    CHECK(none_backend->begin_render_pass != NULL);
    CHECK(none_backend->submit_and_present != NULL);

    reason[0] = '\0';
    result = none_backend->acquire_image(&none_device, &index, &image, reason, sizeof(reason));
    expect_value("nvn-present-none-acquire-refuses", (uint64_t)(uint32_t)result, (uint64_t)0);
    expect_condition("nvn-present-none-acquire-names-the-reason", reason[0] != '\0');
    expect_condition("nvn-present-none-acquire-gives-no-image", image == NULL);

    reason[0] = '\0';
    result = none_backend->begin_render_pass(&none_device, 0, NULL, s_test_colour, reason, sizeof(reason));
    expect_value("nvn-present-none-render-pass-refuses", (uint64_t)(uint32_t)result, (uint64_t)0);
    expect_condition("nvn-present-none-render-pass-names-the-reason", reason[0] != '\0');

    reason[0] = '\0';
    result = none_backend->submit_and_present(&none_device, 0, NULL, reason, sizeof(reason));
    expect_value("nvn-present-none-submit-refuses", (uint64_t)(uint32_t)result, (uint64_t)0);
    expect_condition("nvn-present-none-submit-names-the-reason", reason[0] != '\0');

    /* 2. The arguments are checked. A NULL is refused and does not crash. */
    reason[0] = '\0';
    result = none_backend->acquire_image(&none_device, NULL, NULL, reason, sizeof(reason));
    expect_value("nvn-present-none-acquire-refuses-null-outputs", (uint64_t)(uint32_t)result, (uint64_t)0);

    reason[0] = '\0';
    result = none_backend->acquire_image(NULL, &index, &image, reason, sizeof(reason));
    expect_value("nvn-present-none-acquire-refuses-a-null-device", (uint64_t)(uint32_t)result, (uint64_t)0);

    nvn_present_reset_stats();
    result = nvn_present_frame(NULL, s_test_colour);
    expect_value("nvn-present-refuses-a-null-device", (uint64_t)(uint32_t)result, (uint64_t)0);
    nvn_present_get_stats(&stats);
    expect_condition("nvn-present-names-a-null-device", stats.last_reason[0] != '\0');
    expect_value("nvn-present-counts-a-null-device", (uint64_t)stats.failures, (uint64_t)1);

    nvn_present_reset_stats();
    result = nvn_present_frame(&fake_device, NULL);
    expect_value("nvn-present-refuses-a-null-colour", (uint64_t)(uint32_t)result, (uint64_t)0);
    nvn_present_get_stats(&stats);
    expect_condition("nvn-present-names-a-null-colour", stats.last_reason[0] != '\0');
    expect_value("nvn-present-counts-a-null-colour", (uint64_t)stats.failures, (uint64_t)1);

    /* A backend without the three steps is refused as well. */
    {
        nvn_device_state empty_device;
        memset(&empty_device, 0, sizeof(empty_device));
        empty_device.backend = &s_empty_backend;
        nvn_present_reset_stats();
        result = nvn_present_frame(&empty_device, s_test_colour);
        expect_value("nvn-present-refuses-a-backend-without-the-path", (uint64_t)(uint32_t)result, (uint64_t)0);
        nvn_present_get_stats(&stats);
        expect_condition("nvn-present-names-a-backend-without-the-path", stats.last_reason[0] != '\0');
    }

    /* 3. A failure is named. The reason says which step failed, and the log
     *    says the same thing. */
    nvn_present_reset_stats();
    capture_start();
    result = nvn_present_frame(&none_device, s_test_colour);
    capture_stop();
    expect_value("nvn-present-refuses-with-the-backend-of-this-build", (uint64_t)(uint32_t)result, (uint64_t)0);
    nvn_present_get_stats(&stats);
    expect_condition("nvn-present-names-the-step-that-failed",
                     strncmp(stats.last_reason,"acquire:",8) == 0);
    expect_condition("nvn-present-logs-the-same-reason",
                     strstr(s_capture_text, stats.last_reason) != NULL);
    expect_value("nvn-present-counts-the-acquire-failure", (uint64_t)stats.acquire_failures, (uint64_t)1);
    expect_value("nvn-present-counts-the-failure", (uint64_t)stats.failures, (uint64_t)1);
    expect_value("nvn-present-counted-the-frame", (uint64_t)stats.frames_requested, (uint64_t)1);
    expect_value("nvn-present-acquired-nothing", (uint64_t)stats.frames_acquired, (uint64_t)0);

    /* 4. A failure does not leave the impression that a frame was shown. */
    expect_value("nvn-present-shows-nothing-when-the-step-failed",
                 (uint64_t)stats.frames_presented, (uint64_t)0);
    expect_value("nvn-present-submitted-nothing-when-the-step-failed",
                 (uint64_t)stats.frames_submitted, (uint64_t)0);

    /* 5. The colour is the colour of the caller. The fake backend records
     *    the four floats it was given and refuses to present, so the colour
     *    can be compared while the frame is still not shown. */
    memset(s_fake_colour, 0, sizeof(s_fake_colour));
    s_fake_acquire_calls = 0;
    s_fake_render_calls = 0;
    s_fake_submit_calls = 0;
    nvn_present_reset_stats();
    capture_start();
    result = nvn_present_frame(&fake_device, s_test_colour);
    capture_stop();
    expect_value("nvn-present-the-fake-backend-refuses-to-present", (uint64_t)(uint32_t)result, (uint64_t)0);
    expect_value("nvn-present-the-fake-backend-was-asked-for-the-image",
                 (uint64_t)s_fake_acquire_calls, (uint64_t)1);
    expect_value("nvn-present-the-fake-backend-was-asked-for-the-render-pass",
                 (uint64_t)s_fake_render_calls, (uint64_t)1);
    expect_value("nvn-present-the-fake-backend-was-asked-to-present",
                 (uint64_t)s_fake_submit_calls, (uint64_t)1);
    expect_condition("nvn-present-the-colour-is-the-colour-of-the-caller",
                     memcmp(s_fake_colour, s_test_colour, sizeof(s_test_colour)) == 0);
    nvn_present_get_stats(&stats);
    expect_value("nvn-present-the-fake-frame-was-not-shown",
                 (uint64_t)stats.frames_presented, (uint64_t)0);
    expect_value("nvn-present-the-fake-frame-was-not-submitted",
                 (uint64_t)stats.frames_submitted, (uint64_t)0);
    expect_condition("nvn-present-names-the-step-that-refused",
                     strncmp(stats.last_reason,"submit and present:",18) == 0);
    expect_condition("nvn-present-logs-the-same-reason-of-the-fake-backend",
                     strstr(s_capture_text, stats.last_reason) != NULL);

    /* The counters of the whole contract. */
    expect_value("nvn-present-counts-every-frame", (uint64_t)stats.frames_requested, (uint64_t)1);
    expect_value("nvn-present-counts-every-failure", (uint64_t)stats.failures, (uint64_t)1);

    /* The statistics of a NULL output are refused and do not crash. */
    nvn_present_get_stats(NULL);

    /* The final gate. Without it the contract could exit 0 while the
     * expectations had failed. */
    CHECK(!errors);
    printf("{\"nvn_present_errors\":%u,\"frames_requested\":%lu,\"frames_presented\":%lu,"
           "\"failures\":%lu,\"acquire_failures\":%lu,\"submit_failures\":%lu}\n",
           errors,
           (unsigned long)stats.frames_requested,
           (unsigned long)stats.frames_presented,
           (unsigned long)stats.failures,
           (unsigned long)stats.acquire_failures,
           (unsigned long)stats.submit_failures);
    return 0;
}

int main(int argc,char **argv) {
    if(argc!=2 || strcmp(argv[1],"--nvn-present")) {
        fprintf(stderr,"usage: %s --nvn-present\n",argv[0]);
        return 2;
    }
    return nvn_present_contract();
}
