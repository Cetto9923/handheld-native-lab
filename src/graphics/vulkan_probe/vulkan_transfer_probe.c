/* SPDX-License-Identifier: MIT
 * Standalone Vulkan transfer-operation probe. No shaders, surface, or game code.
 */
#define _POSIX_C_SOURCE 200809L
#include <vulkan_core.h>
#include "../vulkan/buffer_resource.h"
#include <inttypes.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef FFRS_VK_SOURCE_SHA256
#define FFRS_VK_SOURCE_SHA256 "unfrozen"
#endif
#ifndef FFRS_VK_GIT_HEAD
#define FFRS_VK_GIT_HEAD "unknown"
#endif

enum { COPY_BYTES = 65536, IMAGE_WIDTH = 8, IMAGE_HEIGHT = 8,
       IMAGE_BYTES = IMAGE_WIDTH * IMAGE_HEIGHT * 4, MAX_STAGES = 64 };
static const uint64_t FENCE_TIMEOUT_NS = UINT64_C(5000000000);

typedef struct Stage { const char *name; VkResult result; } Stage;
typedef FfrsVkBufferResource HostBuffer;
typedef struct Probe {
    VkInstance instance;
    VkPhysicalDevice physical;
    VkDevice device;
    VkQueue queue;
    VkCommandPool pool;
    VkCommandBuffer command;
    VkFence fence;
    VkImage image;
    VkDeviceMemory image_memory;
    VkDeviceSize image_allocation_bytes;
    uint32_t image_memory_type;
    VkMemoryPropertyFlags image_memory_flags;
    HostBuffer source, destination, pixels;
    VkPhysicalDeviceProperties properties;
    VkPhysicalDeviceMemoryProperties memory_properties;
    VkPhysicalDeviceFeatures features;
    uint32_t device_index, queue_family;
    VkQueueFlags queue_flags;
    Stage stages[MAX_STAGES];
    size_t stage_count;
    const char *failure_stage, *failure;
    int exit_code;
    bool pending, commands_recorded, submitted, fence_complete, compared, core_features_queried;
    uint64_t wait_elapsed_ns;
    size_t buffer_mismatches, image_mismatches;
    size_t buffer_first_mismatch, image_first_mismatch;
    uint32_t destroyed_objects, unmapped_allocations;
} Probe;

static void watchdog(int signal_number) {
    (void)signal_number;
    static const char message[] = "{\"schema\":\"ffrs.vulkan.transfer.v1\",\"status\":\"FAIL\","
        "\"failure\":\"PROCESS_WATCHDOG_TIMEOUT\",\"exit_code\":124,"
        "\"cleanup\":\"process_teardown_watchdog\",\"gpu_completion_confirmed\":false}\n";
    /* Only async-signal-safe functions; there is no partial normal JSON output. */
    (void)write(STDOUT_FILENO, message, sizeof(message) - 1);
    _exit(124);
}

static bool fail(Probe *p, const char *stage, const char *reason, int exit_code) {
    if (!p->failure) {
        p->failure_stage = stage;
        p->failure = reason;
        p->exit_code = exit_code;
    }
    return false;
}

static bool result(Probe *p, const char *stage, VkResult value) {
    if (p->stage_count == MAX_STAGES)
        return fail(p, stage, "STAGE_CAPACITY_EXCEEDED", 2);
    p->stages[p->stage_count++] = (Stage){stage, value};
    if (value != VK_SUCCESS) return fail(p, stage, "VK_RESULT_NOT_SUCCESS", 2);
    return true;
}

static const char *result_name(VkResult value) {
    switch (value) {
    case VK_SUCCESS: return "VK_SUCCESS";
    case VK_NOT_READY: return "VK_NOT_READY";
    case VK_TIMEOUT: return "VK_TIMEOUT";
    case VK_INCOMPLETE: return "VK_INCOMPLETE";
    case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
    case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    default: return "VK_RESULT_OTHER";
    }
}

static bool memory_type(Probe *p, uint32_t bits, VkMemoryPropertyFlags required,
                        VkMemoryPropertyFlags preferred, uint32_t *selected) {
    int best_score = -1;
    for (uint32_t i = 0; i < p->memory_properties.memoryTypeCount; ++i) {
        VkMemoryPropertyFlags flags = p->memory_properties.memoryTypes[i].propertyFlags;
        if (!(bits & (1u << i)) || (flags & required) != required) continue;
        int score = (flags & preferred) == preferred ? 1 : 0;
        if (score > best_score) { best_score = score; *selected = i; }
    }
    return best_score >= 0;
}

