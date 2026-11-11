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
#include <time.h>
/* The Android surface and the present bits only exist when the platform is
 * declared before the Vulkan header. */
#define VK_USE_PLATFORM_ANDROID_KHR
/* The header is needed for the ANativeWindow type of the surface info. Its
 * functions live in libandroid.so, and this layer deliberately does not call
 * them: the size of the window comes from the surface instead. */
#include <android/native_window.h>
#include <unistd.h>
#include <vulkan/vulkan.h>
#include "native_window.h"
#include "nvn_vulkan_features.h"

/* How long the wait for the fence of a frame may take. The wait is bounded,
 * so a frame that never finishes is a named failure and not a hang. */
#define NVN_PRESENT_FENCE_TIMEOUT_NS 2000000000ull
/* How long the wait for a swapchain image may take. */
#define NVN_PRESENT_ACQUIRE_TIMEOUT_NS 1000000000ull

typedef struct {
    VkInstance instance;
    VkPhysicalDevice physical;
    VkDevice device;
    VkQueue queue;
    uint32_t queue_family;
    uint32_t queue_flags;
    uint32_t instance_api_version;
    uint32_t api_version;
    uint32_t driver_version;
    char gpu_name[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
    VkPhysicalDeviceFeatures features;         /* the features that were asked for */
    VkPhysicalDeviceFeatures enabled;         /* the features the device gave */
    VkBool32 shader_draw_parameters_enabled;
    VkBool32 shader_output_layer_supported,shader_output_viewport_index_supported;
    VkBool32 shader_output_layer_enabled,shader_output_viewport_index_enabled;
    uint32_t storage_image_write_supported;   /* the device can write a storage image without a format */
    uint32_t storage_image_write_enabled;     /* and it was enabled on this device */
    uint32_t swapchain_supported;             /* the device has VK_KHR_swapchain */

    /* The present path. The backend owns all of it: one surface, one
     * swapchain, the images of that swapchain, a view and a framebuffer per
     * image, one render pass, one command pool, one command buffer per image
     * and one fence per image. There is one present path per device; more
     * than one window is not supported. */
    VkSurfaceKHR surface;
    VkSwapchainKHR swapchain;
    uint64_t surface_window_generation;
    VkFormat surface_format;
    VkColorSpaceKHR color_space;
    uint32_t width;
    uint32_t height;
    uint32_t image_count;
    VkImage *images;
    VkImageView *views;
    VkFramebuffer *framebuffers;
    VkCommandBuffer *command_buffers;
    VkFence *fences;
    VkSemaphore *render_semaphores;
    VkSemaphore *acquire_semaphores;
    uint32_t acquire_sem_index;
    VkFence acquire_fence;
    VkRenderPass render_pass;
    VkCommandPool command_pool;
    uint32_t present_path_ready;
    uint8_t *images_cleared;

    /* What the present path did. The render pass and the framebuffers are
     * built once and reused, so the two counters tell which of the two
     * happened. */
    unsigned long frames;
    unsigned long images_acquired;
    unsigned long acquire_failures;
    unsigned long submit_failures;
    unsigned long present_failures;
    unsigned long fence_timeouts;
    unsigned long frames_submitted;
    unsigned long frames_presented;
    unsigned long render_passes_built;
    unsigned long render_passes_reused;
    unsigned long framebuffers_built;
    unsigned long framebuffers_reused;
    double last_fence_wait_ms;
    double max_fence_wait_ms;
    double total_fence_wait_ms;
} nvn_vulkan_state;

/* How long a reason may be. */
#define NVN_VULKAN_REASON_SIZE 256

/* The present path is released before the device goes away, and the release
 * is defined further down with the rest of the present path. */
static void nvn_vulkan_release_present_path(nvn_vulkan_state *st);
static const nvn_backend s_vulkan_backend;

int nvn_vulkan_get_enabled_features(const nvn_device_state *device,
                                    nvn_vulkan_enabled_features *out) {
    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    if (!device || device->backend != &s_vulkan_backend || !device->backend_state) return 0;
    const nvn_vulkan_state *st = (const nvn_vulkan_state *)device->backend_state;
    if (!st->device) return 0;
    out->core = st->enabled;
    out->shader_draw_parameters = st->shader_draw_parameters_enabled;
    out->instance_api_version = st->instance_api_version;
    out->physical_api_version = st->api_version;
    out->shader_output_layer = st->shader_output_layer_enabled;
    out->shader_output_viewport_index = st->shader_output_viewport_index_enabled;
    return 1;
}

static int nvn_vulkan_create(nvn_device_state *device, const void *builder) {
    nvn_vulkan_state *st;
    VkApplicationInfo app;
    VkInstanceCreateInfo instance_info;
    VkDeviceQueueCreateInfo queue_info;
    VkDeviceCreateInfo device_info;
    VkPhysicalDeviceShaderDrawParametersFeatures draw_parameters;
    VkPhysicalDeviceVulkan12Features supported12,requested12;
    int core12_route;
    VkPhysicalDevice *physicals = NULL;
    uint32_t physical_count = 0;
    uint32_t i;
    float priority = 1.0f;
    VkResult result;
    uint32_t instance_version = VK_API_VERSION_1_0;
    PFN_vkEnumerateInstanceVersion enumerate_instance_version =
        (PFN_vkEnumerateInstanceVersion)vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceVersion");

    if (!enumerate_instance_version ||
        enumerate_instance_version(&instance_version) != VK_SUCCESS ||
        instance_version < VK_API_VERSION_1_1) {
        fprintf(stderr, "[nvn] vulkan: Vulkan 1.1 instance support is required, loader reports %u.%u\n",
                VK_VERSION_MAJOR(instance_version), VK_VERSION_MINOR(instance_version));
        return 0;
    }

    (void)builder;
    memset(&app, 0, sizeof(app));
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "ffrs-rg477m-native";
    app.applicationVersion = 1;
    app.pEngineName = "ffrs-rg477m-native";
    app.engineVersion = 1;
    /* The engine uses Vulkan 1.1 and optional physical-device 1.3 queries.
     * Advertise the version actually supported by this loader, bounded by
     * the newest core version used here. Physical support is checked below. */
    app.apiVersion = instance_version < VK_API_VERSION_1_3 ? instance_version : VK_API_VERSION_1_3;

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
    st->instance_api_version = app.apiVersion;

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
        VkPhysicalDeviceProperties candidate_properties;
        uint32_t family_count = 0;
        VkQueueFamilyProperties *families = NULL;
        uint32_t f;
        int found = 0;
        vkGetPhysicalDeviceProperties(physicals[i], &candidate_properties);
        if (candidate_properties.apiVersion < VK_API_VERSION_1_1) continue;
        vkGetPhysicalDeviceQueueFamilyProperties(physicals[i], &family_count, NULL);
        if (family_count == 0) continue;
        families = (VkQueueFamilyProperties *)calloc(family_count, sizeof(*families));
        if (!families) continue;
        vkGetPhysicalDeviceQueueFamilyProperties(physicals[i], &family_count, families);
        for (f = 0; f < family_count; f++) {
            if ((families[f].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) ==
                (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) {
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
        fprintf(stderr, "[nvn] vulkan: none of the %u physical devices supports Vulkan 1.1 and a graphics/compute queue\n", physical_count);
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

    /* Query Vulkan12 only when BOTH the requested instance API and selected
     * physical-device API permit that core structure. Loader/physical support
     * cannot upgrade an already-created Vulkan1.1 instance. */
    core12_route = st->instance_api_version >= VK_API_VERSION_1_2 && st->api_version >= VK_API_VERSION_1_2;
    memset(&supported12, 0, sizeof(supported12));
    supported12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    memset(&requested12, 0, sizeof(requested12));
    requested12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;

    /* Enable the supported Vulkan 1.0 core set used by the replay engine.
     * Previously only storage-image writes were enabled, while the engine
     * was told the entire physical set was enabled. Keep the actual request
     * and publish it only after successful device creation. */
    {
        VkPhysicalDeviceFeatures2 supported;
        memset(&draw_parameters, 0, sizeof(draw_parameters));
        draw_parameters.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES;
        draw_parameters.pNext = core12_route ? &supported12 : NULL;
        memset(&supported, 0, sizeof(supported));
        supported.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        supported.pNext = &draw_parameters;
        vkGetPhysicalDeviceFeatures2(st->physical, &supported);
        st->features = supported.features;
        st->storage_image_write_supported = supported.features.shaderStorageImageWriteWithoutFormat ? 1 : 0;
        st->shader_output_layer_supported = supported12.shaderOutputLayer;
        st->shader_output_viewport_index_supported = supported12.shaderOutputViewportIndex;
    }
    /* Only the two proven output features are requested. Never forward all
     * supported Vulkan12 bits (descriptor indexing, memory model, etc.). */
    draw_parameters.pNext = core12_route ? &requested12 : NULL;

    memset(&device_info, 0, sizeof(device_info));
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.pNext = &draw_parameters;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    device_info.pEnabledFeatures = &st->features;

    /* Request only the two promoted features. The target does not advertise
     * VK_EXT_shader_viewport_index_layer and has multiViewport=false: original
     * legacy SPIR-V is rejected; the validated core ShaderLayer module is used. */
    if (nvn_vulkan_layer_output_route(st->instance_api_version,st->api_version,
            supported12.shaderOutputLayer,supported12.shaderOutputViewportIndex)) {
        requested12.shaderOutputLayer=VK_TRUE;
        requested12.shaderOutputViewportIndex=VK_TRUE;
    }
    /* Extension names remain alive through vkCreateDevice. */
    const char *device_extensions[1];
    uint32_t extension_count = 0;
    VkExtensionProperties *available = NULL;
    VkResult extension_result = vkEnumerateDeviceExtensionProperties(st->physical, NULL, &extension_count, NULL);
    if (extension_result == VK_SUCCESS && extension_count > 0) {
        available = (VkExtensionProperties *)calloc(extension_count, sizeof(*available));
        if (available) {
            extension_result = vkEnumerateDeviceExtensionProperties(st->physical, NULL, &extension_count, available);
            if (extension_result == VK_SUCCESS) for (uint32_t e=0;e<extension_count;e++)
                if (!strcmp(available[e].extensionName,VK_KHR_SWAPCHAIN_EXTENSION_NAME)) st->swapchain_supported=1;
            free(available);
        }
    }
    if (st->swapchain_supported) device_extensions[device_info.enabledExtensionCount++]=VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    device_info.ppEnabledExtensionNames=device_info.enabledExtensionCount?device_extensions:NULL;
    fprintf(stderr,"[nvn] vulkan: swapchain supported=%u requested=%u; Vulkan12 route=%u outputLayer supported=%u requested=%u outputViewportIndex supported=%u requested=%u\n",
        st->swapchain_supported,st->swapchain_supported,core12_route,supported12.shaderOutputLayer,requested12.shaderOutputLayer,
        supported12.shaderOutputViewportIndex,requested12.shaderOutputViewportIndex);

    result = vkCreateDevice(st->physical, &device_info, NULL, &st->device);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "[nvn] vulkan: vkCreateDevice failed with %d\n", (int)result);
        vkDestroyInstance(st->instance, NULL);
        free(st);
        return 0;
    }
    /* vkCreateDevice succeeded with precisely this feature request. There
     * is no logical-device feature query: retain the successful request. */
    st->enabled = st->features;
    st->shader_draw_parameters_enabled = draw_parameters.shaderDrawParameters;
    st->shader_output_layer_enabled = requested12.shaderOutputLayer;
    st->shader_output_viewport_index_enabled = requested12.shaderOutputViewportIndex;
    fprintf(stderr,"[nvn] vulkan: successful device output features: shaderOutputLayer=%u shaderOutputViewportIndex=%u legacyLayerEXT=0 instanceAPI=%u.%u physicalAPI=%u.%u\n",
        st->shader_output_layer_enabled,st->shader_output_viewport_index_enabled,
        VK_VERSION_MAJOR(st->instance_api_version),VK_VERSION_MINOR(st->instance_api_version),
        VK_VERSION_MAJOR(st->api_version),VK_VERSION_MINOR(st->api_version));
    st->storage_image_write_enabled = st->enabled.shaderStorageImageWriteWithoutFormat ? 1 : 0;
    vkGetDeviceQueue(st->device, st->queue_family, 0, &st->queue);

    device->backend_state = st;
    snprintf(device->description, sizeof(device->description),
             "Vulkan device \"%s\", instance API %u.%u, driver %u, graphics queue family %u",
             st->gpu_name, VK_VERSION_MAJOR(st->instance_api_version), VK_VERSION_MINOR(st->instance_api_version),
             st->driver_version, st->queue_family);
    fprintf(stderr, "[nvn] vulkan: created %s\n", device->description);
    fprintf(stderr, "[nvn] vulkan: feature shaderStorageImageWriteWithoutFormat "
                    "supported=%u enabled=%u\n",
            st->storage_image_write_supported, st->storage_image_write_enabled);
    fprintf(stderr, "[nvn] vulkan: successful device feature request: physical API=%u.%u.%u "
                    "shaderDrawParameters=%u geometryShader=%u tessellationShader=%u "
                    "samplerAnisotropy=%u independentBlend=%u multiDrawIndirect=%u\n",
            VK_VERSION_MAJOR(st->api_version), VK_VERSION_MINOR(st->api_version), VK_VERSION_PATCH(st->api_version),
            st->shader_draw_parameters_enabled, st->enabled.geometryShader, st->enabled.tessellationShader,
            st->enabled.samplerAnisotropy, st->enabled.independentBlend, st->enabled.multiDrawIndirect);
    return 1;
}

static void nvn_vulkan_destroy(nvn_device_state *device) {
    nvn_vulkan_state *st = (nvn_vulkan_state *)device->backend_state;
    if (!st) return;
    /* The present path belongs to the device, so it goes back before the
     * device itself. */
    nvn_vulkan_release_present_path(st);
    if (st->swapchain) vkDestroySwapchainKHR(st->device, st->swapchain, NULL);
    if (st->surface) vkDestroySurfaceKHR(st->instance, st->surface, NULL);
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

/* The present path.
 *
 * The surface, the swapchain, the images, the views, the framebuffers, the
 * render pass, the command pool, the command buffers and the fences are all
 * held by the backend. The render pass is built once and the framebuffers are
 * built once per image, so the second frame reuses them: a new clear colour
 * does not rebuild anything, because the colour is not part of the render
 * pass but part of the clear value of the begin info. */

/* Give back everything the present path holds. Safe to call twice and safe
 * to call when there is no present path. */
static void nvn_vulkan_release_present_path(nvn_vulkan_state *st) {
    uint32_t i;
    if (!st) return;
    if (st->framebuffers) {
        for (i = 0; i < st->image_count; i++) {
            if (st->framebuffers[i]) vkDestroyFramebuffer(st->device, st->framebuffers[i], NULL);
        }
    }
    if (st->views) {
        for (i = 0; i < st->image_count; i++) {
            if (st->views[i]) vkDestroyImageView(st->device, st->views[i], NULL);
        }
    }
    if (st->fences) {
        for (i = 0; i < st->image_count; i++) {
            if (st->fences[i]) vkDestroyFence(st->device, st->fences[i], NULL);
        }
    }
    if (st->render_semaphores) {
        for (i = 0; i < st->image_count; i++) {
            if (st->render_semaphores[i]) vkDestroySemaphore(st->device, st->render_semaphores[i], NULL);
        }
    }
    if (st->acquire_semaphores) {
        for (i = 0; i < st->image_count; i++) {
            if (st->acquire_semaphores[i]) vkDestroySemaphore(st->device, st->acquire_semaphores[i], NULL);
        }
        free(st->acquire_semaphores);
        st->acquire_semaphores = NULL;
    }
    if(st->acquire_fence){vkDestroyFence(st->device,st->acquire_fence,NULL);st->acquire_fence=VK_NULL_HANDLE;}
    if (st->command_pool) {
        /* Destroying the pool frees its allocated buffers. In a partial
         * setup the array can exist even if allocation never succeeded. */
        vkDestroyCommandPool(st->device, st->command_pool, NULL);
    }
    if (st->render_pass) vkDestroyRenderPass(st->device, st->render_pass, NULL);
    free(st->images);
    free(st->views);
    free(st->framebuffers);
    free(st->command_buffers);
    free(st->fences);
    free(st->render_semaphores);
    free(st->images_cleared);
    st->images = NULL;
    st->views = NULL;
    st->framebuffers = NULL;
    st->command_buffers = NULL;
    st->fences = NULL;
    st->render_semaphores = NULL;
    st->images_cleared = NULL;
    st->render_pass = VK_NULL_HANDLE;
    st->command_pool = VK_NULL_HANDLE;
    st->image_count = 0;
    st->present_path_ready = 0;
}

/* A failed setup has submitted no work, but may own partial allocations. */
static void nvn_vulkan_abort_present_setup(nvn_vulkan_state *st, VkSurfaceKHR surface,
                                          VkSwapchainKHR swapchain) {
    nvn_vulkan_release_present_path(st);
    if (swapchain) vkDestroySwapchainKHR(st->device, swapchain, NULL);
    if (surface) vkDestroySurfaceKHR(st->instance, surface, NULL);
    st->surface = VK_NULL_HANDLE;
    st->swapchain = VK_NULL_HANDLE;
}

/* Take the images of the swapchain, make one command buffer and one fence
 * per image, and keep them for the frames that follow. */
static int nvn_vulkan_setup_present_path(nvn_vulkan_state *st, VkSurfaceKHR surface, VkSwapchainKHR swapchain,
                                         VkFormat format, VkColorSpaceKHR color_space,
                                         uint32_t width, uint32_t height) {
    uint32_t image_count = 0;
    uint32_t filled = 0;
    VkCommandPoolCreateInfo pool_info;
    VkCommandBufferAllocateInfo allocate_info;
    VkResult result;
    uint32_t i;

    st->surface = surface;
    st->swapchain = swapchain;
    st->surface_format = format;
    st->color_space = color_space;
    st->width = width;
    st->height = height;

    result = vkGetSwapchainImagesKHR(st->device, swapchain, &image_count, NULL);
    if (result != VK_SUCCESS || image_count == 0) {
        fprintf(stderr, "[nvn] vulkan: vkGetSwapchainImagesKHR failed with %d (count %u)\n",
                (int)result, image_count);
        return 0;
    }

    st->image_count = image_count;
    st->images = (VkImage *)calloc(image_count, sizeof(VkImage));
    st->views = (VkImageView *)calloc(image_count, sizeof(VkImageView));
    st->framebuffers = (VkFramebuffer *)calloc(image_count, sizeof(VkFramebuffer));
    st->command_buffers = (VkCommandBuffer *)calloc(image_count, sizeof(VkCommandBuffer));
    st->fences = (VkFence *)calloc(image_count, sizeof(VkFence));
    st->render_semaphores = (VkSemaphore *)calloc(image_count, sizeof(VkSemaphore));
    st->acquire_semaphores = (VkSemaphore *)calloc(image_count, sizeof(VkSemaphore));
    st->acquire_sem_index = 0;
    st->images_cleared = (uint8_t *)calloc(image_count, sizeof(uint8_t));
    if (!st->images || !st->views || !st->framebuffers || !st->command_buffers || !st->fences || !st->render_semaphores || !st->acquire_semaphores || !st->images_cleared) {
        fprintf(stderr, "[nvn] vulkan: no memory for the %u images of the present path\n", image_count);
        return 0;
    }

    filled = image_count;
    result = vkGetSwapchainImagesKHR(st->device, swapchain, &filled, st->images);
    if (result != VK_SUCCESS || filled != image_count) {
        fprintf(stderr, "[nvn] vulkan: vkGetSwapchainImagesKHR failed with %d (wanted %u, got %u)\n",
                (int)result, image_count, filled);
        return 0;
    }

    memset(&pool_info, 0, sizeof(pool_info));
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    /* The buffers of this pool are reset before they are recorded into
     * again, so the pool has to allow that. */
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = st->queue_family;
    result = vkCreateCommandPool(st->device, &pool_info, NULL, &st->command_pool);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "[nvn] vulkan: vkCreateCommandPool failed with %d\n", (int)result);
        return 0;
    }

    memset(&allocate_info, 0, sizeof(allocate_info));
    allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate_info.commandPool = st->command_pool;
    allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate_info.commandBufferCount = image_count;
    result = vkAllocateCommandBuffers(st->device, &allocate_info, st->command_buffers);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "[nvn] vulkan: vkAllocateCommandBuffers failed with %d\n", (int)result);
        return 0;
    }

    for (i = 0; i < image_count; i++) {
        VkFenceCreateInfo fence_info;
        memset(&fence_info, 0, sizeof(fence_info));
        fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT; /* image command buffer initially idle */
        result = vkCreateFence(st->device, &fence_info, NULL, &st->fences[i]);
        if (result != VK_SUCCESS) {
            fprintf(stderr, "[nvn] vulkan: vkCreateFence failed with %d for image %u\n", (int)result, i);
            return 0;
        }

        VkSemaphoreCreateInfo sem_info;
        memset(&sem_info, 0, sizeof(sem_info));
        sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        result = vkCreateSemaphore(st->device, &sem_info, NULL, &st->render_semaphores[i]);
        if (result != VK_SUCCESS) {
            fprintf(stderr, "[nvn] vulkan: vkCreateSemaphore failed with %d for image %u\n", (int)result, i);
            return 0;
        }

        result = vkCreateSemaphore(st->device, &sem_info, NULL, &st->acquire_semaphores[i]);
        if (result != VK_SUCCESS) {
            fprintf(stderr, "[nvn] vulkan: vkCreateSemaphore failed with %d for acquire semaphore %u\n", (int)result, i);
            return 0;
        }
    }

    VkFenceCreateInfo acquire_info={0};
    acquire_info.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if(vkCreateFence(st->device,&acquire_info,NULL,&st->acquire_fence)!=VK_SUCCESS)return 0;
    st->present_path_ready = 1;
    fprintf(stderr, "[nvn] vulkan: the present path is ready: %u images, one command buffer and one fence per image\n",
            image_count);
    return 1;
}

