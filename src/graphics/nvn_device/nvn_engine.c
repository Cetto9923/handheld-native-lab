/* The seam between the NVN device layer and the Vulkan engine that replays
 * the commands. See nvn_engine.h for what it is for.
 *
 * The engine was written against the NVN objects of this game
 * (.local/refs/nxnative/dm/gfx/nvn/nvn_obj.h), and the objects this runtime
 * makes for it are in that layout (nvn_obj_ffrs.h). The assertions below
 * hold the two layouts together: the engine reads the fields of these
 * objects, and a field that moved would be read as another field.
 */
#include "nvn_engine.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "nvn_bridge.h"
#include "nvn_device.h"
#include "nvn_window.h"
#include "nvn_vulkan_features.h"

#include "../nvn_vulkan/engine/xbg_game.h"
#include "../nvn_vulkan/engine/xbg_game_internal.h"
#include "../nvn_vulkan/nvn_vk_adapter.h"

/* The engine's objects are the reference's objects, and the bridge makes
 * the reference's layout. These are the sizes and the offsets of the
 * reference's own headers, measured by compiling against them. */
_Static_assert(sizeof(tex_t) == 128, "the engine's texture object is not the reference's");
_Static_assert(offsetof(tex_t, d) == 4, "the engine's texture description moved");
_Static_assert(offsetof(tex_t, L) == 48, "the engine's texture layout moved");
_Static_assert(offsetof(tex_t, mem) == 88, "the engine's texture storage moved");
_Static_assert(offsetof(tex_t, pool) == 96, "the engine's texture pool moved");
_Static_assert(offsetof(tex_t, off) == 104, "the engine's texture offset moved");
_Static_assert(offsetof(tex_t, serial) == 120, "the engine's texture number moved");
_Static_assert(sizeof(view_t) == 16, "the engine's view is not the reference's");
_Static_assert(sizeof(samp_par) == 40, "the engine's sampler is not the reference's");
_Static_assert(sizeof(pool_t) == 24, "the engine's memory pool is not the reference's");
_Static_assert(sizeof(dpool_t) == 24, "the engine's descriptor pool is not the reference's");
_Static_assert(offsetof(dpool_t, off) == 8, "the engine's pool offset moved");
_Static_assert(offsetof(dpool_t, n) == 16, "the engine's pool count moved");
_Static_assert(sizeof(prog_t) == 112, "the engine's program is not the reference's");
_Static_assert(offsetof(prog_t, sd) == 8, "the engine's shader data moved");
_Static_assert(offsetof(prog_t, serial) == 104, "the engine's program number moved");

static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static int s_enabled = 1;
static char s_shader_directory[1024];
static int s_ready;
static nvn_device_state *s_device;

/* The present path of the engine asks for the game's own render target to be
 * shown. This runtime owns the swapchain, so the copy into it happens here.
 *
 * The engine has already put the image into the layout a copy needs, and it
 * has waited for the work that wrote it, so the copy is of a finished image. */
static int engine_present(void *userdata, void *native_window, VkImage image, VkFormat format,
                          uint32_t width, uint32_t height, const int32_t crop[4], int32_t interval) {
    nvn_device_state *device = (nvn_device_state *)userdata;
    const nvn_backend *backend = device ? device->backend : NULL;
    char reason[256];
    reason[0] = '\0';
    if (!backend || !backend->present_image) {
        return -1;
    }
    int outcome = backend->present_image(device, (void *)(uintptr_t)image, (uint32_t)format, width, height, crop,
                                         interval, reason, sizeof(reason));
    if (outcome == NVN_PRESENT_SKIPPED) return 1;
    if (outcome != NVN_PRESENT_QUEUED) {
        static unsigned err_count = 0;
        if (err_count++ < 10) {
            fprintf(stderr, "[nvn] the engine present skipped: %s\n",
                    reason[0] ? reason : "frame dropped");
        }
        return -1;
    }
    (void)native_window;
    return 0;
}

static void engine_log(void *userdata, const char *message) {
    (void)userdata;
    fprintf(stderr, "[engine] %s\n", message ? message : "");
}

static void engine_fatal(void *userdata, const char *message) {
    (void)userdata;
    fprintf(stderr, "[engine] FATAL: %s\n", message ? message : "");
    /* The engine is about to leave. There is nothing left for this runtime
     * to do, and the guest cannot go on without the graphics. */
    fflush(NULL);
    _exit(70);
}

void nvn_engine_set_enabled(int enabled) {
    pthread_mutex_lock(&s_lock);
    s_enabled = enabled ? 1 : 0;
    pthread_mutex_unlock(&s_lock);
}