typedef struct BufferTrace { Probe *probe; const char *const *names; } BufferTrace;
static void buffer_observer(void *user, FfrsVkBufferOperation operation, VkResult value) {
    BufferTrace *trace = user;
    (void)result(trace->probe, trace->names[operation], value);
}

static bool create_host_buffer(Probe *p, HostBuffer *b, VkDeviceSize bytes,
                               VkBufferUsageFlags usage, const char *const names[5]) {
    FfrsVkBufferCreateInfo info = {.device = p->device,
        .memory_properties = &p->memory_properties, .bytes = bytes, .usage = usage};
    BufferTrace trace = {p, names};
    FfrsVkBufferResult created = ffrs_vk_buffer_create(b, &info, buffer_observer, &trace);
    p->destroyed_objects += created.rollback.destroyed_objects;
    p->unmapped_allocations += created.rollback.unmapped_allocations;
    if (created.status == FFRS_VK_BUFFER_NO_HOST_MEMORY)
        return fail(p, names[1], "NO_HOST_VISIBLE_MEMORY_TYPE", 5);
    if (created.status == FFRS_VK_BUFFER_INVALID_ARGUMENT)
        return fail(p, names[0], "INVALID_BUFFER_RESOURCE_ARGUMENT", 2);
    return created.status == FFRS_VK_BUFFER_OK && !p->failure;
}

static bool flush_buffer(Probe *p, HostBuffer *b, const char *name) {
    FfrsVkBufferResult flushed = ffrs_vk_buffer_flush(b);
    if (flushed.status == FFRS_VK_BUFFER_INVALID_ARGUMENT)
        return fail(p, name, "INVALID_BUFFER_RESOURCE_ARGUMENT", 2);
    return result(p, name, flushed.vk_result);
}

static uint8_t pattern(size_t index) {
    return (uint8_t)((index * 131u + (index >> 8) + 0x5au) & 0xffu);
}

static bool select_device(Probe *p) {
    uint32_t count = 0;
    if (!result(p, "enumerate_devices_count", vkEnumeratePhysicalDevices(p->instance, &count, NULL)))
        return false;
    if (!count || count > 256) return fail(p, "select_device", "INVALID_DEVICE_COUNT", 5);
    VkPhysicalDevice *devices = calloc(count, sizeof(*devices));
    if (!devices) return fail(p, "select_device", "HOST_ALLOCATION_FAILED", 2);
    bool ok = result(p, "enumerate_devices", vkEnumeratePhysicalDevices(p->instance, &count, devices));
    for (uint32_t i = 0; ok && i < count && !p->physical; ++i) {
        uint32_t queue_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &queue_count, NULL);
        if (!queue_count || queue_count > 4096) continue;
        VkQueueFamilyProperties *queues = calloc(queue_count, sizeof(*queues));
        if (!queues) { ok = fail(p, "select_queue", "HOST_ALLOCATION_FAILED", 2); break; }
        vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &queue_count, queues);
        for (uint32_t j = 0; j < queue_count; ++j) {
            /* Graphics guarantees support for the clear and transfer commands used here. */
            if (queues[j].queueCount && (queues[j].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                p->physical = devices[i]; p->device_index = i;
                p->queue_family = j; p->queue_flags = queues[j].queueFlags;
                break;
            }
        }
        free(queues);
    }
    free(devices);
    if (!ok) return false;
    if (!p->physical) return fail(p, "select_queue", "NO_GRAPHICS_QUEUE", 5);
    vkGetPhysicalDeviceProperties(p->physical, &p->properties);
    vkGetPhysicalDeviceMemoryProperties(p->physical, &p->memory_properties);
    vkGetPhysicalDeviceFeatures(p->physical, &p->features);
    p->core_features_queried = true;
    return true;
}