/* The render pass of the present path. It is built once and reused for every
 * frame: the clear colour is not part of it. */
static int nvn_vulkan_ensure_render_pass(nvn_vulkan_state *st, char *reason, size_t reason_size) {
    VkAttachmentDescription attachment;
    VkAttachmentReference color_reference;
    VkSubpassDescription subpass;
    VkRenderPassCreateInfo create_info;
    VkResult result;

    if (st->render_pass != VK_NULL_HANDLE) {
        st->render_passes_reused++;
        return 1;
    }

    memset(&attachment, 0, sizeof(attachment));
    /* The format is the format the surface gave, not a format this file
     * picked. */
    attachment.format = st->surface_format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    memset(&color_reference, 0, sizeof(color_reference));
    color_reference.attachment = 0;
    color_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    memset(&subpass, 0, sizeof(subpass));
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_reference;

    memset(&create_info, 0, sizeof(create_info));
    create_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    create_info.attachmentCount = 1;
    create_info.pAttachments = &attachment;
    create_info.subpassCount = 1;
    create_info.pSubpasses = &subpass;

    result = vkCreateRenderPass(st->device, &create_info, NULL, &st->render_pass);
    if (result != VK_SUCCESS) {
        if (reason && reason_size) {
            snprintf(reason, reason_size, "vkCreateRenderPass failed with %d for format %u", (int)result,
                     (unsigned)st->surface_format);
        }
        fprintf(stderr, "[nvn] vulkan: vkCreateRenderPass failed with %d\n", (int)result);
        return 0;
    }
    st->render_passes_built++;
    fprintf(stderr, "[nvn] vulkan: the render pass is built for format %u and it is reused from now on\n",
            (unsigned)st->surface_format);
    return 1;
}

