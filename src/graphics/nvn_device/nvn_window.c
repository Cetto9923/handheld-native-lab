/* The NVN window family implementation.
 *
 * See nvn_window.h for why the state lives on the host.
 *
 * The window is where the guest's picture meets the screen. The guest gives
 * this layer the platform window (through nvnWindowBuilderSetNativeWindow)
 * and the textures it wants to present into; this layer turns that into a
 * real surface and a real swapchain, and reports exactly what it got. When
 * there is no platform window, there is no surface, and the log says so
 * instead of pretending that a frame has somewhere to go.
 */
#include "nvn_window.h"

#include "nvn_device.h"
#include "nvn_engine.h"
#include "nvn_sync.h"

#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NVN_WINDOW_MAX_BUILDER_STATES 8
#define NVN_WINDOW_MAX_STATES 8

typedef struct {
    void *builder;
    void *device;
    void *native_window;
    uint32_t flags;
    uint32_t present_interval;
    uint32_t num_textures;
    uint32_t num_active_textures;
    void *textures[NVN_WINDOW_MAX_TEXTURES];
    int in_use;
} nvn_window_builder_state;

typedef struct {
    void *window;
    void *device;
    void *native_window;
    uint32_t flags;
    uint32_t present_interval;
    uint32_t num_textures;
    uint32_t num_active_textures;
    void *surface;
    uint32_t next_texture;
    void *swapchain;
    uint32_t width;
    uint32_t height;
    uint32_t crop_x, crop_y, crop_width, crop_height;
    /* The textures of the swapchain. They are the guest's own textures and
     * the guest renders into them, so the present path has to show one of
     * them and not a copy of its own. */
    void *textures[NVN_WINDOW_MAX_TEXTURES];
    uint64_t texture_fences[NVN_WINDOW_MAX_TEXTURES];
    char debug_label[64];
    int in_use;
} nvn_window_state;

static pthread_mutex_t s_window_lock = PTHREAD_MUTEX_INITIALIZER;
static nvn_window_builder_state s_window_builders[NVN_WINDOW_MAX_BUILDER_STATES];
static nvn_window_state s_windows[NVN_WINDOW_MAX_STATES];
static nvn_window_stats s_window_stats;

static nvn_window_builder_state *window_builder_find(void *builder, int create) {
    size_t i;
    nvn_window_builder_state *free_slot = NULL;
    if (!builder) return NULL;
    for (i = 0; i < NVN_WINDOW_MAX_BUILDER_STATES; i++) {
        if (s_window_builders[i].in_use) {
            if (s_window_builders[i].builder == builder) return &s_window_builders[i];
        } else if (!free_slot) {
            free_slot = &s_window_builders[i];
        }
    }
    if (!create || !free_slot) return NULL;
    memset(free_slot, 0, sizeof(*free_slot));
    free_slot->builder = builder;
    free_slot->in_use = 1;
    return free_slot;
}

static nvn_window_state *window_find(void *window, int create) {
    size_t i;
    nvn_window_state *free_slot = NULL;
    if (!window) return NULL;
    for (i = 0; i < NVN_WINDOW_MAX_STATES; i++) {
        if (s_windows[i].in_use) {
            if (s_windows[i].window == window) return &s_windows[i];
        } else if (!free_slot) {
            free_slot = &s_windows[i];
        }
    }
    if (!create || !free_slot) return NULL;
    memset(free_slot, 0, sizeof(*free_slot));
    free_slot->window = window;
    free_slot->in_use = 1;
    return free_slot;
}

/* ------------------------------------------------------------------ */
/* The builder                                                         */
/* ------------------------------------------------------------------ */

void nvn_window_builder_set_defaults(void *builder) {
    nvn_window_builder_state *b;
    pthread_mutex_lock(&s_window_lock);
    s_window_stats.builder_calls++;
    b = window_builder_find(builder, 1);
    if (b) {
        b->device = NULL;
        b->native_window = NULL;
        b->flags = 0;
        b->present_interval = 0;
        b->num_textures = 0;
        b->num_active_textures = 0;
        memset(b->textures, 0, sizeof(b->textures));
    }
    pthread_mutex_unlock(&s_window_lock);
    if (!b) fprintf(stderr, "[nvn] nvnWindowBuilderSetDefaults: no room for another builder (%p)\n", builder);
    NVN_VOID_RETURN();
}

