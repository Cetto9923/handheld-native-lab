// The Android presentation host probe.
//
// What this proves, on the real device: a Vulkan surface can be created from
// the Activity's own ANativeWindow, a swapchain can be created on that
// surface, an image can be cleared, the clear can be submitted, completed
// under a real VkFence and presented, and the colour reaches the screen.
//
// What this does NOT prove: that the game draws anything. Nothing in this
// file comes from the guest; the colour is this probe's own. A success here
// is a host probe success and is never a game frame.
//
// The window comes from the runtime side holder (src/runtime/hle/native_window.c)
// that the SurfaceHolder callback publishes, so this file runs inside the
// application process. That is the same process the guest has to run in: an
// ANativeWindow is a process local handle, it cannot be used by a guest that
// runs in a different process.
#include <android/log.h>
#include <android/native_window.h>

#include <jni.h>

#define VK_USE_PLATFORM_ANDROID_KHR
#include <vulkan/vulkan.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "native_window.h"

namespace {

constexpr const char* kLogTag = "FFRS-VkProbe";

// The colour this probe clears to. It is picked so a screenshot of the
// device can be checked for it: R 16, G 200, B 96.
constexpr float kClearR = 16.0f / 255.0f;
constexpr float kClearG = 200.0f / 255.0f;
constexpr float kClearB = 96.0f / 255.0f;

void Log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

void Log(const char* fmt, ...) {
    char line[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "%s", line);
}

struct Instance {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t queue_family = 0;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer command = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t image_count = 0;
};

// Looks one entry point up in the instance. The name is what is asked for;
// asking for "vkGetInstanceProcAddr" itself would return the lookup function
// and not the entry point behind it.
void* InstanceProc(VkInstance instance, const char* name) {
    return reinterpret_cast<void*>(vkGetInstanceProcAddr(instance, name));
}

int CreateInstance(Instance& s) {
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "ffrs-rg477m-native";
    app.applicationVersion = 1;
    app.pEngineName = "ffrs-rg477m-native";
    app.engineVersion = 1;
    app.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo = &app;
    // The surface extensions have to be enabled on the instance, otherwise
    // vkGetInstanceProcAddr does not hand out their entry points.
    const char* instance_extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME,
                                          VK_KHR_ANDROID_SURFACE_EXTENSION_NAME};
    info.enabledExtensionCount = 2;
    info.ppEnabledExtensionNames = instance_extensions;

    VkResult r = vkCreateInstance(&info, nullptr, &s.instance);
    Log("vkCreateInstance -> %d", static_cast<int>(r));
    if (r != VK_SUCCESS) return 1;

    return 0;
}

// Picks the physical device and the queue family. The surface has to exist
// before this runs: the present support of a queue family is a property of
// the surface, not of the device alone.
int SelectPhysicalDevice(Instance& s, VkSurfaceKHR surface) {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(s.instance, &count, nullptr);
    if (count == 0) {
        Log("no physical device");
        return 1;
    }
    VkPhysicalDevice* devices = new VkPhysicalDevice[count];
    vkEnumeratePhysicalDevices(s.instance, &count, devices);

    auto get_surface_support =
        reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceSupportKHR>(
            InstanceProc(s.instance, "vkGetPhysicalDeviceSurfaceSupportKHR"));
    if (!get_surface_support) {
        Log("vkGetPhysicalDeviceSurfaceSupportKHR is not available");
        delete[] devices;
        return 1;
    }

    for (uint32_t i = 0; i < count; i++) {
        uint32_t families = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &families, nullptr);
        VkQueueFamilyProperties* props = new VkQueueFamilyProperties[families ? families : 1];
        vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &families, props);
        for (uint32_t f = 0; f < families; f++) {
            if (!(props[f].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
            VkBool32 present = VK_FALSE;
            get_surface_support(devices[i], f, surface, &present);
            if (!present) continue;
            s.physical = devices[i];
            s.queue_family = f;
            break;
        }
        delete[] props;
        if (s.physical) break;
    }
    delete[] devices;
    if (!s.physical) {
        Log("no physical device with a graphics queue that can present");
        return 1;
    }
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(s.physical, &props);
    Log("device \"%s\" api %u.%u", props.deviceName, VK_VERSION_MAJOR(props.apiVersion),
        VK_VERSION_MINOR(props.apiVersion));
    return 0;
}

}  // namespace