/* The view and the framebuffer of one swapchain image. They are built once
 * and reused afterwards. */
static int nvn_vulkan_ensure_framebuffer(nvn_vulkan_state *st, uint32_t index, char *reason, size_t reason_size) {
    VkImageViewCreateInfo view_info;
    VkFramebufferCreateInfo framebuffer_info;
    VkResult result;

    if (st->views[index] != VK_NULL_HANDLE && st->framebuffers[index] != VK_NULL_HANDLE) {
        st->framebuffers_reused++;
        return 1;
    }

    if (st->views[index] == VK_NULL_HANDLE) {
        memset(&view_info, 0, sizeof(view_info));
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = st->images[index];
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = st->surface_format;
        view_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.baseMipLevel = 0;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount = 1;
        result = vkCreateImageView(st->device, &view_info, NULL, &st->views[index]);
        if (result != VK_SUCCESS) {
            if (reason && reason_size) {
                snprintf(reason, reason_size, "vkCreateImageView failed with %d for image %u", (int)result, index);
            }
            fprintf(stderr, "[nvn] vulkan: vkCreateImageView failed with %d for image %u\n", (int)result, index);
            return 0;
        }
    }

    if (st->framebuffers[index] == VK_NULL_HANDLE) {
        memset(&framebuffer_info, 0, sizeof(framebuffer_info));
        framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer_info.renderPass = st->render_pass;
        framebuffer_info.attachmentCount = 1;
        framebuffer_info.pAttachments = &st->views[index];
        framebuffer_info.width = st->width;
        framebuffer_info.height = st->height;
        framebuffer_info.layers = 1;
        result = vkCreateFramebuffer(st->device, &framebuffer_info, NULL, &st->framebuffers[index]);
        if (result != VK_SUCCESS) {
            if (reason && reason_size) {
                snprintf(reason, reason_size, "vkCreateFramebuffer failed with %d for image %u", (int)result, index);
            }
            fprintf(stderr, "[nvn] vulkan: vkCreateFramebuffer failed with %d for image %u\n", (int)result, index);
            return 0;
        }
        st->framebuffers_built++;
    }
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

    /* A new window or a rebuilt surface starts a new present path, so the
     * old one is given back first. */
    nvn_vulkan_release_present_path(st);

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

    PFN_vkGetPhysicalDeviceSurfacePresentModesKHR get_surface_modes =
        (PFN_vkGetPhysicalDeviceSurfacePresentModesKHR)vkGetInstanceProcAddr(
            st->instance, "vkGetPhysicalDeviceSurfacePresentModesKHR");
    if (get_surface_modes) {
        uint32_t mode_count = 0;
        if (get_surface_modes(st->physical, surface, &mode_count, NULL) == VK_SUCCESS && mode_count > 0) {
            VkPresentModeKHR *modes = (VkPresentModeKHR *)calloc(mode_count, sizeof(VkPresentModeKHR));
            if (modes && get_surface_modes(st->physical, surface, &mode_count, modes) == VK_SUCCESS) {
                int has_mailbox = 0, has_relaxed = 0, has_immediate = 0;
                for (uint32_t i = 0; i < mode_count; i++) {
                    fprintf(stderr, "[nvn] vulkan: available present mode[%u] = %d\n", i, (int)modes[i]);
                    if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) has_mailbox = 1;
                    if (modes[i] == VK_PRESENT_MODE_FIFO_RELAXED_KHR) has_relaxed = 1;
                    if (modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR) has_immediate = 1;
                }
                if (has_mailbox) present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
                else if (has_relaxed) present_mode = VK_PRESENT_MODE_FIFO_RELAXED_KHR;
                else present_mode = VK_PRESENT_MODE_FIFO_KHR;

                const char *env_mode = getenv("FFRS_PRESENT_MODE");
                if (env_mode && !strcmp(env_mode, "mailbox") && has_mailbox) present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
                else if (env_mode && !strcmp(env_mode, "relaxed") && has_relaxed) present_mode = VK_PRESENT_MODE_FIFO_RELAXED_KHR;
                else if (env_mode && !strcmp(env_mode, "immediate") && has_immediate) present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
                else if (env_mode && !strcmp(env_mode, "fifo")) present_mode = VK_PRESENT_MODE_FIFO_KHR;

                FILE *pm_f = fopen("/storage/emulated/0/Android/data/org.ffrs.perfscale/files/gfx-present-mode", "rb");
                if (!pm_f) pm_f = fopen("/sdcard/Android/data/org.ffrs.perfscale/files/gfx-present-mode", "rb");
                if (!pm_f) pm_f = fopen("/storage/emulated/0/Android/data/org.ffrs.perfisolated/files/gfx-present-mode", "rb");
                if (!pm_f) pm_f = fopen("/sdcard/Android/data/org.ffrs.perfisolated/files/gfx-present-mode", "rb");
                if (!pm_f) pm_f = fopen("/storage/emulated/0/Android/data/org.ffrs.gamewindow/files/gfx-present-mode", "rb");
                if (!pm_f) pm_f = fopen("/sdcard/Android/data/org.ffrs.gamewindow/files/gfx-present-mode", "rb");
                if (pm_f) {
                    int c = fgetc(pm_f);
                    fclose(pm_f);
                    if (c == '0') present_mode = VK_PRESENT_MODE_FIFO_KHR;
                    else if (c == '1' && has_relaxed) present_mode = VK_PRESENT_MODE_FIFO_RELAXED_KHR;
                    else if (c == '2' && has_mailbox) present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
                    else if (c == '3' && has_immediate) present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
                }
                fprintf(stderr, "[nvn] vulkan: selected present mode = %d (mailbox=%d relaxed=%d)\n",
                        (int)present_mode, has_mailbox, has_relaxed);
            }
            free(modes);
        }
    }

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
    /* This blit writes in window orientation, without applying a device pre-rotation.
     * Ask the presentation engine to perform the transform when identity is supported. */
    const char *tablet_profile = getenv("FFRS_PROFILE_TABLET");
    swapchain_info.preTransform = (tablet_profile && tablet_profile[0] == '1' &&
                                  (capabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR))
        ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR : capabilities.currentTransform;
    fprintf(stderr, "[nvn] surface transform current=%#x supported=%#x requested=%#x\n",
            (unsigned)capabilities.currentTransform, (unsigned)capabilities.supportedTransforms,
            (unsigned)swapchain_info.preTransform);
    /* The presentation path copies the guest's own render target into the
     * swapchain with a blit, and that copy keeps the alpha channel of the
     * source. The game draws its final frame with alpha zero for the whole
     * 3D world and uses alpha only for the HUD pass, so a swapchain that
     * lets the compositor read that alpha shows a black world with only the
     * mini-map visible. OPAQUE tells the presentation engine to ignore the
     * alpha component and to treat the image as if its alpha were 1.0.
     *
     * The community port of this game (d2r-mobile, dm/android/vulkan_present.cpp,
     * revision f1e4b081af8ea65d95f0bf490a568cb657c3917e, MIT) creates its
     * swapchain with VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR for the same reason,
     * and falls back to the first mode the device reports when OPAQUE is not
     * offered. That fallback is kept here.
     *
     * Only the tablet profile changes. The handheld presents by rendering
     * straight into the swapchain image, so no blit carries an alpha channel
     * there, and its composite alpha stays exactly as it was. */
    if (tablet_profile && tablet_profile[0] == '1') {
        swapchain_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        if (!(capabilities.supportedCompositeAlpha & swapchain_info.compositeAlpha)) {
            for (uint32_t bit = 1; bit <= 8; bit <<= 1) {
                if (capabilities.supportedCompositeAlpha & bit) {
                    swapchain_info.compositeAlpha = (VkCompositeAlphaFlagBitsKHR)bit;
                    break;
                }
            }
        }
    } else {
        swapchain_info.compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    }
    fprintf(stderr, "[nvn] vulkan: composite alpha selected=%#x supported=%#x "
                    "(tablet=%d; opaque game layer ignores the alpha of the copied frame)\n",
            (unsigned)swapchain_info.compositeAlpha,
            (unsigned)capabilities.supportedCompositeAlpha,
            (tablet_profile && tablet_profile[0] == '1') ? 1 : 0);
    swapchain_info.presentMode = present_mode;
    swapchain_info.clipped = VK_TRUE;
    result = vkCreateSwapchainKHR(st->device, &swapchain_info, NULL, &swapchain);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "[nvn] vulkan: vkCreateSwapchainKHR failed with %d\n", (int)result);
        vkDestroySurfaceKHR(st->instance, surface, NULL);
        return 0;
    }

    /* The present path of the backend: the images of the swapchain, one
     * command buffer and one fence per image, and the places where the views
     * and the framebuffers are kept for the frames that follow. */
    if (!nvn_vulkan_setup_present_path(st, surface, swapchain, format.format, format.colorSpace, width, height)) {
        fprintf(stderr, "[nvn] vulkan: the present path could not be set up, so there is nothing to present\n");
        nvn_vulkan_abort_present_setup(st, surface, swapchain);
        return 0;
    }

    /* Callers create under the holder lease, so this generation belongs to
     * the window used above and cannot change until the lease is released. */
    st->surface_window_generation = nso_host_native_window_generation();

    fprintf(stderr, "[nvn] vulkan: created a surface and a swapchain for the platform window: "
                    "%ux%u, format %u, %u images, present mode %d\n",
            width, height, (unsigned)format.format, image_count, (int)present_mode);
    if (out_width) *out_width = width;
    if (out_height) *out_height = height;
    *out_surface = (void *)(uintptr_t)surface;
    *out_swapchain = (void *)(uintptr_t)swapchain;
    return 1;
}