void nvn_window_builder_set_device(void *builder, void *device) {
    nvn_window_builder_state *b;
    pthread_mutex_lock(&s_window_lock);
    s_window_stats.builder_calls++;
    b = window_builder_find(builder, 1);
    if (b) b->device = device;
    pthread_mutex_unlock(&s_window_lock);
    NVN_VOID_RETURN();
}

void nvn_window_builder_set_native_window(void *builder, void *native_window) {
    nvn_window_builder_state *b;
    pthread_mutex_lock(&s_window_lock);
    s_window_stats.builder_calls++;
    b = window_builder_find(builder, 1);
    if (b) b->native_window = native_window;
    pthread_mutex_unlock(&s_window_lock);
    fprintf(stderr, "[nvn] nvnWindowBuilderSetNativeWindow: builder=%p native_window=%p\n",
            builder, native_window);
    NVN_VOID_RETURN();
}

void nvn_window_builder_set_textures(void *builder, int count, void *textures) {
    nvn_window_builder_state *b;
    pthread_mutex_lock(&s_window_lock);
    s_window_stats.builder_calls++;
    b = window_builder_find(builder, 1);
    if (b) {
        b->textures[0] = NULL;
        b->num_textures = 0;
        if (textures && count > 0) {
            /* The guest's texture handles are copied here, so no guest array
             * is read later. A count beyond what this layer keeps is reported
             * and the rest is dropped, never silently ignored. */
            uint32_t keep = (uint32_t)count;
            if (keep > NVN_WINDOW_MAX_TEXTURES) keep = NVN_WINDOW_MAX_TEXTURES;
            const void **source = (const void **)textures;
            for (uint32_t i = 0; i < keep; i++) b->textures[i] = (void *)(uintptr_t)source[i];
            b->num_textures = keep;
            s_window_stats.textures_recorded += keep;
        }
    }
    pthread_mutex_unlock(&s_window_lock);
    fprintf(stderr, "[nvn] nvnWindowBuilderSetTextures: builder=%p count=%d textures=%p (recorded=%u)\n",
            builder, count, textures, b ? b->num_textures : 0);
    if (count > NVN_WINDOW_MAX_TEXTURES)
        fprintf(stderr, "[nvn] nvnWindowBuilderSetTextures: %d textures requested, this layer keeps %d\n",
                count, NVN_WINDOW_MAX_TEXTURES);
    NVN_VOID_RETURN();
}

void nvn_window_builder_set_present_interval(void *builder, int interval) {
    nvn_window_builder_state *b;
    pthread_mutex_lock(&s_window_lock);
    s_window_stats.builder_calls++;
    b = window_builder_find(builder, 1);
    if (b) b->present_interval = (uint32_t)interval;
    pthread_mutex_unlock(&s_window_lock);
    NVN_VOID_RETURN();
}

void nvn_window_builder_set_num_active_textures(void *builder, int count) {
    nvn_window_builder_state *b;
    pthread_mutex_lock(&s_window_lock);
    s_window_stats.builder_calls++;
    b = window_builder_find(builder, 1);
    if (b) b->num_active_textures = (uint32_t)count;
    pthread_mutex_unlock(&s_window_lock);
    NVN_VOID_RETURN();
}

void *nvn_window_builder_get_device(const void *builder) {
    nvn_window_builder_state *b;
    void *device = NULL;
    pthread_mutex_lock(&s_window_lock);
    s_window_stats.builder_calls++;
    b = window_builder_find((void *)(uintptr_t)builder, 0);
    if (b) device = b->device;
    pthread_mutex_unlock(&s_window_lock);
    return device;
}

void *nvn_window_builder_get_native_window(const void *builder) {
    nvn_window_builder_state *b;
    void *window = NULL;
    pthread_mutex_lock(&s_window_lock);
    s_window_stats.builder_calls++;
    b = window_builder_find((void *)(uintptr_t)builder, 0);
    if (b) window = b->native_window;
    pthread_mutex_unlock(&s_window_lock);
    return window;
}

int nvn_window_builder_get_flags(const void *builder) {
    nvn_window_builder_state *b;
    int flags = 0;
    pthread_mutex_lock(&s_window_lock);
    s_window_stats.builder_calls++;
    b = window_builder_find((void *)(uintptr_t)builder, 0);
    if (b) flags = (int)(uint32_t)b->flags;
    pthread_mutex_unlock(&s_window_lock);
    return flags;
}