static bool setup(Probe *p) {
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "ffrs-vulkan-transfer-probe", .applicationVersion = 1,
        .pEngineName = "transfer-probe-only", .engineVersion = 1, .apiVersion = VK_API_VERSION_1_0};
    VkInstanceCreateInfo instance = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app};
    if (!result(p, "create_instance", vkCreateInstance(&instance, NULL, &p->instance))) return false;
    if (!select_device(p)) return false;
    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = p->queue_family, .queueCount = 1, .pQueuePriorities = &priority};
    /* No optional VkPhysicalDeviceFeatures and no device extensions are enabled. */
    VkDeviceCreateInfo device = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &queue};
    if (!result(p, "create_device", vkCreateDevice(p->physical, &device, NULL, &p->device))) return false;
    vkGetDeviceQueue(p->device, p->queue_family, 0, &p->queue);
    if (!p->queue) return fail(p, "get_device_queue", "NULL_QUEUE", 2);
    const char *const source_names[5] = {"create_source_buffer", "allocate_source_memory",
        "bind_source_memory", "map_source_memory", "flush_source_memory"};
    const char *const destination_names[5] = {"create_destination_buffer", "allocate_destination_memory",
        "bind_destination_memory", "map_destination_memory", "flush_destination_memory"};
    const char *const pixel_names[5] = {"create_pixel_buffer", "allocate_pixel_memory",
        "bind_pixel_memory", "map_pixel_memory", "flush_pixel_memory"};
    if (!create_host_buffer(p, &p->source, COPY_BYTES, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, source_names) ||
        !create_host_buffer(p, &p->destination, COPY_BYTES, VK_BUFFER_USAGE_TRANSFER_DST_BIT, destination_names) ||
        !create_host_buffer(p, &p->pixels, IMAGE_BYTES, VK_BUFFER_USAGE_TRANSFER_DST_BIT, pixel_names)) return false;
    for (size_t i = 0; i < COPY_BYTES; ++i) ((uint8_t *)p->source.mapped)[i] = pattern(i);
    memset(p->destination.mapped, 0x33, COPY_BYTES);
    memset(p->pixels.mapped, 0x33, IMAGE_BYTES);
    if (!flush_buffer(p, &p->source, source_names[4]) ||
        !flush_buffer(p, &p->destination, destination_names[4]) ||
        !flush_buffer(p, &p->pixels, pixel_names[4])) return false;
    VkImageFormatProperties format;
    if (!result(p, "query_image_format", vkGetPhysicalDeviceImageFormatProperties(p->physical,
        VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, 0, &format))) return false;
    if (format.maxExtent.width < IMAGE_WIDTH || format.maxExtent.height < IMAGE_HEIGHT ||
        !(format.sampleCounts & VK_SAMPLE_COUNT_1_BIT))
        return fail(p, "query_image_format", "IMAGE_LIMIT_INSUFFICIENT", 5);
    VkImageCreateInfo image = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = {IMAGE_WIDTH, IMAGE_HEIGHT, 1}, .mipLevels = 1, .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
    if (!result(p, "create_image", vkCreateImage(p->device, &image, NULL, &p->image))) return false;
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(p->device, p->image, &requirements);
    if (!memory_type(p, requirements.memoryTypeBits, 0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     &p->image_memory_type)) return fail(p, "allocate_image_memory", "NO_IMAGE_MEMORY_TYPE", 5);
    p->image_allocation_bytes = requirements.size;
    p->image_memory_flags = p->memory_properties.memoryTypes[p->image_memory_type].propertyFlags;
    VkMemoryAllocateInfo allocate = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size, .memoryTypeIndex = p->image_memory_type};
    if (!result(p, "allocate_image_memory", vkAllocateMemory(p->device, &allocate, NULL, &p->image_memory)))
        return false;
    if (!result(p, "bind_image_memory", vkBindImageMemory(p->device, p->image, p->image_memory, 0))) return false;
    VkCommandPoolCreateInfo pool = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, .queueFamilyIndex = p->queue_family};
    if (!result(p, "create_command_pool", vkCreateCommandPool(p->device, &pool, NULL, &p->pool))) return false;
    VkCommandBufferAllocateInfo commands = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = p->pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
    if (!result(p, "allocate_command_buffer", vkAllocateCommandBuffers(p->device, &commands, &p->command)))
        return false;
    VkFenceCreateInfo fence = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    return result(p, "create_fence", vkCreateFence(p->device, &fence, NULL, &p->fence));
}