typedef struct {
    nvn_device_state *device;
    int created;
} nvn_vulkan_live_create;

static void nvn_vulkan_create_live_callback(void *window, void *opaque) {
    nvn_vulkan_live_create *request = (nvn_vulkan_live_create *)opaque;
    uint32_t width = 0, height = 0;
    void *surface = NULL, *swapchain = NULL;
    request->created = nvn_vulkan_create_window_surface(request->device, window,
        &width, &height, &surface, &swapchain);
}

/* Protect the borrowed platform window until Vulkan acquires its own ref. */
static int nvn_vulkan_create_current_window_surface(nvn_device_state *device) {
    nvn_vulkan_live_create request = {device, 0};
    return nso_host_native_window_use(nvn_vulkan_create_live_callback, &request) && request.created;
}

static int nvn_vulkan_surface_window_changed(const nvn_vulkan_state *st) {
    return st->swapchain != VK_NULL_HANDLE &&
           st->surface_window_generation != nso_host_native_window_generation();
}

static void nvn_vulkan_destroy_window_surface(nvn_device_state *device, void *surface, void *swapchain) {
    nvn_vulkan_state *st = (nvn_vulkan_state *)device->backend_state;
    if (!st) return;
    /* A window may still retain handles from before an out-of-date rebuild.
     * Its later finalize must not destroy them again or clear the live path. */
    if ((surface && (VkSurfaceKHR)(uintptr_t)surface != st->surface) ||
        (swapchain && (VkSwapchainKHR)(uintptr_t)swapchain != st->swapchain)) {
        fprintf(stderr, "[nvn] vulkan: ignored stale surface teardown request\n");
        return;
    }
    if (st->device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(st->device);
    }
    /* The present path is given back before the swapchain and the surface
     * it belongs to. */
    nvn_vulkan_release_present_path(st);
    if (swapchain) vkDestroySwapchainKHR(st->device, (VkSwapchainKHR)(uintptr_t)swapchain, NULL);
    else if (st->swapchain) vkDestroySwapchainKHR(st->device, st->swapchain, NULL);
    if (surface) vkDestroySurfaceKHR(st->instance, (VkSurfaceKHR)(uintptr_t)surface, NULL);
    else if (st->surface) vkDestroySurfaceKHR(st->instance, st->surface, NULL);
    st->swapchain = VK_NULL_HANDLE;
    st->surface = VK_NULL_HANDLE;
}

