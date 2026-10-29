#ifndef FFRS_NVN_VULKAN_FEATURES_H
#define FFRS_NVN_VULKAN_FEATURES_H

#include "nvn_device.h"
#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Vulkan-only seam: these are the features passed to a successful
 * vkCreateDevice, not a fresh query of physical-device support. The getter
 * refuses devices owned by any other backend. No generic backend ABI change. */
typedef struct nvn_vulkan_enabled_features {
    VkPhysicalDeviceFeatures core;
    VkBool32 shader_draw_parameters;
    uint32_t instance_api_version;
    uint32_t physical_api_version;
    VkBool32 shader_output_layer;
    VkBool32 shader_output_viewport_index;
} nvn_vulkan_enabled_features;

/* Vulkan12 feature structures require both APIs. No implicit enablement from
 * a newer physical device behind an older requested instance API. */
static inline int nvn_vulkan_layer_output_route(uint32_t instance_api,uint32_t physical_api,
    VkBool32 layer,VkBool32 viewport) {
    return instance_api>=VK_API_VERSION_1_2 && physical_api>=VK_API_VERSION_1_2 && layer && viewport;
}

int nvn_vulkan_get_enabled_features(const nvn_device_state *device,
                                    nvn_vulkan_enabled_features *out);

#ifdef __cplusplus
}
#endif
#endif
