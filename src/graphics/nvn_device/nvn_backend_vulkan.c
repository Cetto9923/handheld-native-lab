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
#include <vulkan/vulkan.h>

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
    uint32_t api_version;
    uint32_t driver_version;
    char gpu_name[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
    VkPhysicalDeviceFeatures features;         /* the features that were asked for */
    VkPhysicalDeviceFeatures enabled;         /* the features the device gave */
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
    VkRenderPass render_pass;
    VkCommandPool command_pool;
    uint32_t present_path_ready;

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

    /* The swapchain extension is a device extension, and it is not optional:
     * the swapchain of the window is created with it, and without it the
     * creation of the swapchain fails with VK_ERROR_EXTENSION_NOT_PRESENT.
     * The device is asked first, because "the device has it" and "the
     * device was given it" are two different facts, and a device that does
     * not have it is reported instead of being asked for it anyway. */
    {
        static const char *const swapchain_extension[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
        uint32_t extension_count = 0;
        VkExtensionProperties *available = NULL;
        uint32_t e;
        VkResult extension_result = vkEnumerateDeviceExtensionProperties(st->physical, NULL, &extension_count, NULL);
        if (extension_result == VK_SUCCESS && extension_count > 0) {
            available = (VkExtensionProperties *)calloc(extension_count, sizeof(*available));
            if (available) {
                extension_result = vkEnumerateDeviceExtensionProperties(st->physical, NULL, &extension_count, available);
                if (extension_result == VK_SUCCESS) {
                    for (e = 0; e < extension_count; e++) {
                        if (strcmp(available[e].extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
                            st->swapchain_supported = 1;
                            break;
                        }
                    }
                }
                free(available);
            }
        }
        if (st->swapchain_supported) {
            device_info.enabledExtensionCount = 1;
            device_info.ppEnabledExtensionNames = swapchain_extension;
            fprintf(stderr, "[nvn] vulkan: the device has %s and it is enabled on this device\n",
                    VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        } else {
            fprintf(stderr, "[nvn] vulkan: the device does not have %s, so this runtime cannot present\n",
                    VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        }
    }

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
    if (st->command_pool) {
        if (st->command_buffers) {
            vkFreeCommandBuffers(st->device, st->command_pool, st->image_count, st->command_buffers);
        }
        vkDestroyCommandPool(st->device, st->command_pool, NULL);
    }
    if (st->render_pass) vkDestroyRenderPass(st->device, st->render_pass, NULL);
    free(st->images);
    free(st->views);
    free(st->framebuffers);
    free(st->command_buffers);
    free(st->fences);
    st->images = NULL;
    st->views = NULL;
    st->framebuffers = NULL;
    st->command_buffers = NULL;
    st->fences = NULL;
    st->render_pass = VK_NULL_HANDLE;
    st->command_pool = VK_NULL_HANDLE;
    st->image_count = 0;
    st->present_path_ready = 0;
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
    if (!st->images || !st->views || !st->framebuffers || !st->command_buffers || !st->fences) {
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
        result = vkCreateFence(st->device, &fence_info, NULL, &st->fences[i]);
        if (result != VK_SUCCESS) {
            fprintf(stderr, "[nvn] vulkan: vkCreateFence failed with %d for image %u\n", (int)result, i);
            return 0;
        }
    }

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

    /* The present path of the backend: the images of the swapchain, one
     * command buffer and one fence per image, and the places where the views
     * and the framebuffers are kept for the frames that follow. */
    if (!nvn_vulkan_setup_present_path(st, surface, swapchain, format.format, format.colorSpace, width, height)) {
        fprintf(stderr, "[nvn] vulkan: the present path could not be set up, so there is nothing to present\n");
        vkDestroySwapchainKHR(st->device, swapchain, NULL);
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

    result = vkAcquireNextImageKHR(st->device, st->swapchain, NVN_PRESENT_ACQUIRE_TIMEOUT_NS,
                                   VK_NULL_HANDLE, VK_NULL_HANDLE, &index);
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
};

const nvn_backend *nvn_device_default_backend(void) { return &s_vulkan_backend; }
