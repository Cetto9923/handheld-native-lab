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
/* The Android surface and the present bits only exist when the platform is
 * declared before the Vulkan header. */
#define VK_USE_PLATFORM_ANDROID_KHR
/* The header is needed for the ANativeWindow type of the surface info. Its
 * functions live in libandroid.so, and this layer deliberately does not call
 * them: the size of the window comes from the surface instead. */
#include <android/native_window.h>
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
    VkPhysicalDeviceFeatures features;         /* the features that were asked for */
    VkPhysicalDeviceFeatures enabled;         /* the features the device gave */
    uint32_t storage_image_write_supported;   /* the device can write a storage image without a format */
    uint32_t storage_image_write_enabled;     /* and it was enabled on this device */
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

    /* The surface extensions are not optional here: without them there is no
     * vkCreateAndroidSurfaceKHR, and therefore no window and no frame. This
     * is the same fact the host probe had to find the hard way. */
    static const char *const surface_extensions[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_ANDROID_SURFACE_EXTENSION_NAME,
    };
    memset(&instance_info, 0, sizeof(instance_info));
    instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_info.pApplicationInfo = &app;
    instance_info.enabledExtensionCount = 2;
    instance_info.ppEnabledExtensionNames = surface_extensions;

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

    /* The features of the device.
     *
     * Two of the compute shaders of this game write into a storage image
     * whose format the game never states: the format is a property of the
     * bound texture and not of the shader, so there is nothing in the game's
     * data to put into the module. The way the reference host that runs this
     * game solves it is the same way: the module declares the capability
     * StorageImageWriteWithoutFormat, and the device has to have the
     * matching feature turned on.
     *
     * Support and enablement are two different facts and the log keeps them
     * apart: a device that does not support the feature is not asked for it,
     * and a device that supports it but was not given it says so. */
    {
        VkPhysicalDeviceFeatures supported;
        memset(&supported, 0, sizeof(supported));
        vkGetPhysicalDeviceFeatures(st->physical, &supported);
        st->storage_image_write_supported = supported.shaderStorageImageWriteWithoutFormat ? 1 : 0;
        if (st->storage_image_write_supported) {
            st->features.shaderStorageImageWriteWithoutFormat = VK_TRUE;
        }
    }

    memset(&device_info, 0, sizeof(device_info));
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    device_info.pEnabledFeatures = &st->features;

    result = vkCreateDevice(st->physical, &device_info, NULL, &st->device);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "[nvn] vulkan: vkCreateDevice failed with %d\n", (int)result);
        vkDestroyInstance(st->instance, NULL);
        free(st);
        return 0;
    }
    /* What the device really has.
     *
     * The instance of this runtime is Vulkan 1.0, and that version has no
     * query for the features of a logical device: a device is created with
     * exactly the features that are asked for, and the creation fails with
     * VK_ERROR_FEATURE_NOT_PRESENT if one of them is not supported. The
     * creation above succeeded, so the features that were asked for are the
     * features the device has. The supported set is asked of the physical
     * device above, and the two numbers are logged apart so that "the device
     * can do it" and "the device does it" are never the same claim. */
    st->enabled = st->features;
    st->storage_image_write_enabled = st->enabled.shaderStorageImageWriteWithoutFormat ? 1 : 0;
    vkGetDeviceQueue(st->device, st->queue_family, 0, &st->queue);

    device->backend_state = st;
    snprintf(device->description, sizeof(device->description),
             "Vulkan device \"%s\", instance API %u.%u, driver %u, graphics queue family %u",
             st->gpu_name, VK_VERSION_MAJOR(st->api_version), VK_VERSION_MINOR(st->api_version),
             st->driver_version, st->queue_family);
    fprintf(stderr, "[nvn] vulkan: created %s\n", device->description);
    fprintf(stderr, "[nvn] vulkan: feature shaderStorageImageWriteWithoutFormat "
                    "supported=%u enabled=%u\n",
            st->storage_image_write_supported, st->storage_image_write_enabled);
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
             "Vulkan device \"%s\", instance API %u.%u, driver %u, graphics queue family %u, "
             "shaderStorageImageWriteWithoutFormat supported=%u enabled=%u",
             st->gpu_name, VK_VERSION_MAJOR(st->api_version), VK_VERSION_MINOR(st->api_version),
             st->driver_version, st->queue_family,
             st->storage_image_write_supported, st->storage_image_write_enabled);
}