int nvn_window_builder_get_num_textures(const void *builder) {
    nvn_window_builder_state *b;
    int count = 0;
    pthread_mutex_lock(&s_window_lock);
    s_window_stats.builder_calls++;
    b = window_builder_find((void *)(uintptr_t)builder, 0);
    if (b) count = (int)b->num_textures;
    pthread_mutex_unlock(&s_window_lock);
    return count;
}

int nvn_window_builder_get_num_active_textures(const void *builder) {
    nvn_window_builder_state *b;
    int count = 0;
    pthread_mutex_lock(&s_window_lock);
    s_window_stats.builder_calls++;
    b = window_builder_find((void *)(uintptr_t)builder, 0);
    if (b) count = (int)b->num_active_textures;
    pthread_mutex_unlock(&s_window_lock);
    return count;
}

int nvn_window_builder_get_present_interval(const void *builder) {
    nvn_window_builder_state *b;
    int interval = 0;
    pthread_mutex_lock(&s_window_lock);
    s_window_stats.builder_calls++;
    b = window_builder_find((void *)(uintptr_t)builder, 0);
    if (b) interval = (int)b->present_interval;
    pthread_mutex_unlock(&s_window_lock);
    return interval;
}

void *nvn_window_builder_get_texture(const void *builder, int index) {
    nvn_window_builder_state *b;
    void *texture = NULL;
    pthread_mutex_lock(&s_window_lock);
    s_window_stats.builder_calls++;
    b = window_builder_find((void *)(uintptr_t)builder, 0);
    if (b && index >= 0 && (uint32_t)index < b->num_textures) texture = b->textures[index];
    pthread_mutex_unlock(&s_window_lock);
    return texture;
}

/* ------------------------------------------------------------------ */
/* The window                                                          */
/* ------------------------------------------------------------------ */

int nvn_window_initialize(void *window, const void *builder, int flags) {
    nvn_window_builder_state *b;
    nvn_window_state *w;
    const nvn_backend *backend = nvn_device_backend();
    void *surface = NULL;
    void *swapchain = NULL;
    uint32_t width = 0, height = 0;
    int created_surface = 0;

    pthread_mutex_lock(&s_window_lock);
    s_window_stats.builder_calls++;
    b = window_builder_find((void *)(uintptr_t)builder, 0);
    if (!b) {
        s_window_stats.refused++;
        pthread_mutex_unlock(&s_window_lock);
        fprintf(stderr, "[nvn] nvnWindowInitialize: no builder state for %p; the guest did not prepare one\n",
                builder);
        return 0;
    }
    if (!nvn_device_is_ready(b->device)) {
        s_window_stats.refused++;
        pthread_mutex_unlock(&s_window_lock);
        fprintf(stderr, "[nvn] nvnWindowInitialize: the device in the builder (%p) is not an initialized device\n",
                b->device);
        return 0;
    }
    w = window_find(window, 1);
    if (!w) {
        s_window_stats.refused++;
        pthread_mutex_unlock(&s_window_lock);
        fprintf(stderr, "[nvn] nvnWindowInitialize: no room for another window\n");
        return 0;
    }
    w->device = b->device;
    w->native_window = b->native_window;
    w->flags = (uint32_t)flags;
    w->present_interval = b->present_interval;
    w->num_textures = b->num_textures;
    w->num_active_textures = b->num_active_textures;
    w->next_texture = 0;
    {
        uint32_t i;
        for (i = 0; i < NVN_WINDOW_MAX_TEXTURES; i++) {
            w->textures[i] = (i < b->num_textures && b->num_textures <= NVN_WINDOW_MAX_TEXTURES)
                                 ? b->textures[i]
                                 : NULL;
        }
    }
    w->surface = NULL;
    w->swapchain = NULL;
    w->width = 0;
    w->height = 0;
    if (b->native_window && backend && backend->create_window_surface) {
        if (backend->create_window_surface(nvn_device_state_handle(), b->native_window, &width, &height,
                                           &surface, &swapchain)) {
            w->surface = surface;
            w->swapchain = swapchain;
            w->width = width;
            w->height = height;
            created_surface = 1;
            s_window_stats.surfaces_created++;
            s_window_stats.swapchains_created++;
            s_window_stats.width = width;
            s_window_stats.height = height;
        }
    }
    w->in_use = 1;
    s_window_stats.initialized++;
    pthread_mutex_unlock(&s_window_lock);

    fprintf(stderr, "[nvn] nvnWindowInitialize: window=%p builder=%p device=%p native_window=%p "
                    "textures=%u active=%u present_interval=%u surface=%p swapchain=%p size=%ux%u -> true\n",
            window, builder, b->device, b->native_window, b->num_textures, b->num_active_textures,
            b->present_interval, surface, swapchain, width, height);
    if (!created_surface)
        fprintf(stderr, "[nvn] nvnWindowInitialize: no platform surface for this window; a frame has "
                        "nowhere to go yet\n");
    return 1;
}

