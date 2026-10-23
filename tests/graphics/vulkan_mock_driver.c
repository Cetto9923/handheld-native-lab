/* SPDX-License-Identifier: MIT
 * Host-only deterministic test double. NEVER evidence of Android/GPU execution.
 */
#define _POSIX_C_SOURCE 200809L
#include <vulkan_core.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct VkDeviceMemory_T { unsigned char *data; size_t size; int mapped; };
struct VkBuffer_T { size_t size; VkDeviceMemory memory; };
struct VkImage_T { VkDeviceMemory memory; };
struct VkCommandPool_T { int unused; };
struct VkFence_T { int unused; };
typedef struct Command {
    int kind;
    VkBuffer source, destination;
    VkImage image;
    size_t size;
} Command;
static Command commands[8];
static size_t command_count;
static int live_objects, mapped_memories, result_calls, flushes, invalidates;
static int instance_token, physical_token, device_token, queue_token, command_token;

static int enabled(const char *name) { return getenv(name) && strcmp(getenv(name), "0"); }
static VkResult next_result(void) {
    ++result_calls;
    const char *failure = getenv("MOCK_FAIL_CALL");
    return failure && atoi(failure) == result_calls ? VK_ERROR_OUT_OF_HOST_MEMORY : VK_SUCCESS;
}
#define CHECK_RESULT() do { VkResult value = next_result(); if (value) return value; } while (0)
#define UNUSED(x) (void)(x)
#define CREATE(handle) do { *(handle) = calloc(1, sizeof(**(handle))); assert(*(handle)); ++live_objects; } while (0)
#define DESTROY(handle) do { assert(handle); free(handle); --live_objects; } while (0)