void nvn_engine_set_shader_directory(const char *directory) {
    pthread_mutex_lock(&s_lock);
    if (directory) {
        snprintf(s_shader_directory, sizeof(s_shader_directory), "%s", directory);
    } else {
        s_shader_directory[0] = '\0';
    }
    pthread_mutex_unlock(&s_lock);
}

int nvn_engine_initialize(nvn_device_state *device, char *reason, size_t reason_size) {
    const nvn_backend *backend;
    void *physical = NULL;
    void *vk_device = NULL;
    void *queue = NULL;
    uint32_t family = 0;
    xbg_vk_config config;
    char error[512];
    int result;

    pthread_mutex_lock(&s_lock);
    {
        int enabled = s_enabled;
        pthread_mutex_unlock(&s_lock);
        if (!enabled) {
            if (reason && reason_size) {
                snprintf(reason, reason_size, "the engine is switched off for this run");
            }
            return 0;
        }
    }
    if (!device || !device->backend) {
        if (reason && reason_size) snprintf(reason, reason_size, "there is no device to start the engine on");
        return 0;
    }
    backend = device->backend;
    if (!backend->get_device_queue || !backend->get_device_queue(device, &physical, &vk_device, &queue, &family)) {
        if (reason && reason_size) {
            snprintf(reason, reason_size, "this runtime has no Vulkan device and queue to give to the engine");
        }
        return 0;
    }
    pthread_mutex_lock(&s_lock);
    if (s_ready) {
        pthread_mutex_unlock(&s_lock);
        return 1;
    }
    if (!s_shader_directory[0]) {
        pthread_mutex_unlock(&s_lock);
        if (reason && reason_size) {
            snprintf(reason, reason_size, "there is no directory of shader bundles to give to the engine");
        }
        return 0;
    }
    pthread_mutex_unlock(&s_lock);

    memset(&config, 0, sizeof(config));
    memset(&error, 0, sizeof(error));
    config.size = (uint32_t)sizeof(config);
    config.game.size = (uint32_t)sizeof(config.game);
    config.game.physical = (VkPhysicalDevice)(uintptr_t)physical;
    config.game.device = (VkDevice)(uintptr_t)vk_device;
    config.game.queue = (VkQueue)(uintptr_t)queue;
    config.game.queue_family = family;
    config.game.fence_timeout_ns = 2000000000ull;
    config.game.userdata = device;
    config.game.present = engine_present;
    config.game.log = engine_log;
    config.game.fatal = engine_fatal;
    config.display_hz = 60;
    pthread_mutex_lock(&s_lock);
    config.game.shader_directory = s_shader_directory;
    pthread_mutex_unlock(&s_lock);

    /* The features of the device that the engine needs are the ones the
     * device was created with; the engine has to submit on the same device
     * or an image of one device cannot be given to the queue of another. */
    {
        nvn_vulkan_enabled_features enabled;
        if (!nvn_vulkan_get_enabled_features(device, &enabled)) {
            if (reason && reason_size) snprintf(reason, reason_size, "the Vulkan backend has no successful device feature record");
            return 0;
        }
        config.game.enabled_core = enabled.core;
        config.game.enabled_shader_draw_parameters = enabled.shader_draw_parameters;
        config.game.instance_api_version = enabled.instance_api_version;
        config.game.enabled_shader_output_layer = enabled.shader_output_layer;
        config.game.enabled_shader_output_viewport_index = enabled.shader_output_viewport_index;
        fprintf(stderr, "[engine] using successful device feature request: instance API %u.%u.%u, "
                        "physical API %u.%u.%u, enabled shaderDrawParameters=%u outputLayer=%u outputViewportIndex=%u legacyLayerEXT=0\n",
                VK_VERSION_MAJOR(enabled.instance_api_version), VK_VERSION_MINOR(enabled.instance_api_version),
                VK_VERSION_PATCH(enabled.instance_api_version), VK_VERSION_MAJOR(enabled.physical_api_version),
                VK_VERSION_MINOR(enabled.physical_api_version), VK_VERSION_PATCH(enabled.physical_api_version),
                enabled.shader_draw_parameters,enabled.shader_output_layer,
                enabled.shader_output_viewport_index);
    }

    result = xbg_vk_initialize(&config, error, sizeof(error));
    if (result) {
        if (reason && reason_size) {
            snprintf(reason, reason_size, "%s", error[0] ? error : "the engine refused to start");
        }
        fprintf(stderr, "[engine] the engine did not start: %s\n", error[0] ? error : "no reason given");
        return 0;
    }
    pthread_mutex_lock(&s_lock);
    s_ready = 1;
    s_device = device;
    pthread_mutex_unlock(&s_lock);
    fprintf(stderr, "[engine] the engine is running on the device of this runtime; the shaders are read from %s\n",
            s_shader_directory);
    return 1;
}