void nvn_window_finalize(void *window) {
    nvn_window_state *w;
    const nvn_backend *backend = nvn_device_backend();
    pthread_mutex_lock(&s_window_lock);
    w = window_find(window, 0);
    if (w) {
        if (w->surface && backend && backend->destroy_window_surface)
            backend->destroy_window_surface(nvn_device_state_handle(), w->surface, w->swapchain);
        w->surface = NULL;
        w->swapchain = NULL;
        w->in_use = 0;
        s_window_stats.finalized++;
    }
    pthread_mutex_unlock(&s_window_lock);
    fprintf(stderr, "[nvn] nvnWindowFinalize: window=%p\n", window);
    NVN_VOID_RETURN();
}

void *nvn_window_get_native_window(const void *window) {
    nvn_window_state *w;
    void *native_window = NULL;
    pthread_mutex_lock(&s_window_lock);
    w = window_find((void *)(uintptr_t)window, 0);
    if (w) native_window = w->native_window;
    pthread_mutex_unlock(&s_window_lock);
    return native_window;
}

int nvn_window_get_num_textures(const void *window) {
    nvn_window_state *w;
    int count = 0;
    pthread_mutex_lock(&s_window_lock);
    w = window_find((void *)(uintptr_t)window, 0);
    if (w) count = (int)w->num_textures;
    pthread_mutex_unlock(&s_window_lock);
    return count;
}

int nvn_window_get_num_active_textures(const void *window) {
    nvn_window_state *w;
    int count = 0;
    pthread_mutex_lock(&s_window_lock);
    w = window_find((void *)(uintptr_t)window, 0);
    if (w) count = (int)w->num_active_textures;
    pthread_mutex_unlock(&s_window_lock);
    return count;
}

int nvn_window_get_present_interval(const void *window) {
    nvn_window_state *w;
    int interval = 0;
    pthread_mutex_lock(&s_window_lock);
    w = window_find((void *)(uintptr_t)window, 0);
    if (w) interval = (int)w->present_interval;
    pthread_mutex_unlock(&s_window_lock);
    return interval;
}

void nvn_window_set_num_active_textures(void *window, int count) {
    nvn_window_state *w;
    pthread_mutex_lock(&s_window_lock);
    w = window_find(window, 1);
    if (w) w->num_active_textures = (uint32_t)count;
    pthread_mutex_unlock(&s_window_lock);
    NVN_VOID_RETURN();
}

void nvn_window_set_present_interval(void *window, int interval) {
    nvn_window_state *w;
    pthread_mutex_lock(&s_window_lock);
    w = window_find(window, 1);
    if (w) w->present_interval = (uint32_t)interval;
    pthread_mutex_unlock(&s_window_lock);
    NVN_VOID_RETURN();
}

void nvn_window_set_crop(void *window, int x, int y, int width, int height) {
    nvn_window_state *w;
    pthread_mutex_lock(&s_window_lock);
    w = window_find(window, 1);
    if (w) {
        w->crop_x = (uint32_t)x;
        w->crop_y = (uint32_t)y;
        w->crop_width = (uint32_t)width;
        w->crop_height = (uint32_t)height;
    }
    pthread_mutex_unlock(&s_window_lock);
    NVN_VOID_RETURN();
}