/* Step one of the present path: take the next image of the swapchain.
 *
 * The wait is bounded, and the three ways it can go wrong are named apart:
 * the image did not come in time, the swapchain has to be rebuilt, and the
 * surface of the window is gone. */
/* A real WSI acquire completion fence is required. Image index availability
 * alone does not make presentation-engine reads complete. Serial host waits
 * are conservative here; no unsignalled/reused in-flight sync is published. */
static VkResult acquire_completed(nvn_vulkan_state *st,uint32_t *index,VkSemaphore *out_sem) {
    if (out_sem && st->acquire_semaphores) {
        uint32_t sem_idx = st->acquire_sem_index;
        VkSemaphore sem = st->acquire_semaphores[sem_idx];
        st->acquire_sem_index = (sem_idx + 1) % st->image_count;
        VkResult r = vkAcquireNextImageKHR(st->device, st->swapchain, NVN_PRESENT_ACQUIRE_TIMEOUT_NS,
                                           sem, VK_NULL_HANDLE, index);
        if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) return r;
        if (*index >= st->image_count) return VK_ERROR_INITIALIZATION_FAILED;
        VkResult acquired = r;
        r = vkWaitForFences(st->device, 1, &st->fences[*index], VK_TRUE, NVN_PRESENT_FENCE_TIMEOUT_NS);
        if (r != VK_SUCCESS) return r;
        *out_sem = sem;
        return acquired;
    }
    VkResult r=vkResetFences(st->device,1,&st->acquire_fence);
    if(r!=VK_SUCCESS)return r;
    r=vkAcquireNextImageKHR(st->device,st->swapchain,NVN_PRESENT_ACQUIRE_TIMEOUT_NS,VK_NULL_HANDLE,st->acquire_fence,index);
    if(r!=VK_SUCCESS && r!=VK_SUBOPTIMAL_KHR)return r;
    VkResult acquired=r;
    r=vkWaitForFences(st->device,1,&st->acquire_fence,VK_TRUE,NVN_PRESENT_FENCE_TIMEOUT_NS);
    if(r!=VK_SUCCESS)return r;
    if(*index>=st->image_count)return VK_ERROR_INITIALIZATION_FAILED;
    r=vkWaitForFences(st->device,1,&st->fences[*index],VK_TRUE,NVN_PRESENT_FENCE_TIMEOUT_NS);
    return r==VK_SUCCESS?acquired:r;
}