static int nvn_vulkan_get_queue(nvn_device_state *device, void **out_queue) {
    nvn_vulkan_state *st = (nvn_vulkan_state *)device->backend_state;
    if (!st || !st->queue || !out_queue) return 0;
    /* This backend creates exactly one queue: the graphics queue of the
     * logical device. */
    *out_queue = (void *)(uintptr_t)st->queue;
    return 1;
}

static int nvn_vulkan_create_window_surface(nvn_device_state *device, void *native_window,
                                            uint32_t *out_width, uint32_t *out_height,
                                            void **out_surface, void **out_swapchain) {
    nvn_vulkan_state *st = (nvn_vulkan_state *)device->backend_state;
    VkAndroidSurfaceCreateInfoKHR surface_info;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSurfaceCapabilitiesKHR capabilities;
    VkSurfaceFormatKHR format;
    uint32_t format_count = 0;
    VkSurfaceFormatKHR formats[16];
    VkSwapchainCreateInfoKHR swapchain_info;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkResult result;
    uint32_t width, height;
    uint32_t image_count;
    VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
    PFN_vkCreateAndroidSurfaceKHR create_android_surface;

    if (!st || !native_window || !out_surface || !out_swapchain) return 0;

    create_android_surface =
        (PFN_vkCreateAndroidSurfaceKHR)vkGetInstanceProcAddr(st->instance, "vkCreateAndroidSurfaceKHR");
    if (!create_android_surface) {
        fprintf(stderr, "[nvn] vulkan: vkCreateAndroidSurfaceKHR is not available in this instance\n");
        return 0;
    }

    memset(&surface_info, 0, sizeof(surface_info));
    surface_info.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    surface_info.window = (ANativeWindow *)native_window;
    result = create_android_surface(st->instance, &surface_info, NULL, &surface);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "[nvn] vulkan: vkCreateAndroidSurfaceKHR failed with %d\n", (int)result);
        return 0;
    }

    /* Whether this queue family can present into this surface is asked from
     * the driver, because the queue family was chosen before any surface
     * existed. A family that cannot present is reported here instead of
     * failing later in a way that says nothing. */
    {
        PFN_vkGetPhysicalDeviceSurfaceSupportKHR surface_support =
            (PFN_vkGetPhysicalDeviceSurfaceSupportKHR)vkGetInstanceProcAddr(
                st->instance, "vkGetPhysicalDeviceSurfaceSupportKHR");
        VkBool32 supported = VK_FALSE;
        if (surface_support &&
            surface_support(st->physical, st->queue_family, surface, &supported) == VK_SUCCESS) {
            fprintf(stderr, "[nvn] vulkan: queue family %u can present into this surface: %s\n",
                    st->queue_family, supported ? "yes" : "no");
        }
    }

    result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(st->physical, surface, &capabilities);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "[nvn] vulkan: vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed with %d\n", (int)result);
        vkDestroySurfaceKHR(st->instance, surface, NULL);
        return 0;
    }

    /* The size of the platform window is what the guest draws into. The
     * surface reports the current size of the window it was made from, so
     * the size comes from the surface itself and not from a guess. The
     * capabilities say what the surface allows, so the size is clamped into
     * that range instead of being trusted blindly. */
    width = capabilities.currentExtent.width;
    height = capabilities.currentExtent.height;
    if (capabilities.currentExtent.width != 0xFFFFFFFFu) {
        if (width < capabilities.currentExtent.width) width = capabilities.currentExtent.width;
        if (height < capabilities.currentExtent.height) height = capabilities.currentExtent.height;
    }
    if (width > capabilities.maxImageExtent.width) width = capabilities.maxImageExtent.width;
    if (height > capabilities.maxImageExtent.height) height = capabilities.maxImageExtent.height;
    if (width < capabilities.minImageExtent.width) width = capabilities.minImageExtent.width;
    if (height < capabilities.minImageExtent.height) height = capabilities.minImageExtent.height;

    result = vkGetPhysicalDeviceSurfaceFormatsKHR(st->physical, surface, &format_count, NULL);
    if (result != VK_SUCCESS || format_count == 0) {
        fprintf(stderr, "[nvn] vulkan: the surface has no formats (result %d, count %u)\n",
                (int)result, format_count);
        vkDestroySurfaceKHR(st->instance, surface, NULL);
        return 0;
    }
    if (format_count > 16) format_count = 16;
    result = vkGetPhysicalDeviceSurfaceFormatsKHR(st->physical, surface, &format_count, formats);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "[nvn] vulkan: vkGetPhysicalDeviceSurfaceFormatsKHR failed with %d\n", (int)result);
        vkDestroySurfaceKHR(st->instance, surface, NULL);
        return 0;
    }
    /* The format the surface offers is used as it is. The host probe showed
     * that this display reads the buffer as RGBA, so a BGRA format would
     * swap the colours on screen. */
    format = formats[0];
    for (uint32_t i = 0; i < format_count; i++) {
        if (formats[i].format == VK_FORMAT_R8G8B8A8_UNORM) { format = formats[i]; break; }
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM && format.format != VK_FORMAT_R8G8B8A8_UNORM)
            format = formats[i];
    }

    image_count = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && image_count > capabilities.maxImageCount)
        image_count = capabilities.maxImageCount;

    memset(&swapchain_info, 0, sizeof(swapchain_info));
    swapchain_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchain_info.surface = surface;
    swapchain_info.minImageCount = image_count;
    swapchain_info.imageFormat = format.format;
    swapchain_info.imageColorSpace = format.colorSpace;
    swapchain_info.imageExtent.width = width;
    swapchain_info.imageExtent.height = height;
    swapchain_info.imageArrayLayers = 1;
    swapchain_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    swapchain_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchain_info.preTransform = capabilities.currentTransform;
    swapchain_info.compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    swapchain_info.presentMode = present_mode;
    swapchain_info.clipped = VK_TRUE;
    result = vkCreateSwapchainKHR(st->device, &swapchain_info, NULL, &swapchain);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "[nvn] vulkan: vkCreateSwapchainKHR failed with %d\n", (int)result);
        vkDestroySurfaceKHR(st->instance, surface, NULL);
        return 0;
    }

    fprintf(stderr, "[nvn] vulkan: created a surface and a swapchain for the platform window: "
                    "%ux%u, format %u, %u images, present mode FIFO\n",
            width, height, (unsigned)format.format, image_count);
    if (out_width) *out_width = width;
    if (out_height) *out_height = height;
    *out_surface = (void *)(uintptr_t)surface;
    *out_swapchain = (void *)(uintptr_t)swapchain;
    return 1;
}

static void nvn_vulkan_destroy_window_surface(nvn_device_state *device, void *surface, void *swapchain) {
    nvn_vulkan_state *st = (nvn_vulkan_state *)device->backend_state;
    if (!st) return;
    if (swapchain) vkDestroySwapchainKHR(st->device, (VkSwapchainKHR)(uintptr_t)swapchain, NULL);
    if (surface) vkDestroySurfaceKHR(st->instance, (VkSurfaceKHR)(uintptr_t)surface, NULL);
}

static const nvn_backend s_vulkan_backend = {
    "vulkan",
    nvn_vulkan_create,
    nvn_vulkan_destroy,
    nvn_vulkan_describe,
    nvn_vulkan_get_queue,
    nvn_vulkan_create_window_surface,
    nvn_vulkan_destroy_window_surface,
};

const nvn_backend *nvn_device_default_backend(void) { return &s_vulkan_backend; }