void nvn_window_get_crop(const void *window, int *out_x, int *out_y, int *out_width, int *out_height) {
    nvn_window_state *w;
    pthread_mutex_lock(&s_window_lock);
    w = window_find((void *)(uintptr_t)window, 0);
    if (w) {
        if (out_x) *out_x = (int)w->crop_x;
        if (out_y) *out_y = (int)w->crop_y;
        if (out_width) *out_width = (int)w->crop_width;
        if (out_height) *out_height = (int)w->crop_height;
    }
    pthread_mutex_unlock(&s_window_lock);
    NVN_VOID_RETURN();
}

void nvn_window_set_debug_label(void *window, const char *label) {
    nvn_window_state *w;
    pthread_mutex_lock(&s_window_lock);
    w = window_find(window, 1);
    if (w) snprintf(w->debug_label, sizeof(w->debug_label), "%s", label ? label : "");
    pthread_mutex_unlock(&s_window_lock);
    NVN_VOID_RETURN();
}

void nvn_window_record_present(void *window, int index, uint64_t fence) {
    nvn_window_state *w;
    if (index < 0 || index >= NVN_WINDOW_MAX_TEXTURES) return;
    pthread_mutex_lock(&s_window_lock);
    w = window_find(window, 0);
    if (w) {
        w->texture_fences[index] = fence;
    }
    pthread_mutex_unlock(&s_window_lock);
}

/* NVNwindowAcquireTextureResult (window, availableSync, int *textureIndex).
 * These indices address the guest's textures, not Vulkan swapchain images.
 * Presentation copies a completed guest image into the platform swapchain.
 * Retire prior work for the specific requested texture rather than serializing
 * the entire engine, enabling concurrent CPU recording and GPU rendering. */
int nvn_window_acquire_texture(void *window, void *available_sync, int *texture_index) {
    nvn_window_state *w;
    uint32_t count;
    int index;
    if (!texture_index || !nvn_engine_ready()) return 1;
    pthread_mutex_lock(&s_window_lock);
    w = window_find(window, 0);
    count = w ? (w->num_active_textures ? w->num_active_textures : w->num_textures) : 0;
    if (!w || !w->swapchain || !count || count > w->num_textures ||
        count > NVN_WINDOW_MAX_TEXTURES) {
        pthread_mutex_unlock(&s_window_lock);
        return 1; /* NVN_WINDOW_ACQUIRE_TEXTURE_RESULT_NATIVE_ERROR */
    }
    index = (int)(w->next_texture % count);
    if (!w->textures[index]) {
        pthread_mutex_unlock(&s_window_lock);
        return 1;
    }
    uint64_t prev_fence = w->texture_fences[index];
    pthread_mutex_unlock(&s_window_lock);

    /* If this texture was previously presented, wait only for that texture's fence;
     * otherwise wait for any prior submission to ensure initial readiness. */
    uint64_t wait_fence = prev_fence > 0 ? prev_fence : nvn_engine_last_fence();
    if (wait_fence > 0 && !nvn_engine_wait(wait_fence, UINT64_MAX)) return 1;

    pthread_mutex_lock(&s_window_lock);
    w = window_find(window, 0);
    if (!w) {
        pthread_mutex_unlock(&s_window_lock);
        return 1;
    }
    w->next_texture++;
    pthread_mutex_unlock(&s_window_lock);

    *texture_index = index;
    if (available_sync) nvn_sync_family_fence(available_sync);
    static unsigned s_acq_log = 0;
    if (s_acq_log++ < 20) {
        fprintf(stderr, "[nvn] nvnWindowAcquireTexture: window=%p sync=%p -> guest texture %d/%u (fence=%" PRIu64 ")\n",
                window, available_sync, index, count, prev_fence);
    }
    return 0; /* NVN_WINDOW_ACQUIRE_TEXTURE_RESULT_SUCCESS */
}

/* ------------------------------------------------------------------ */
/* The names                                                           */
/* ------------------------------------------------------------------ */