static int nvn_vulkan_acquire_image(nvn_device_state *device, uint32_t *out_index, void **out_image,
                                    char *reason, size_t reason_size) {
    nvn_vulkan_state *st = (nvn_vulkan_state *)device->backend_state;
    uint32_t index = 0;
    VkResult result;
    char detail[NVN_VULKAN_REASON_SIZE];

    if (!st || !out_index || !out_image) {
        if (reason && reason_size) {
            snprintf(reason, reason_size, "the device or the output of the acquire step is NULL");
        }
        fprintf(stderr, "[nvn] vulkan: the acquire step refused: the device or the output is NULL\n");
        return 0;
    }
    *out_index = 0;
    *out_image = NULL;

    if (!st->present_path_ready || st->swapchain == VK_NULL_HANDLE) {
        st->acquire_failures++;
        snprintf(detail, sizeof(detail), "there is no swapchain, so there is no image to take");
        if (reason && reason_size) snprintf(reason, reason_size, "%s", detail);
        fprintf(stderr, "[nvn] vulkan: the acquire step refused: %s\n", detail);
        return 0;
    }

    result = acquire_completed(st, &index, NULL);
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        st->acquire_failures++;
        switch (result) {
        case VK_TIMEOUT:
            snprintf(detail, sizeof(detail),
                     "vkAcquireNextImageKHR timed out after %llu ms",
                     (unsigned long long)(NVN_PRESENT_ACQUIRE_TIMEOUT_NS / 1000000ull));
            break;
        case VK_ERROR_OUT_OF_DATE_KHR:
            snprintf(detail, sizeof(detail), "the swapchain is out of date and has to be rebuilt");
            break;
        case VK_ERROR_SURFACE_LOST_KHR:
            snprintf(detail, sizeof(detail), "the surface of the window is lost");
            break;
        case VK_ERROR_DEVICE_LOST:
            snprintf(detail, sizeof(detail), "the Vulkan device is lost");
            break;
        default:
            snprintf(detail, sizeof(detail), "vkAcquireNextImageKHR failed with %d", (int)result);
            break;
        }
        if (reason && reason_size) snprintf(reason, reason_size, "%s", detail);
        fprintf(stderr, "[nvn] vulkan: the acquire step failed: %s\n", detail);
        return 0;
    }

    if (index >= st->image_count) {
        st->acquire_failures++;
        snprintf(detail, sizeof(detail), "the swapchain gave the image %u, which is not one of its %u images",
                 index, st->image_count);
        if (reason && reason_size) snprintf(reason, reason_size, "%s", detail);
        fprintf(stderr, "[nvn] vulkan: the acquire step failed: %s\n", detail);
        return 0;
    }

    st->images_acquired++;
    *out_index = index;
    *out_image = (void *)(uintptr_t)st->images[index];
    return 1;
}

/* The physical device, the device and the queue of this backend.
 *
 * The replay engine submits to the same queue as everything else, so it has
 * to be the same device and the same queue: an image that one device created
 * cannot be given to the queue of another one. */
static int nvn_vulkan_get_device_queue(nvn_device_state *device, void **out_physical, void **out_device,
                                       void **out_queue, uint32_t *out_family) {
    nvn_vulkan_state *st = (nvn_vulkan_state *)device->backend_state;
    if (!st || st->device == VK_NULL_HANDLE || st->queue == VK_NULL_HANDLE) return 0;
    if (out_physical) *out_physical = (void *)(uintptr_t)st->physical;
    if (out_device) *out_device = (void *)(uintptr_t)st->device;
    if (out_queue) *out_queue = (void *)(uintptr_t)st->queue;
    if (out_family) *out_family = st->queue_family;
    return 1;
}

/* Present an image that something else rendered.
 *
 * This is the path of the replay engine: the engine renders the game's own
 * render target into an image of its own and hands that image over here.
 * The image is copied into the swapchain and shown.
 *
 * The copy keeps the shape of the game: the crop is the part of the game's
 * image the window shows, and the image is scaled into the swapchain with
 * the shape it has, so that nothing of the game's picture is cut away. The
 * bars that are left over are cleared, so that they do not show the picture
 * of an earlier frame. */