static void report_mock(void) {
    fprintf(stderr, "{\"test_double\":true,\"live_objects\":%d,\"mapped_memories\":%d,"
        "\"result_calls\":%d,\"flush_calls\":%d,\"invalidate_calls\":%d}\n",
        live_objects, mapped_memories, result_calls, flushes, invalidates);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(const VkInstanceCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkInstance *instance) {
    UNUSED(allocator); atexit(report_mock); CHECK_RESULT();
    assert(info->pApplicationInfo->apiVersion == VK_API_VERSION_1_0);
    assert(!info->enabledExtensionCount && !info->enabledLayerCount);
    *instance = (VkInstance)&instance_token; ++live_objects; return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyInstance(VkInstance instance, const VkAllocationCallbacks *allocator) {
    UNUSED(instance); UNUSED(allocator); --live_objects;
}
VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDevices(VkInstance instance, uint32_t *count,
    VkPhysicalDevice *devices) {
    UNUSED(instance); CHECK_RESULT();
    *count = enabled("MOCK_NO_DEVICE") ? 0 : 1;
    if (devices && *count) devices[0] = (VkPhysicalDevice)&physical_token;
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physical,
    uint32_t *count, VkQueueFamilyProperties *properties) {
    UNUSED(physical); *count = 1;
    if (properties) {
        memset(properties, 0, sizeof(*properties)); properties->queueCount = 1;
        properties->queueFlags = enabled("MOCK_NO_QUEUE") ? 0 : VK_QUEUE_GRAPHICS_BIT;
    }
}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties(VkPhysicalDevice physical,
    VkPhysicalDeviceProperties *properties) {
    UNUSED(physical); memset(properties, 0, sizeof(*properties));
    strcpy(properties->deviceName, "HOST TEST DOUBLE - NO GPU");
    properties->apiVersion = VK_API_VERSION_1_0;
}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice physical,
    VkPhysicalDeviceMemoryProperties *properties) {
    UNUSED(physical); memset(properties, 0, sizeof(*properties));
    properties->memoryTypeCount = 1; properties->memoryHeapCount = 1;
    properties->memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    if (!enabled("MOCK_NO_HOST_MEMORY")) properties->memoryTypes[0].propertyFlags |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    if (!enabled("MOCK_NONCOHERENT")) properties->memoryTypes[0].propertyFlags |= VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures(VkPhysicalDevice physical,
    VkPhysicalDeviceFeatures *features) {
    UNUSED(physical); memset(features, 0, sizeof(*features)); features->robustBufferAccess = VK_TRUE;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(VkPhysicalDevice physical, const VkDeviceCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkDevice *device) {
    UNUSED(physical); UNUSED(allocator); CHECK_RESULT();
    assert(!info->pEnabledFeatures && !info->enabledExtensionCount && info->queueCreateInfoCount == 1);
    *device = (VkDevice)&device_token; ++live_objects; return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyDevice(VkDevice device, const VkAllocationCallbacks *allocator) {
    UNUSED(device); UNUSED(allocator); --live_objects;
}
VKAPI_ATTR void VKAPI_CALL vkGetDeviceQueue(VkDevice device, uint32_t family, uint32_t index, VkQueue *queue) {
    UNUSED(device); assert(!family && !index); *queue = (VkQueue)&queue_token;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateBuffer(VkDevice device, const VkBufferCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkBuffer *buffer) {
    UNUSED(device); UNUSED(allocator); CHECK_RESULT(); CREATE(buffer); (*buffer)->size = info->size; return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyBuffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks *allocator) {
    UNUSED(device); UNUSED(allocator); DESTROY(buffer);
}
VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements(VkDevice device, VkBuffer buffer,
    VkMemoryRequirements *requirements) {
    UNUSED(device); *requirements = (VkMemoryRequirements){.size = buffer->size + 256, .alignment = 256, .memoryTypeBits = 1};
}
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateMemory(VkDevice device, const VkMemoryAllocateInfo *info,
    const VkAllocationCallbacks *allocator, VkDeviceMemory *memory) {
    UNUSED(device); UNUSED(allocator); CHECK_RESULT(); CREATE(memory);
    (*memory)->size = info->allocationSize; (*memory)->data = calloc(1, info->allocationSize); assert((*memory)->data);
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkFreeMemory(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks *allocator) {
    UNUSED(device); UNUSED(allocator); assert(!memory->mapped); free(memory->data); DESTROY(memory);
}
VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory,
    VkDeviceSize offset) {
    UNUSED(device); assert(!offset); CHECK_RESULT(); buffer->memory = memory; return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkMapMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset,
    VkDeviceSize size, VkMemoryMapFlags flags, void **mapped) {
    UNUSED(device); assert(!offset && size == VK_WHOLE_SIZE && !flags); CHECK_RESULT();
    assert(!memory->mapped); memory->mapped = 1; ++mapped_memories; *mapped = memory->data; return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkUnmapMemory(VkDevice device, VkDeviceMemory memory) {
    UNUSED(device); assert(memory->mapped); memory->mapped = 0; --mapped_memories;
}
VKAPI_ATTR VkResult VKAPI_CALL vkFlushMappedMemoryRanges(VkDevice device, uint32_t count,
    const VkMappedMemoryRange *ranges) {
    UNUSED(device); CHECK_RESULT(); assert(count == 1 && ranges->memory->mapped);
    assert(!ranges->offset && ranges->size == VK_WHOLE_SIZE); ++flushes; return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkInvalidateMappedMemoryRanges(VkDevice device, uint32_t count,
    const VkMappedMemoryRange *ranges) {
    UNUSED(device); CHECK_RESULT(); assert(count == 2);
    for (uint32_t i = 0; i < count; ++i) assert(ranges[i].memory->mapped && !ranges[i].offset && ranges[i].size == VK_WHOLE_SIZE);
    ++invalidates; return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceImageFormatProperties(VkPhysicalDevice physical,
    VkFormat format, VkImageType type, VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags,
    VkImageFormatProperties *properties) {
    UNUSED(physical); assert(format == VK_FORMAT_R8G8B8A8_UNORM && type == VK_IMAGE_TYPE_2D);
    assert(tiling == VK_IMAGE_TILING_OPTIMAL && usage == (VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT) && !flags);
    CHECK_RESULT(); memset(properties, 0, sizeof(*properties)); properties->maxExtent = (VkExtent3D){4096, 4096, 1};
    properties->sampleCounts = VK_SAMPLE_COUNT_1_BIT; return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateImage(VkDevice device, const VkImageCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkImage *image) {
    UNUSED(device); UNUSED(allocator); assert(info->extent.width == 8 && info->extent.height == 8);
    CHECK_RESULT(); CREATE(image); return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks *allocator) {
    UNUSED(device); UNUSED(allocator); DESTROY(image);
}
VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements(VkDevice device, VkImage image, VkMemoryRequirements *requirements) {
    UNUSED(device); UNUSED(image); *requirements = (VkMemoryRequirements){.size = 512, .alignment = 256, .memoryTypeBits = 1};
}
VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize offset) {
    UNUSED(device); assert(!offset); CHECK_RESULT(); image->memory = memory; return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkCommandPool *pool) {
    UNUSED(device); UNUSED(info); UNUSED(allocator); CHECK_RESULT(); CREATE(pool); return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyCommandPool(VkDevice device, VkCommandPool pool, const VkAllocationCallbacks *allocator) {
    UNUSED(device); UNUSED(allocator); DESTROY(pool);
}
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo *info,
    VkCommandBuffer *command) {
    UNUSED(device); assert(info->commandBufferCount == 1); CHECK_RESULT();
    *command = (VkCommandBuffer)&command_token; return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateFence(VkDevice device, const VkFenceCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkFence *fence) {
    UNUSED(device); UNUSED(info); UNUSED(allocator); CHECK_RESULT(); CREATE(fence); return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyFence(VkDevice device, VkFence fence, const VkAllocationCallbacks *allocator) {
    UNUSED(device); UNUSED(allocator); DESTROY(fence);
}
VKAPI_ATTR VkResult VKAPI_CALL vkBeginCommandBuffer(VkCommandBuffer command, const VkCommandBufferBeginInfo *info) {
    UNUSED(command); UNUSED(info); CHECK_RESULT(); return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkEndCommandBuffer(VkCommandBuffer command) {
    UNUSED(command); CHECK_RESULT(); assert(command_count == 3); return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(VkCommandBuffer command, VkPipelineStageFlags src,
    VkPipelineStageFlags dst, VkDependencyFlags flags, uint32_t memory_count, const VkMemoryBarrier *memory,
    uint32_t buffer_count, const VkBufferMemoryBarrier *buffers, uint32_t image_count, const VkImageMemoryBarrier *images) {
    UNUSED(command); UNUSED(src); UNUSED(dst); UNUSED(flags); UNUSED(memory); assert(!memory_count);
    assert((buffer_count && buffers) || (image_count && images));
}
VKAPI_ATTR void VKAPI_CALL vkCmdCopyBuffer(VkCommandBuffer command, VkBuffer source, VkBuffer destination,
    uint32_t count, const VkBufferCopy *copy) {
    UNUSED(command); assert(count == 1 && !copy->srcOffset && !copy->dstOffset && copy->size == 65536);
    commands[command_count++] = (Command){.kind = 1, .source = source, .destination = destination, .size = copy->size};
}
VKAPI_ATTR void VKAPI_CALL vkCmdClearColorImage(VkCommandBuffer command, VkImage image, VkImageLayout layout,
    const VkClearColorValue *color, uint32_t count, const VkImageSubresourceRange *range) {
    UNUSED(command); assert(layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && count == 1 && range->layerCount == 1);
    assert(color->float32[0] == 1 && color->float32[1] == 0 && color->float32[2] == 1 && color->float32[3] == 1);
    commands[command_count++] = (Command){.kind = 2, .image = image, .size = 256};
}
VKAPI_ATTR void VKAPI_CALL vkCmdCopyImageToBuffer(VkCommandBuffer command, VkImage image, VkImageLayout layout,
    VkBuffer destination, uint32_t count, const VkBufferImageCopy *copy) {
    UNUSED(command); assert(layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && count == 1);
    assert(!copy->bufferOffset && !copy->bufferRowLength && !copy->bufferImageHeight && copy->imageExtent.width == 8);
    commands[command_count++] = (Command){.kind = 3, .image = image, .destination = destination, .size = 256};
}
VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit(VkQueue queue, uint32_t count, const VkSubmitInfo *submit, VkFence fence) {
    UNUSED(queue); UNUSED(fence); assert(count == 1 && submit->commandBufferCount == 1); CHECK_RESULT();
    for (size_t i = 0; i < command_count; ++i) {
        Command *c = &commands[i];
        if (c->kind == 1) memcpy(c->destination->memory->data, c->source->memory->data, c->size);
        if (c->kind == 2) for (size_t j = 0; j < c->size; ++j) c->image->memory->data[j] = j % 4 == 1 ? 0 : 255;
        if (c->kind == 3) memcpy(c->destination->memory->data, c->image->memory->data, c->size);
    }
    const char *corrupt = getenv("MOCK_CORRUPT");
    if (corrupt && !strcmp(corrupt, "buffer")) commands[0].destination->memory->data[123] ^= 1;
    if (corrupt && !strcmp(corrupt, "image")) commands[2].destination->memory->data[17] ^= 1;
    return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkWaitForFences(VkDevice device, uint32_t count, const VkFence *fence,
    VkBool32 wait_all, uint64_t timeout) {
    UNUSED(device); UNUSED(fence); assert(count == 1 && wait_all && timeout == 5000000000ULL); CHECK_RESULT();
    if (enabled("MOCK_HANG")) sleep(30);
    return enabled("MOCK_TIMEOUT") ? VK_TIMEOUT : VK_SUCCESS;
}