static VkBufferMemoryBarrier buffer_barrier(VkBuffer buffer, VkAccessFlags source, VkAccessFlags dest) {
    VkBufferMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = source, .dstAccessMask = dest,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = buffer, .offset = 0, .size = VK_WHOLE_SIZE};
    return barrier;
}

static bool record_commands(Probe *p) {
    VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    if (!result(p, "begin_command_buffer", vkBeginCommandBuffer(p->command, &begin))) return false;
    VkBufferMemoryBarrier before[3] = {
        buffer_barrier(p->source.buffer, VK_ACCESS_HOST_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT),
        buffer_barrier(p->destination.buffer, VK_ACCESS_HOST_WRITE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT),
        buffer_barrier(p->pixels.buffer, VK_ACCESS_HOST_WRITE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT)};
    vkCmdPipelineBarrier(p->command, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, NULL, 3, before, 0, NULL);
    VkBufferCopy copy = {.srcOffset = 0, .dstOffset = 0, .size = COPY_BYTES};
    vkCmdCopyBuffer(p->command, p->source.buffer, p->destination.buffer, 1, &copy);
    VkImageSubresourceRange subresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1};
    VkImageMemoryBarrier to_clear = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = 0, .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = p->image, .subresourceRange = subresource};
    vkCmdPipelineBarrier(p->command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, NULL, 0, NULL, 1, &to_clear);
    VkClearColorValue clear = {.float32 = {1.0f, 0.0f, 1.0f, 1.0f}};
    vkCmdClearColorImage(p->command, p->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &subresource);
    VkImageMemoryBarrier to_copy = to_clear;
    to_copy.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    to_copy.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    to_copy.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_copy.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    vkCmdPipelineBarrier(p->command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, NULL, 0, NULL, 1, &to_copy);
    VkBufferImageCopy pixel_copy = {.bufferOffset = 0, .bufferRowLength = 0, .bufferImageHeight = 0,
        .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0,
            .baseArrayLayer = 0, .layerCount = 1},
        .imageOffset = {0, 0, 0}, .imageExtent = {IMAGE_WIDTH, IMAGE_HEIGHT, 1}};
    vkCmdCopyImageToBuffer(p->command, p->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           p->pixels.buffer, 1, &pixel_copy);
    VkBufferMemoryBarrier after[2] = {
        buffer_barrier(p->destination.buffer, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT),
        buffer_barrier(p->pixels.buffer, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT)};
    vkCmdPipelineBarrier(p->command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                         0, 0, NULL, 2, after, 0, NULL);
    if (!result(p, "end_command_buffer", vkEndCommandBuffer(p->command))) return false;
    p->commands_recorded = true;
    return true;
}

static uint64_t monotonic_ns(void) {
    struct timespec time;
    if (clock_gettime(CLOCK_MONOTONIC, &time)) return 0;
    return (uint64_t)time.tv_sec * UINT64_C(1000000000) + (uint64_t)time.tv_nsec;
}

static bool execute(Probe *p) {
    VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1, .pCommandBuffers = &p->command};
    if (!result(p, "queue_submit", vkQueueSubmit(p->queue, 1, &submit, p->fence))) return false;
    p->pending = true; p->submitted = true;
    uint64_t start = monotonic_ns();
    VkResult wait = vkWaitForFences(p->device, 1, &p->fence, VK_TRUE, FENCE_TIMEOUT_NS);
    uint64_t end = monotonic_ns();
    p->wait_elapsed_ns = end >= start ? end - start : 0;
    if (wait == VK_SUCCESS) p->pending = false;
    if (!result(p, "wait_for_fence", wait)) {
        if (wait == VK_TIMEOUT) { p->failure = "FENCE_TIMEOUT"; p->exit_code = 4; }
        return false;
    }
    p->fence_complete = true;
    const FfrsVkBufferResource *readback[2] = {&p->destination, &p->pixels};
    VkMappedMemoryRange ranges[2];
    FfrsVkBufferResult invalidated = ffrs_vk_buffer_invalidate(readback, 2, ranges);
    if (invalidated.status == FFRS_VK_BUFFER_INVALID_ARGUMENT)
        return fail(p, "invalidate_readback_memory", "INVALID_BUFFER_RESOURCE_ARGUMENT", 2);
    if (!result(p, "invalidate_readback_memory", invalidated.vk_result))
        return false;
    for (size_t i = 0; i < COPY_BYTES; ++i) {
        if (((const uint8_t *)p->destination.mapped)[i] != pattern(i)) {
            if (!p->buffer_mismatches) p->buffer_first_mismatch = i;
            ++p->buffer_mismatches;
        }
    }
    static const uint8_t expected[4] = {255, 0, 255, 255};
    for (size_t i = 0; i < IMAGE_BYTES; ++i) {
        if (((const uint8_t *)p->pixels.mapped)[i] != expected[i % 4]) {
            if (!p->image_mismatches) p->image_first_mismatch = i;
            ++p->image_mismatches;
        }
    }
    p->compared = true;
    if (p->buffer_mismatches || p->image_mismatches)
        return fail(p, "compare_readback", "READBACK_MISMATCH", 3);
    return true;
}