// Runs the probe against the given ANativeWindow. Returns 0 when the clear
// reached the screen, 1 otherwise. out_frames receives the number of frames
// that were presented.
extern "C" int ffrs_vulkan_present_probe(void* window, int32_t* out_width, int32_t* out_height,
                                          int32_t* out_frames) {
    if (out_width) *out_width = 0;
    if (out_height) *out_height = 0;
    if (out_frames) *out_frames = 0;
    if (!window) {
        Log("no window: the probe needs the Activity's own ANativeWindow");
        return 1;
    }
    ANativeWindow* native = static_cast<ANativeWindow*>(window);

    Instance s;
    if (CreateInstance(s) != 0) return 1;

    auto create_surface = reinterpret_cast<PFN_vkCreateAndroidSurfaceKHR>(
        InstanceProc(s.instance, "vkCreateAndroidSurfaceKHR"));
    if (!create_surface) {
        // Some loaders only hand the platform entry points out globally.
        create_surface = reinterpret_cast<PFN_vkCreateAndroidSurfaceKHR>(
            InstanceProc(nullptr, "vkCreateAndroidSurfaceKHR"));
    }
    if (!create_surface) {
        Log("vkCreateAndroidSurfaceKHR is not available");
        return 1;
    }
    VkAndroidSurfaceCreateInfoKHR surface_info{};
    surface_info.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    surface_info.window = native;
    VkResult r = create_surface(s.instance, &surface_info, nullptr, &s.surface);
    Log("vkCreateAndroidSurfaceKHR -> %d", static_cast<int>(r));
    if (r != VK_SUCCESS) return 1;

    if (SelectPhysicalDevice(s, s.surface) != 0) return 1;

    auto get_surface_caps = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>(
        InstanceProc(s.instance, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR"));
    auto get_surface_formats = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceFormatsKHR>(
        InstanceProc(s.instance, "vkGetPhysicalDeviceSurfaceFormatsKHR"));
    auto get_surface_modes = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfacePresentModesKHR>(
        InstanceProc(s.instance, "vkGetPhysicalDeviceSurfacePresentModesKHR"));
    if (!get_surface_caps || !get_surface_formats || !get_surface_modes) {
        Log("the surface query entry points are not available");
        return 1;
    }

    VkSurfaceCapabilitiesKHR caps{};
    get_surface_caps(s.physical, s.surface, &caps);
    uint32_t format_count = 0;
    get_surface_formats(s.physical, s.surface, &format_count, nullptr);
    VkSurfaceFormatKHR* formats = new VkSurfaceFormatKHR[format_count ? format_count : 1];
    get_surface_formats(s.physical, s.surface, &format_count, formats);
    // The format the surface prefers is the one the display path reads. On
    // this device the first entry is the one that keeps the channels in
    // order; forcing B8G8R8A8_UNORM instead swaps the red and blue channels
    // on screen, because the display reads the buffer as RGBA.
    s.format = formats[0].format;
    for (uint32_t i = 0; i < format_count; i++) {
        Log("surface format[%u] = %d color_space=%d", i, static_cast<int>(formats[i].format),
            static_cast<int>(formats[i].colorSpace));
        if (formats[i].format == VK_FORMAT_R8G8B8A8_UNORM) {
            s.format = formats[i].format;
            break;
        }
    }
    delete[] formats;
    uint32_t mode_count = 0;
    get_surface_modes(s.physical, s.surface, &mode_count, nullptr);
    VkPresentModeKHR* modes = new VkPresentModeKHR[mode_count ? mode_count : 1];
    get_surface_modes(s.physical, s.surface, &mode_count, modes);
    VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
    for (uint32_t i = 0; i < mode_count; i++) {
        if (modes[i] == VK_PRESENT_MODE_FIFO_KHR) {
            present_mode = modes[i];
            break;
        }
    }
    delete[] modes;

    s.width = caps.currentExtent.width;
    s.height = caps.currentExtent.height;
    if (s.width == UINT32_MAX || s.height == UINT32_MAX) {
        int32_t w = ANativeWindow_getWidth(native);
        int32_t h = ANativeWindow_getHeight(native);
        s.width = static_cast<uint32_t>(w > 0 ? w : 1);
        s.height = static_cast<uint32_t>(h > 0 ? h : 1);
    }
    if (out_width) *out_width = static_cast<int32_t>(s.width);
    if (out_height) *out_height = static_cast<int32_t>(s.height);

    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info{};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = s.queue_family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;
    const char* extension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkDeviceCreateInfo device_info{};
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    device_info.enabledExtensionCount = 1;
    device_info.ppEnabledExtensionNames = &extension;
    r = vkCreateDevice(s.physical, &device_info, nullptr, &s.device);
    Log("vkCreateDevice -> %d", static_cast<int>(r));
    if (r != VK_SUCCESS) return 1;
    vkGetDeviceQueue(s.device, s.queue_family, 0, &s.queue);

    auto get_device_proc = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
        vkGetDeviceProcAddr(s.device, "vkGetDeviceProcAddr"));
    (void)get_device_proc;
    auto create_swapchain = reinterpret_cast<PFN_vkCreateSwapchainKHR>(
        get_device_proc(s.device, "vkCreateSwapchainKHR"));
    auto acquire_next = reinterpret_cast<PFN_vkAcquireNextImageKHR>(
        get_device_proc(s.device, "vkAcquireNextImageKHR"));
    auto queue_present = reinterpret_cast<PFN_vkQueuePresentKHR>(
        get_device_proc(s.device, "vkQueuePresentKHR"));
    if (!create_swapchain || !acquire_next || !queue_present) {
        Log("the swapchain entry points are not available");
        return 1;
    }

    VkSwapchainCreateInfoKHR swap_info{};
    swap_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swap_info.surface = s.surface;
    swap_info.minImageCount = caps.minImageCount ? caps.minImageCount : 1;
    swap_info.imageFormat = s.format;
    swap_info.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    swap_info.imageExtent.width = s.width;
    swap_info.imageExtent.height = s.height;
    swap_info.imageArrayLayers = 1;
    swap_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    swap_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swap_info.preTransform = caps.currentTransform;
    swap_info.compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    swap_info.presentMode = present_mode;
    swap_info.clipped = VK_TRUE;
    swap_info.oldSwapchain = VK_NULL_HANDLE;
    r = create_swapchain(s.device, &swap_info, nullptr, &s.swapchain);
    Log("vkCreateSwapchainKHR -> %d extent=%ux%u format=%d present_mode=%d", static_cast<int>(r),
        s.width, s.height, static_cast<int>(s.format), static_cast<int>(present_mode));
    if (r != VK_SUCCESS) return 1;

    uint32_t image_count = 0;
    vkGetSwapchainImagesKHR(s.device, s.swapchain, &image_count, nullptr);
    VkImage* images = new VkImage[image_count ? image_count : 1];
    vkGetSwapchainImagesKHR(s.device, s.swapchain, &image_count, images);
    s.image_count = image_count;
    s.image = images[0];

    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = s.queue_family;
    r = vkCreateCommandPool(s.device, &pool_info, nullptr, &s.pool);
    if (r != VK_SUCCESS) {
        Log("vkCreateCommandPool -> %d", static_cast<int>(r));
        return 1;
    }
    VkCommandBufferAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc.commandPool = s.pool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    r = vkAllocateCommandBuffers(s.device, &alloc, &s.command);
    if (r != VK_SUCCESS) {
        Log("vkAllocateCommandBuffers -> %d", static_cast<int>(r));
        return 1;
    }
    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    r = vkCreateFence(s.device, &fence_info, nullptr, &s.fence);
    if (r != VK_SUCCESS) {
        Log("vkCreateFence -> %d", static_cast<int>(r));
        return 1;
    }

    int presented = 0;
    for (int frame = 0; frame < 3; frame++) {
        uint32_t index = 0;
        r = acquire_next(s.device, s.swapchain, 50000000ull, VK_NULL_HANDLE, VK_NULL_HANDLE, &index);
        Log("vkAcquireNextImageKHR -> %d index=%u", static_cast<int>(r), index);
        if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) break;
        VkImage image = images[index];

        vkResetCommandBuffer(s.command, 0);
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(s.command, &begin);

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(s.command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        VkClearColorValue clear{};
        clear.float32[0] = kClearR;
        clear.float32[1] = kClearG;
        clear.float32[2] = kClearB;
        clear.float32[3] = 1.0f;
        VkImageSubresourceRange range{};
        range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        range.levelCount = 1;
        range.layerCount = 1;
        vkCmdClearColorImage(s.command, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &range);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = 0;
        vkCmdPipelineBarrier(s.command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        vkEndCommandBuffer(s.command);

        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &s.command;
        vkResetFences(s.device, 1, &s.fence);
        r = vkQueueSubmit(s.queue, 1, &submit, s.fence);
        Log("vkQueueSubmit -> %d", static_cast<int>(r));
        if (r != VK_SUCCESS) break;

        // A real fence, not a CPU sleep: the completion is the GPU's.
        r = vkWaitForFences(s.device, 1, &s.fence, VK_TRUE, 5000000000ull);
        Log("vkWaitForFences -> %d", static_cast<int>(r));
        if (r != VK_SUCCESS) break;

        VkPresentInfoKHR present{};
        present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present.waitSemaphoreCount = 0;
        present.swapchainCount = 1;
        present.pSwapchains = &s.swapchain;
        present.pImageIndices = &index;
        r = queue_present(s.queue, &present);
        Log("vkQueuePresentKHR -> %d", static_cast<int>(r));
        if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) break;
        presented++;
    }
    delete[] images;
    if (out_frames) *out_frames = presented;
    Log("probe finished: presented=%d extent=%ux%u format=%d images=%u", presented, s.width, s.height,
        static_cast<int>(s.format), s.image_count);
    return presented > 0 ? 0 : 1;
}

// The Java entry point. It runs the probe against the window the
// SurfaceHolder callback published, which is the Activity's own window in
// this very process.
extern "C" JNIEXPORT jint JNICALL
Java_org_ffrs_diagnostic_GameWindowNative_nativeRunVulkanProbe(JNIEnv*, jclass) {
    void* window = nso_host_native_window_get();
    if (!window) {
        Log("nativeRunVulkanProbe: no window has been published yet");
        return 1;
    }
    int32_t width = 0;
    int32_t height = 0;
    int32_t frames = 0;
    int rc = ffrs_vulkan_present_probe(window, &width, &height, &frames);
    Log("nativeRunVulkanProbe rc=%d extent=%dx%d presented=%d (host probe, not a game frame)", rc,
        width, height, frames);
    return rc;
}