static int nvn_vulkan_present_image(nvn_device_state *device, void *image, uint32_t format,
                                    uint32_t width, uint32_t height, const int32_t crop[4],
                                    int32_t interval, char *reason, size_t reason_size) {
    nvn_vulkan_state *st = (nvn_vulkan_state *)device->backend_state;
    VkResult result;
    VkCommandBuffer cmd;
    uint32_t index = 0;
    VkCommandBufferBeginInfo begin_info;
    VkImageMemoryBarrier barrier;
    VkImageBlit blit;
    VkSubmitInfo submit_info;
    VkPresentInfoKHR present_info;
    VkClearColorValue black;
    VkImageSubresourceRange range;
    double scale;
    int32_t source_x = 0, source_y = 0;
    double source_w = width ? width : 1, source_h = height ? height : 1;
    int32_t destination_x, destination_y, destination_w, destination_h;
    char detail[NVN_VULKAN_REASON_SIZE];

    (void)format;
    (void)interval;
    if (!st || !image) {
        if (reason && reason_size) snprintf(reason, reason_size, "device or image is NULL");
        return NVN_PRESENT_FAILED;
    }

    void *cur_win = nso_host_native_window_get();
    if (!cur_win) {
        /* App is in background or surface destroyed by Android lifecycle */
        if (st->swapchain != VK_NULL_HANDLE) {
            nvn_vulkan_destroy_window_surface(device, st->surface, st->swapchain);
        }
        usleep(16000);
        return NVN_PRESENT_SKIPPED; /* Drop frame gracefully while in background */
    }

    /* A replacement may happen entirely between frames, so observing a
     * non-null window does not mean the existing swapchain belongs to it. */
    if (nvn_vulkan_surface_window_changed(st)) {
        nvn_vulkan_destroy_window_surface(device, st->surface, st->swapchain);
    }

    if (!st->present_path_ready || st->swapchain == VK_NULL_HANDLE) {
        if (!nvn_vulkan_create_current_window_surface(device)) {
            usleep(16000);
            return NVN_PRESENT_SKIPPED; /* Wait for valid surface/swapchain */
        }
    }

    VkSemaphore acquire_sem = VK_NULL_HANDLE;
    result = acquire_completed(st, &index, &acquire_sem);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_ERROR_SURFACE_LOST_KHR) {
        nvn_vulkan_destroy_window_surface(device, st->surface, st->swapchain);
        nvn_vulkan_create_current_window_surface(device);
        usleep(16000);
        return NVN_PRESENT_SKIPPED; /* Frame dropped during swapchain rebuild */
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        snprintf(detail, sizeof(detail), "vkAcquireNextImageKHR non-fatal code: %d", (int)result);
        if (reason && reason_size) snprintf(reason, reason_size, "%s", detail);
        usleep(16000);
        return NVN_PRESENT_FAILED;
    }
    if (index >= st->image_count) {
        snprintf(detail, sizeof(detail), "the swapchain gave the image %u, which is not one of its %u images",
                 index, st->image_count);
        if (reason && reason_size) snprintf(reason, reason_size, "%s", detail);
        usleep(16000);
        return NVN_PRESENT_FAILED;
    }

    if (crop && crop[2] > 0 && crop[3] > 0) {
        source_x = crop[0];
        source_y = crop[1];
        source_w = (double)crop[2];
        source_h = (double)crop[3];
    }
    scale = (double)st->width / source_w;
    if ((double)st->height / source_h < scale) scale = (double)st->height / source_h;
    destination_w = (int32_t)(source_w * scale);
    destination_h = (int32_t)(source_h * scale);
    destination_x = (int32_t)(((double)st->width - destination_w) / 2.0);
    destination_y = (int32_t)(((double)st->height - destination_h) / 2.0);

    {
        static uint32_t s_present_log_count = 0;
        if (s_present_log_count < 5 || (s_present_log_count % 300 == 0 && s_present_log_count <= 1800)) {
            fprintf(stderr, "[nvn] vulkan present #%u: source=%.0fx%.0f crop=(%d,%d,%d,%d) -> dst=%dx%d at (%d,%d) on swapchain %ux%u (scale=%.4f)\n",
                    s_present_log_count, source_w, source_h,
                    crop ? crop[0] : 0, crop ? crop[1] : 0, crop ? crop[2] : 0, crop ? crop[3] : 0,
                    destination_w, destination_h, destination_x, destination_y,
                    st->width, st->height, scale);
        }
        s_present_log_count++;
    }

    cmd = st->command_buffers[index];
    vkResetCommandBuffer(cmd, 0);
    memset(&begin_info, 0, sizeof(begin_info));
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    result = vkBeginCommandBuffer(cmd, &begin_info);
    if (result != VK_SUCCESS) {
        snprintf(detail, sizeof(detail), "vkBeginCommandBuffer failed with %d", (int)result);
        if (reason && reason_size) snprintf(reason, reason_size, "%s", detail);
        fprintf(stderr, "[nvn] vulkan: the present step failed: %s\n", detail);
        return NVN_PRESENT_FAILED;
    }

    /* The swapchain image is the destination of the copy, so it has to be a
     * destination first. */
    memset(&barrier, 0, sizeof(barrier));
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = (st->images_cleared && st->images_cleared[index]) ?
                        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.image = st->images[index];
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL,
                         0, NULL, 1, &barrier);

    /* The letterbox bars around the picture only need to be cleared once per
     * swapchain image. Because the blit overwrites the destination rectangle on
     * every frame, the bars stay black. Clearing the whole image every frame caused
     * frame-to-frame flicker and brightness fluctuations on Mali TBDR. */
    if (st->images_cleared && !st->images_cleared[index]) {
        if (destination_x > 0 || destination_y > 0 ||
            destination_w < (int32_t)st->width || destination_h < (int32_t)st->height) {
            memset(&black, 0, sizeof(black));
            memset(&range, 0, sizeof(range));
            range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            range.levelCount = 1;
            range.layerCount = 1;
            vkCmdClearColorImage(cmd, st->images[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &range);
            barrier.oldLayout = barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.srcAccessMask = barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 0, 0, NULL, 0, NULL, 1, &barrier);
        }
        st->images_cleared[index] = 1;
    }

    memset(&blit, 0, sizeof(blit));
    blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.srcSubresource.layerCount = 1;
    blit.srcOffsets[0].x = source_x;
    blit.srcOffsets[0].y = source_y;
    blit.srcOffsets[1].x = source_x + (int32_t)source_w;
    blit.srcOffsets[1].y = source_y + (int32_t)source_h;
    blit.srcOffsets[1].z = 1; /* a 2D blit has unit Z extent */
    blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.dstSubresource.layerCount = 1;
    blit.dstOffsets[0].x = destination_x;
    blit.dstOffsets[0].y = destination_y;
    blit.dstOffsets[1].x = destination_x + destination_w;
    blit.dstOffsets[1].y = destination_y + destination_h;
    blit.dstOffsets[1].z = 1;
    vkCmdBlitImage(cmd, (VkImage)(uintptr_t)image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, st->images[index],
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

    memset(&barrier, 0, sizeof(barrier));
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = st->images[index];
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL,
                         0, NULL, 1, &barrier);

    result = vkEndCommandBuffer(cmd);
    if (result != VK_SUCCESS) {
        st->submit_failures++;
        snprintf(detail, sizeof(detail), "vkEndCommandBuffer failed with %d", (int)result);
        if (reason && reason_size) snprintf(reason, reason_size, "%s", detail);
        fprintf(stderr, "[nvn] vulkan: the present step failed: %s\n", detail);
        return NVN_PRESENT_FAILED;
    }

    vkResetFences(st->device, 1, &st->fences[index]);
    memset(&submit_info, 0, sizeof(submit_info));
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &cmd;
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
    if (acquire_sem != VK_NULL_HANDLE) {
        submit_info.waitSemaphoreCount = 1;
        submit_info.pWaitSemaphores = &acquire_sem;
        submit_info.pWaitDstStageMask = &wait_stage;
    }
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &st->render_semaphores[index];
    result = vkQueueSubmit(st->queue, 1, &submit_info, st->fences[index]);
    if (result != VK_SUCCESS) {
        st->submit_failures++;
        snprintf(detail, sizeof(detail), "vkQueueSubmit failed with %d", (int)result);
        if (reason && reason_size) snprintf(reason, reason_size, "%s", detail);
        fprintf(stderr, "[nvn] vulkan: the present step failed: %s\n", detail);
        return NVN_PRESENT_FAILED;
    }

    memset(&present_info, 0, sizeof(present_info));
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &st->render_semaphores[index];
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &st->swapchain;
    present_info.pImageIndices = &index;
    result = vkQueuePresentKHR(st->queue, &present_info);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_ERROR_SURFACE_LOST_KHR) {
        nvn_vulkan_destroy_window_surface(device, st->surface, st->swapchain);
        return NVN_PRESENT_SKIPPED;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        st->present_failures++;
        snprintf(detail, sizeof(detail), "vkQueuePresentKHR failed with %d", (int)result);
        if (reason && reason_size) snprintf(reason, reason_size, "%s", detail);
        fprintf(stderr, "[nvn] vulkan: the present step warning: %s\n", detail);
        return NVN_PRESENT_FAILED;
    }
    st->frames_presented++;
    return NVN_PRESENT_QUEUED;
}

/* Step two of the present path: open the render pass on the image and clear
 * it with the colour of the caller.
 *
 * The render pass and the framebuffer of the image are built when they are
 * not there yet and reused afterwards, so the second frame does not build
 * anything again. The colour is not part of the render pass: it goes into the
 * clear value of the begin info, so a new colour needs no new objects. */