static void cleanup_buffer(Probe *p, HostBuffer *b) {
    FfrsVkBufferReleaseStats released;
    if (!ffrs_vk_buffer_destroy(b, !p->pending, &released)) {
        (void)fail(p, "destroy_buffer_resource", "GPU_QUIESCENCE_NOT_CONFIRMED", 2);
        return;
    }
    p->destroyed_objects += released.destroyed_objects;
    p->unmapped_allocations += released.unmapped_allocations;
}

static void cleanup(Probe *p) {
    /* No unbounded DeviceWaitIdle and no invalid destruction of in-flight objects.
     * If completion is uncertain, the CLI exits and the OS reclaims the process.
     */
    if (p->pending) return;
    if (p->device) {
        if (p->fence) { vkDestroyFence(p->device, p->fence, NULL); ++p->destroyed_objects; }
        if (p->pool) { vkDestroyCommandPool(p->device, p->pool, NULL); ++p->destroyed_objects; }
        if (p->image) { vkDestroyImage(p->device, p->image, NULL); ++p->destroyed_objects; }
        if (p->image_memory) { vkFreeMemory(p->device, p->image_memory, NULL); ++p->destroyed_objects; }
        cleanup_buffer(p, &p->pixels);
        cleanup_buffer(p, &p->destination);
        cleanup_buffer(p, &p->source);
        vkDestroyDevice(p->device, NULL); ++p->destroyed_objects;
    }
    if (p->instance) { vkDestroyInstance(p->instance, NULL); ++p->destroyed_objects; }
}

static void json_string(const char *value) {
    if (!value) { fputs("null", stdout); return; }
    putchar('"');
    for (const unsigned char *s = (const unsigned char *)value; *s; ++s) {
        if (*s == '"' || *s == '\\') { putchar('\\'); putchar(*s); }
        else if (*s < 32 || *s >= 127) printf("\\u%04x", *s);
        else putchar(*s);
    }
    putchar('"');
}

static void print_buffer(const HostBuffer *buffer) {
    printf("{\"allocation_bytes\":%" PRIu64 ",\"memory_type\":%u,\"memory_flags\":%u}",
        (uint64_t)buffer->allocation_bytes, buffer->memory_type, buffer->memory_flags);
}