static const nvn_window_family_entry s_window_family[] = {
    { "nvnWindowBuilderSetDefaults", (void *)(uintptr_t)nvn_window_builder_set_defaults },
    { "nvnWindowBuilderSetDevice", (void *)(uintptr_t)nvn_window_builder_set_device },
    { "nvnWindowBuilderSetNativeWindow", (void *)(uintptr_t)nvn_window_builder_set_native_window },
    { "nvnWindowBuilderSetTextures", (void *)(uintptr_t)nvn_window_builder_set_textures },
    { "nvnWindowBuilderSetPresentInterval", (void *)(uintptr_t)nvn_window_builder_set_present_interval },
    { "nvnWindowBuilderSetNumActiveTextures", (void *)(uintptr_t)nvn_window_builder_set_num_active_textures },
    { "nvnWindowBuilderGetDevice", (void *)(uintptr_t)nvn_window_builder_get_device },
    { "nvnWindowBuilderGetNativeWindow", (void *)(uintptr_t)nvn_window_builder_get_native_window },
    { "nvnWindowBuilderGetFlags", (void *)(uintptr_t)nvn_window_builder_get_flags },
    { "nvnWindowBuilderGetNumTextures", (void *)(uintptr_t)nvn_window_builder_get_num_textures },
    { "nvnWindowBuilderGetNumActiveTextures", (void *)(uintptr_t)nvn_window_builder_get_num_active_textures },
    { "nvnWindowBuilderGetPresentInterval", (void *)(uintptr_t)nvn_window_builder_get_present_interval },
    { "nvnWindowBuilderGetTexture", (void *)(uintptr_t)nvn_window_builder_get_texture },
    { "nvnWindowInitialize", (void *)(uintptr_t)nvn_window_initialize },
    { "nvnWindowFinalize", (void *)(uintptr_t)nvn_window_finalize },
    { "nvnWindowGetNativeWindow", (void *)(uintptr_t)nvn_window_get_native_window },
    { "nvnWindowGetNumTextures", (void *)(uintptr_t)nvn_window_get_num_textures },
    { "nvnWindowGetNumActiveTextures", (void *)(uintptr_t)nvn_window_get_num_active_textures },
    { "nvnWindowGetPresentInterval", (void *)(uintptr_t)nvn_window_get_present_interval },
    { "nvnWindowSetNumActiveTextures", (void *)(uintptr_t)nvn_window_set_num_active_textures },
    { "nvnWindowSetPresentInterval", (void *)(uintptr_t)nvn_window_set_present_interval },
    { "nvnWindowSetCrop", (void *)(uintptr_t)nvn_window_set_crop },
    { "nvnWindowGetCrop", (void *)(uintptr_t)nvn_window_get_crop },
    { "nvnWindowSetDebugLabel", (void *)(uintptr_t)nvn_window_set_debug_label },
    { "nvnWindowAcquireTexture", (void *)(uintptr_t)nvn_window_acquire_texture },
};

size_t nvn_window_family_count(void) { return sizeof(s_window_family) / sizeof(s_window_family[0]); }

const nvn_window_family_entry *nvn_window_family_entry_at(size_t index) {
    if (index >= nvn_window_family_count()) return NULL;
    return &s_window_family[index];
}

void nvn_window_get_stats(nvn_window_stats *out) {
    size_t i;
    if (!out) return;
    pthread_mutex_lock(&s_window_lock);
    *out = s_window_stats;
    out->live_windows = 0;
    out->surface_present = 0;
    for (i = 0; i < NVN_WINDOW_MAX_STATES; i++) {
        if (s_windows[i].in_use) {
            out->live_windows++;
            if (s_windows[i].surface) out->surface_present = 1;
        }
    }
    pthread_mutex_unlock(&s_window_lock);
}

/* The present state of the guest's window.
 *
 * The crop is the part of the image the window shows; when the guest has
 * not set one, the whole image is shown. */
int nvn_window_present_state_of(const void *window, nvn_window_present_state *out) {
    nvn_window_state *slot;
    int i;
    if (!window || !out) return 0;
    pthread_mutex_lock(&s_window_lock);
    slot = window_find((void *)(uintptr_t)window, 0);
    if (!slot || !slot->in_use) {
        pthread_mutex_unlock(&s_window_lock);
        return 0;
    }
    memset(out, 0, sizeof(*out));
    out->native_window = slot->native_window;
    out->ntex = (int32_t)slot->num_textures;
    out->interval = (int32_t)slot->present_interval;
    out->crop[0] = (int32_t)slot->crop_x;
    out->crop[1] = (int32_t)slot->crop_y;
    out->crop[2] = (int32_t)slot->crop_width;
    out->crop[3] = (int32_t)slot->crop_height;
    for (i = 0; i < 4 && i < NVN_WINDOW_MAX_TEXTURES; i++) out->textures[i] = slot->textures[i];
    pthread_mutex_unlock(&s_window_lock);
    return 1;
}