static int nvn_vulkan_begin_render_pass(nvn_device_state *device, uint32_t index, void *image,
                                        const float rgba[4], char *reason, size_t reason_size) {
    nvn_vulkan_state *st = (nvn_vulkan_state *)device->backend_state;
    VkCommandBufferBeginInfo begin_info;
    VkRenderPassBeginInfo render_pass_info;
    VkClearValue clear_value;
    VkResult result;
    char detail[NVN_VULKAN_REASON_SIZE];

    if (!st || !rgba) {
        if (reason && reason_size) {
            snprintf(reason, reason_size, "the device or the clear colour of the render pass is NULL");
        }
        fprintf(stderr, "[nvn] vulkan: the render pass step refused: the device or the colour is NULL\n");
        return 0;
    }
    if (!st->present_path_ready || index >= st->image_count) {
        snprintf(detail, sizeof(detail), "the image index %u is not one of the %u images of the swapchain",
                 index, st->image_count);
        if (reason && reason_size) snprintf(reason, reason_size, "%s", detail);
        fprintf(stderr, "[nvn] vulkan: the render pass step refused: %s\n", detail);
        return 0;
    }
    if (image != (void *)(uintptr_t)st->images[index]) {
        snprintf(detail, sizeof(detail), "the image that was given is not the image of index %u of the swapchain",
                 index);
        if (reason && reason_size) snprintf(reason, reason_size, "%s", detail);
        fprintf(stderr, "[nvn] vulkan: the render pass step refused: %s\n", detail);
        return 0;
    }

    if (!nvn_vulkan_ensure_render_pass(st, detail, sizeof(detail))) {
        if (reason && reason_size) snprintf(reason, reason_size, "%s", detail);
        return 0;
    }
    if (!nvn_vulkan_ensure_framebuffer(st, index, detail, sizeof(detail))) {
        if (reason && reason_size) snprintf(reason, reason_size, "%s", detail);
        return 0;
    }

    vkResetCommandBuffer(st->command_buffers[index], 0);
    memset(&begin_info, 0, sizeof(begin_info));
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    result = vkBeginCommandBuffer(st->command_buffers[index], &begin_info);
    if (result != VK_SUCCESS) {
        snprintf(detail, sizeof(detail), "vkBeginCommandBuffer failed with %d for image %u", (int)result, index);
        if (reason && reason_size) snprintf(reason, reason_size, "%s", detail);
        fprintf(stderr, "[nvn] vulkan: the render pass step failed: %s\n", detail);
        return 0;
    }

    /* The colour of the caller, copied bit for bit. Nothing in this file
     * picks a colour of its own. */
    memset(&clear_value, 0, sizeof(clear_value));
    clear_value.color.float32[0] = rgba[0];
    clear_value.color.float32[1] = rgba[1];
    clear_value.color.float32[2] = rgba[2];
    clear_value.color.float32[3] = rgba[3];

    memset(&render_pass_info, 0, sizeof(render_pass_info));
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_info.renderPass = st->render_pass;
    render_pass_info.framebuffer = st->framebuffers[index];
    render_pass_info.renderArea.offset.x = 0;
    render_pass_info.renderArea.offset.y = 0;
    render_pass_info.renderArea.extent.width = st->width;
    render_pass_info.renderArea.extent.height = st->height;
    render_pass_info.clearValueCount = 1;
    render_pass_info.pClearValues = &clear_value;

    vkCmdBeginRenderPass(st->command_buffers[index], &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
    st->frames++;
    return 1;
}

/* Step three of the present path: end the render pass, submit the command
 * buffer, present the image and wait for the fence.
 *
 * The wait for the fence is bounded and the time it took is measured with
 * the monotonic clock, so the log says how long the frame took and a frame
 * that never finishes is a named failure and not a hang. */
static int nvn_vulkan_submit_and_present(nvn_device_state *device, uint32_t index, void *image,
                                          char *reason, size_t reason_size) {
    nvn_vulkan_state *st = (nvn_vulkan_state *)device->backend_state;
    VkCommandBuffer command_buffer;
    VkSubmitInfo submit_info;
    VkPresentInfoKHR present_info;
    VkResult result;
    struct timespec start;
    struct timespec end;
    double elapsed_ms;
    char detail[NVN_VULKAN_REASON_SIZE];

    (void)image;
    if (!st || !st->present_path_ready || index >= st->image_count) {
        snprintf(detail, sizeof(detail), "the image index %u is not one of the %u images of the swapchain",
                 index, st->image_count);
        if (reason && reason_size) snprintf(reason, reason_size, "%s", detail);
        fprintf(stderr, "[nvn] vulkan: the submit step refused: %s\n", detail);
        return 0;
    }

    command_buffer = st->command_buffers[index];
    vkCmdEndRenderPass(command_buffer);
    result = vkEndCommandBuffer(command_buffer);
    if (result != VK_SUCCESS) {
        st->submit_failures++;
        snprintf(detail, sizeof(detail), "vkEndCommandBuffer failed with %d for image %u", (int)result, index);
        if (reason && reason_size) snprintf(reason, reason_size, "%s", detail);
        fprintf(stderr, "[nvn] vulkan: the submit step failed: %s\n", detail);
        return 0;
    }

    vkResetFences(st->device, 1, &st->fences[index]);
    memset(&submit_info, 0, sizeof(submit_info));
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;
    result = vkQueueSubmit(st->queue, 1, &submit_info, st->fences[index]);
    if (result != VK_SUCCESS) {
        st->submit_failures++;
        snprintf(detail, sizeof(detail), "vkQueueSubmit failed with %d for image %u", (int)result, index);
        if (reason && reason_size) snprintf(reason, reason_size, "%s", detail);
        fprintf(stderr, "[nvn] vulkan: the submit step failed: %s\n", detail);
        return 0;
    }
    st->frames_submitted++;

    memset(&present_info, 0, sizeof(present_info));
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &st->swapchain;
    present_info.pImageIndices = &index;
    result = vkQueuePresentKHR(st->queue, &present_info);
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        st->present_failures++;
        snprintf(detail, sizeof(detail), "vkQueuePresentKHR failed with %d for image %u", (int)result, index);
        if (reason && reason_size) snprintf(reason, reason_size, "%s", detail);
        fprintf(stderr, "[nvn] vulkan: the present step failed: %s\n", detail);
        return 0;
    }
    st->frames_presented++;

    clock_gettime(CLOCK_MONOTONIC, &start);
    result = vkWaitForFences(st->device, 1, &st->fences[index], VK_TRUE, NVN_PRESENT_FENCE_TIMEOUT_NS);
    clock_gettime(CLOCK_MONOTONIC, &end);
    elapsed_ms = (double)(end.tv_sec - start.tv_sec) * 1000.0 +
                 (double)(end.tv_nsec - start.tv_nsec) / 1000000.0;
    st->last_fence_wait_ms = elapsed_ms;
    st->total_fence_wait_ms += elapsed_ms;
    if (elapsed_ms > st->max_fence_wait_ms) st->max_fence_wait_ms = elapsed_ms;

    if (result != VK_SUCCESS) {
        if (result == VK_TIMEOUT) {
            st->fence_timeouts++;
            snprintf(detail, sizeof(detail), "the fence of image %u did not signal within %llu ms", index,
                     (unsigned long long)(NVN_PRESENT_FENCE_TIMEOUT_NS / 1000000ull));
        } else {
            snprintf(detail, sizeof(detail), "vkWaitForFences failed with %d for image %u", (int)result, index);
        }
        if (reason && reason_size) snprintf(reason, reason_size, "%s", detail);
        fprintf(stderr, "[nvn] vulkan: the submit step failed: %s\n", detail);
        return 0;
    }

    /* The log of the wait. Every frame would be too much, so the first frame
     * and then every hundredth frame is written out, and every failure is
     * written out too. */
    if (st->frames == 1 || (st->frames % 100) == 0) {
        fprintf(stderr, "[nvn] vulkan: frame %lu presented, the fence took %.3f ms "
                        "(worst %.3f ms, total %.3f ms), render passes built %lu reused %lu, "
                        "framebuffers built %lu reused %lu\n",
                st->frames, elapsed_ms, st->max_fence_wait_ms, st->total_fence_wait_ms,
                st->render_passes_built, st->render_passes_reused,
                st->framebuffers_built, st->framebuffers_reused);
    }
    return 1;
}

static const nvn_backend s_vulkan_backend = {
    "vulkan",
    nvn_vulkan_create,
    nvn_vulkan_destroy,
    nvn_vulkan_describe,
    nvn_vulkan_get_queue,
    nvn_vulkan_create_window_surface,
    nvn_vulkan_destroy_window_surface,
    nvn_vulkan_acquire_image,
    nvn_vulkan_begin_render_pass,
    nvn_vulkan_submit_and_present,
    nvn_vulkan_get_device_queue,
    nvn_vulkan_present_image,
};

const nvn_backend *nvn_device_default_backend(void) { return &s_vulkan_backend; }