static void print_report(const Probe *p) {
    printf("{\"schema\":\"ffrs.vulkan.transfer.v1\",\"status\":\"%s\",\"exit_code\":%d,",
        p->failure ? "FAIL" : "PASS", p->exit_code);
    fputs("\"source_sha256\":", stdout); json_string(FFRS_VK_SOURCE_SHA256);
    fputs(",\"git_head_at_build\":", stdout); json_string(FFRS_VK_GIT_HEAD);
    fputs(",\"scope\":\"buffer_copy_and_image_clear_transfer_only\",\"game_rendering\":false,", stdout);
    fputs("\"failure_stage\":", stdout); json_string(p->failure_stage);
    fputs(",\"failure\":", stdout); json_string(p->failure);
    fputs(",\"device\":{\"name\":", stdout); json_string(p->properties.deviceName);
    printf(",\"index\":%u,\"vendor_id\":%u,\"device_id\":%u,\"api_version\":%u,"
        "\"driver_version\":%u,\"queue_family\":%u,\"queue_flags\":%u},",
        p->device_index, p->properties.vendorID, p->properties.deviceID, p->properties.apiVersion,
        p->properties.driverVersion, p->queue_family, p->queue_flags);
    printf("\"features\":{\"requested_api_version\":%u,\"optional_features_enabled\":false,"
        "\"device_extensions_enabled\":[],\"queried_core_features\":%s,"
        "\"queried_robust_buffer_access\":%s,\"descriptor_indexing_queried\":false,"
        "\"descriptor_indexing_used\":false,\"timeline_semaphore_used\":false},",
        VK_API_VERSION_1_0, p->core_features_queried ? "true" : "false",
        p->features.robustBufferAccess ? "true" : "false");
    printf("\"operations\":{\"commands_recorded\":%s,\"queue_submitted\":%s,"
        "\"fence_completed\":%s,\"readback_compared\":%s,\"fence_timeout_ns\":%" PRIu64 ","
        "\"wait_elapsed_ns\":%" PRIu64 ",\"buffer_copy_bytes\":%u,\"buffer_compared_bytes\":%u,"
        "\"buffer_mismatches\":%zu,\"buffer_first_mismatch\":",
        p->commands_recorded ? "true" : "false", p->submitted ? "true" : "false",
        p->fence_complete ? "true" : "false", p->compared ? "true" : "false",
        FENCE_TIMEOUT_NS, p->wait_elapsed_ns, p->submitted ? COPY_BYTES : 0,
        p->compared ? COPY_BYTES : 0, p->buffer_mismatches);
    if (p->buffer_mismatches) printf("%zu", p->buffer_first_mismatch); else fputs("null", stdout);
    printf(",\"image_format\":\"VK_FORMAT_R8G8B8A8_UNORM\",\"image_extent\":[%u,%u],"
        "\"image_expected_rgba\":[255,0,255,255],\"image_compared_bytes\":%u,"
        "\"image_mismatches\":%zu,\"image_first_mismatch\":",
        IMAGE_WIDTH, IMAGE_HEIGHT, p->compared ? IMAGE_BYTES : 0, p->image_mismatches);
    if (p->image_mismatches) printf("%zu", p->image_first_mismatch); else fputs("null", stdout);
    fputs("},\"memory\":{\"source\":", stdout); print_buffer(&p->source);
    fputs(",\"destination\":", stdout); print_buffer(&p->destination);
    fputs(",\"pixels\":", stdout); print_buffer(&p->pixels);
    printf(",\"image\":{\"allocation_bytes\":%" PRIu64 ",\"memory_type\":%u,\"memory_flags\":%u}},",
        (uint64_t)p->image_allocation_bytes, p->image_memory_type, p->image_memory_flags);
    printf("\"cleanup\":{\"strategy\":\"%s\",\"destroyed_objects\":%u,\"unmapped_allocations\":%u},",
        p->pending ? "process_teardown_inflight_uncertain" : "explicit_reverse_release",
        p->destroyed_objects, p->unmapped_allocations);
    fputs("\"stages\":[", stdout);
    size_t errors = 0;
    for (size_t i = 0; i < p->stage_count; ++i) {
        if (i) putchar(',');
        fputs("{\"stage\":", stdout); json_string(p->stages[i].name);
        printf(",\"vk_result\":%d,\"vk_result_name\":", (int)p->stages[i].result);
        json_string(result_name(p->stages[i].result)); putchar('}');
        if (p->stages[i].result != VK_SUCCESS) ++errors;
    }
    printf("],\"vk_result_errors\":%zu}\n", errors);
}

int main(int argc, char **argv) {
    if (argc > 1) {
        if (argc == 2 && !strcmp(argv[1], "--help")) {
            puts("Vulkan transfer probe: 64KiB buffer copy + 8x8 RGBA8 image clear/readback.\n"
                 "No optional arguments; fence timeout=5s, process watchdog=20s; JSON on stdout.");
            return 0;
        }
        fputs("Unsupported argument. Use --help.\n", stderr); return 64;
    }
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = watchdog;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGALRM, &action, NULL)) { perror("sigaction"); return 2; }
    alarm(20);
    Probe probe;
    memset(&probe, 0, sizeof(probe));
    if (setup(&probe) && record_commands(&probe)) (void)execute(&probe);
    cleanup(&probe);
    print_report(&probe);
    fflush(stdout);
    alarm(0);
    /* An uncertain in-flight submission is reclaimed at process exit. */
    return probe.exit_code;
}
