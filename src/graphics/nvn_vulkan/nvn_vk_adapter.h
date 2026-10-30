/* The configuration of the ported Vulkan engine and the resource counters it reports.
 *
 * The names here are the ones the D2R project used, with the "dm" prefix (which named that
 * project) replaced by "xbg" (which names this engine). See nvn_vk_adapter.c for what the adapter
 * does with them.
 */
#ifndef FFRS_NVN_VULKAN_ADAPTER_H
#define FFRS_NVN_VULKAN_ADAPTER_H

#include <stddef.h>
#include <stdint.h>

#include "engine/xbg_game.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xbg_vk_config {
    uint32_t size;
    xbg_game_config game;
    VkBool32 sampler_mirror_clamp_to_edge;
    int display_hz;
    int (*has_window)(void *userdata);
    /* The refresh rate of the screen the window is on, in Hz. NULL means the value in display_hz
     * is used. */
    double (*display_refresh)(void *userdata);
} xbg_vk_config;

/* Create the engine's device and queue. The device, the queue and the window belong to the host;
 * the engine only uses them. The shader_directory must hold the genuine .xgs bundles that were
 * exported from the matching control blobs. */
int xbg_vk_initialize(const xbg_vk_config *config, char *error, size_t bytes);

/* What the engine holds. The counters the engine does not have are zero. */
typedef struct xbg_vk_res_count {
    uint32_t pso, cps, shaders, dss;
    uint32_t view_nonnull, view_slots, live_tex, sview_nonnull;
    uint32_t pools;
    uint64_t pool_adds;
    uint32_t c1_bufs;
    uint64_t c1_alloc, c1_data;
} xbg_vk_res_count;

#ifdef __cplusplus
}
#endif

#endif /* FFRS_NVN_VULKAN_ADAPTER_H */
