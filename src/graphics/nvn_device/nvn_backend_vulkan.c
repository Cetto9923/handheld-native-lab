/* The NVN device backend for a build that has Vulkan.
 *
 * This is the Android side of the runtime. The RG477M has
 * /system/lib64/libvulkan.so (Mali, Vulkan 1.x), and the Vulkan probe in
 * src/graphics/vulkan_probe/ already creates an instance, a device and a
 * queue there, so the Vulkan side of the device is the part that is already
 * de-risked; what this file adds is the NVN device on top of it.
 *
 * What it does:
 *
 *   - creates a Vulkan instance and a logical device with one graphics queue;
 *   - reports success only when that really happened, and says which device
 *     it got, so the log never claims a device that does not exist;
 *   - hands the device back to the NVN layer, which is what the guest sees.
 *
 * What it does not do: no swapchain and no surface yet. The window arrives
 * from the nn::vi bridge, and the NVN window family is not implemented, so
 * there is nothing to present into. That is a later step, and until it lands
 * this backend creates a device and no frame.
 */
#include "nvn_device.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

typedef struct {
    VkInstance instance;
    VkPhysicalDevice physical;
    VkDevice device;
    VkQueue queue;
    uint32_t queue_family;
    uint32_t queue_flags;
    uint32_t api_version;
    uint32_t driver_version;
    char gpu_name[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
} nvn_vulkan_state;

static int nvn_vulkan_create(nvn_device_state *device, const void *builder) {
    nvn_vulkan_state *st;
    VkApplicationInfo app;
    VkInstanceCreateInfo instance_info;
    VkDeviceQueueCreateInfo queue_info;
    VkDeviceCreateInfo device_info;
    VkPhysicalDevice *physicals = NULL;
    uint32_t physical_count = 0;
    uint32_t i;
    float priority = 1.0f;
    VkResult result;

    (void)builder;
    memset(&app, 0, sizeof(app));
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "ffrs-rg477m-native";
    app.applicationVersion = 1;
    app.pEngineName = "ffrs-rg477m-native";
    app.engineVersion = 1;
    app.apiVersion = VK_API_VERSION_1_0;

    memset(&instance_info, 0, sizeof(instance_info));
    instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_info.pApplicationInfo = &app;

    st = (nvn_vulkan_state *)calloc(1, sizeof(*st));
    if (!st) {
        fprintf(stderr, "[nvn] vulkan: no memory for the device state\n");
        return 0;
    }

    result = vkCreateInstance(&instance_info, NULL, &st->instance);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "[nvn] vulkan: vkCreateInstance failed with %d\n", (int)result);
        free(st);
        return 0;
    }

    result = vkEnumeratePhysicalDevices(st->instance, &physical_count, NULL);
    if (result != VK_SUCCESS || physical_count == 0) {
        fprintf(stderr, "[nvn] vulkan: no physical device (result %d, count %u)\n", (int)result, physical_count);
        vkDestroyInstance(st->instance, NULL);
        free(st);
        return 0;
    }
    physicals = (VkPhysicalDevice *)calloc(physical_count, sizeof(*physicals));
    if (!physicals) {
        fprintf(stderr, "[nvn] vulkan: no memory for the physical device list\n");
        vkDestroyInstance(st->instance, NULL);
        free(st);
        return 0;
    }
    result = vkEnumeratePhysicalDevices(st->instance, &physical_count, physicals);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "[nvn] vulkan: vkEnumeratePhysicalDevices failed with %d\n", (int)result);
        free(physicals);
        vkDestroyInstance(st->instance, NULL);
        free(st);
        return 0;
    }

    for (i = 0; i < physical_count; i++) {
        uint32_t family_count = 0;
        VkQueueFamilyProperties *families = NULL;
        uint32_t f;
        int found = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physicals[i], &family_count, NULL);
        if (family_count == 0) continue;
        families = (VkQueueFamilyProperties *)calloc(family_count, sizeof(*families));
        if (!families) continue;
        vkGetPhysicalDeviceQueueFamilyProperties(physicals[i], &family_count, families);
        for (f = 0; f < family_count; f++) {
            if (families[f].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                st->physical = physicals[i];
                st->queue_family = f;
                st->queue_flags = families[f].queueFlags;
                found = 1;
                break;
            }
        }
        free(families);
        if (found) break;
    }
    free(physicals);
    if (!st->physical) {
        fprintf(stderr, "[nvn] vulkan: none of the %u physical devices has a graphics queue\n", physical_count);
        vkDestroyInstance(st->instance, NULL);
        free(st);
        return 0;
    }

    {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(st->physical, &props);
        snprintf(st->gpu_name, sizeof(st->gpu_name), "%s", props.deviceName);
        st->api_version = props.apiVersion;
        st->driver_version = props.driverVersion;
    }

    memset(&queue_info, 0, sizeof(queue_info));
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = st->queue_family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;

    memset(&device_info, 0, sizeof(device_info));
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;

    result = vkCreateDevice(st->physical, &device_info, NULL, &st->device);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "[nvn] vulkan: vkCreateDevice failed with %d\n", (int)result);
        vkDestroyInstance(st->instance, NULL);
        free(st);
        return 0;
    }
    vkGetDeviceQueue(st->device, st->queue_family, 0, &st->queue);

    device->backend_state = st;
    snprintf(device->description, sizeof(device->description),
             "Vulkan device \"%s\", instance API %u.%u, driver %u, graphics queue family %u",
             st->gpu_name, VK_VERSION_MAJOR(st->api_version), VK_VERSION_MINOR(st->api_version),
             st->driver_version, st->queue_family);
    fprintf(stderr, "[nvn] vulkan: created %s\n", device->description);
    return 1;
}

static void nvn_vulkan_destroy(nvn_device_state *device) {
    nvn_vulkan_state *st = (nvn_vulkan_state *)device->backend_state;
    if (!st) return;
    if (st->device) vkDestroyDevice(st->device, NULL);
    if (st->instance) vkDestroyInstance(st->instance, NULL);
    free(st);
    device->backend_state = NULL;
}

static void nvn_vulkan_describe(const nvn_device_state *device, char *out, size_t out_size) {
    const nvn_vulkan_state *st = (const nvn_vulkan_state *)device->backend_state;
    if (!st) {
        snprintf(out, out_size, "no Vulkan device");
        return;
    }
    snprintf(out, out_size,
             "Vulkan device \"%s\", instance API %u.%u, driver %u, graphics queue family %u",
             st->gpu_name, VK_VERSION_MAJOR(st->api_version), VK_VERSION_MINOR(st->api_version),
             st->driver_version, st->queue_family);
}

static int nvn_vulkan_get_queue(nvn_device_state *device, void **out_queue) {
    nvn_vulkan_state *st = (nvn_vulkan_state *)device->backend_state;
    if (!st || !st->queue || !out_queue) return 0;
    /* This backend creates exactly one queue: the graphics queue of the
     * logical device. */
    *out_queue = (void *)(uintptr_t)st->queue;
    return 1;
}

static const nvn_backend s_vulkan_backend = {
    "vulkan",
    nvn_vulkan_create,
    nvn_vulkan_destroy,
    nvn_vulkan_describe,
    nvn_vulkan_get_queue,
};

const nvn_backend *nvn_device_default_backend(void) { return &s_vulkan_backend; }