int nvn_engine_ready(void) {
    int ready;
    pthread_mutex_lock(&s_lock);
    ready = s_ready;
    pthread_mutex_unlock(&s_lock);
    return ready;
}

uint64_t nvn_engine_last_fence(void) {
    return nvn_engine_ready() ? xbg_legacy_last_fence() : 0;
}

int nvn_engine_wait(uint64_t fence, uint64_t timeout) {
    return nvn_engine_ready() ? xbg_legacy_wait(fence, timeout) : 0;
}

int nvn_engine_submit(const uint64_t *handles, int count) {
    if (!nvn_engine_ready() || !handles || count <= 0) return 0;
    xbg_submit(count, handles);
    return 1;
}

void nvn_engine_pool_add(void *memory, size_t bytes) {
    if (!nvn_engine_ready() || !memory || !bytes) return;
    xbg_legacy_pool_add(memory, bytes);
}

void nvn_engine_pool_del(void *memory) {
    if (!nvn_engine_ready() || !memory) return;
    xbg_legacy_pool_del(memory);
}

int nvn_engine_register_texture(const void *guest_pool, int id, const void *guest_texture,
                                const void *guest_view) {
    const ffrs_dpool *pool;
    ffrs_tex texture;
    ffrs_view view_value;
    uint64_t view_serial;
    const ffrs_view *view = NULL;
    if (!nvn_engine_ready()) return 0;
    pool = nvn_bridge_texture_pool(guest_pool);
    int have_texture = nvn_bridge_texture_copy(guest_texture, &texture);
    if (!pool || !have_texture) {
        fprintf(stderr, "[engine] the texture %d of a pool could not be registered: %s\n", id,
                !pool ? "the pool is unknown" : "the texture is unknown");
        return 0;
    }
    if (guest_view && nvn_texture_view_info_of(guest_view, &view_value, &view_serial)) view = &view_value;
    xbg_register_texture((const dpool_t *)pool, id, (const tex_t *)&texture, (const view_t *)view);
    return 1;
}

int nvn_engine_register_sampler(const void *guest_pool, int id, const void *guest_sampler) {
    const ffrs_dpool *pool;
    const ffrs_samp_par *sampler;
    if (!nvn_engine_ready()) return 0;
    pool = nvn_bridge_sampler_pool(guest_pool);
    sampler = nvn_bridge_sampler(guest_sampler);
    if (!pool || !sampler) {
        fprintf(stderr, "[engine] the sampler %d of a pool could not be registered: %s\n", id,
                !pool ? "the pool is unknown" : "the sampler is unknown");
        return 0;
    }
    xbg_register_sampler((const dpool_t *)pool, id, (const samp_par *)sampler);
    return 1;
}

int nvn_engine_present(const void *guest_window, int index) {
    nvn_window_present_state state;
    win_t window;
    const ffrs_tex *texture;
    if (!nvn_engine_ready()) return 0;
    if (!nvn_window_present_state_of(guest_window, &state)) {
        fprintf(stderr, "[engine] the window %p is not a window this runtime knows\n", guest_window);
        return 0;
    }
    if (index < 0 || index >= state.ntex || !state.textures[index]) {
        fprintf(stderr, "[engine] the image %d of a window of %d images was asked for and does not exist\n", index,
                state.ntex);
        return 0;
    }
    texture = nvn_bridge_texture(state.textures[index]);
    if (!texture) {
        fprintf(stderr, "[engine] the swapchain texture %d is not a texture this runtime knows\n", index);
        return 0;
    }
    memset(&window, 0, sizeof(window));
    window.magic = FFRS_NVN_MAGIC('W', 'I', 'N', 'W');
    window.native = state.native_window;
    window.ntex = state.ntex;
    window.interval = state.interval;
    window.next = index;
    memcpy(window.crop, state.crop, sizeof(window.crop));
    xbg_present_texture(&window, (const tex_t *)texture);
    return 1;
}

void nvn_engine_get_stats(nvn_engine_stats *out) {
    xbg_game_stats stats;
    if (!out) return;
    memset(&stats, 0, sizeof(stats));
    if (nvn_engine_ready()) xbg_game_get_stats(&stats);
    out->submissions = stats.submissions;
    out->draws = stats.draws;
    out->dispatches = stats.dispatches;
    out->presents = stats.presents;
    out->shader_loads = stats.shader_loads;
}
