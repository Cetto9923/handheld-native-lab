/* SPDX-License-Identifier: MIT */
#include "buffer_resource.h"
#include <stddef.h>

static FfrsVkBufferResult outcome(FfrsVkBufferStatus status,
                                 FfrsVkBufferOperation operation, VkResult value) {
    return (FfrsVkBufferResult){.status = status, .operation = operation, .vk_result = value};
}

bool ffrs_vk_buffer_destroy(FfrsVkBufferResource *b, bool gpu_quiescent,
                          FfrsVkBufferReleaseStats *stats) {
    FfrsVkBufferReleaseStats released = {0};
    if (stats) *stats = released;
    if (!b || !gpu_quiescent) return false;
    if ((b->buffer || b->memory || b->mapped) && !b->device) return false;
    if (b->mapped) {
        vkUnmapMemory(b->device, b->memory);
        b->mapped = NULL;
        ++released.unmapped_allocations;
    }
    if (b->buffer) {
        vkDestroyBuffer(b->device, b->buffer, NULL);
        b->buffer = VK_NULL_HANDLE;
        ++released.destroyed_objects;
    }
    if (b->memory) {
        vkFreeMemory(b->device, b->memory, NULL);
        b->memory = VK_NULL_HANDLE;
        ++released.destroyed_objects;
    }
    b->device = VK_NULL_HANDLE;
    if (stats) *stats = released;
    return true;
}

static FfrsVkBufferResult rollback(FfrsVkBufferResource *b, FfrsVkBufferResult result) {
    /* This newly-created resource has never escaped to a queue submission. */
    (void)ffrs_vk_buffer_destroy(b, true, &result.rollback);
    return result;
}

static void observe(FfrsVkBufferObserver observer, void *user,
                    FfrsVkBufferOperation operation, VkResult value) {
    if (observer) observer(user, operation, value);
}

FfrsVkBufferResult ffrs_vk_buffer_create(FfrsVkBufferResource *b,
    const FfrsVkBufferCreateInfo *info, FfrsVkBufferObserver observer, void *user) {
    if (!b || !info || !info->device || !info->memory_properties || !info->bytes ||
        !info->usage || info->memory_properties->memoryTypeCount > VK_MAX_MEMORY_TYPES ||
        b->buffer || b->memory || b->mapped || b->device)
        return outcome(FFRS_VK_BUFFER_INVALID_ARGUMENT, FFRS_VK_BUFFER_CREATE, VK_SUCCESS);
    *b = (FfrsVkBufferResource){.device = info->device, .bytes = info->bytes};
    VkBufferCreateInfo create = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = info->bytes, .usage = info->usage, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    VkBuffer buffer = VK_NULL_HANDLE;
    VkResult value = vkCreateBuffer(info->device, &create, NULL, &buffer);
    observe(observer, user, FFRS_VK_BUFFER_CREATE, value);
    if (value != VK_SUCCESS)
        return rollback(b, outcome(FFRS_VK_BUFFER_VULKAN_ERROR, FFRS_VK_BUFFER_CREATE, value));
    b->buffer = buffer;
    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(info->device, buffer, &requirements);
    int best_score = -1;
    for (uint32_t i = 0; i < info->memory_properties->memoryTypeCount; ++i) {
        VkMemoryPropertyFlags flags = info->memory_properties->memoryTypes[i].propertyFlags;
        if (!(requirements.memoryTypeBits & (UINT32_C(1) << i)) ||
            !(flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) continue;
        int score = (flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) ? 1 : 0;
        if (score > best_score) { best_score = score; b->memory_type = i; }
    }
    if (best_score < 0)
        return rollback(b, outcome(FFRS_VK_BUFFER_NO_HOST_MEMORY, FFRS_VK_BUFFER_ALLOCATE, VK_SUCCESS));
    b->memory_flags = info->memory_properties->memoryTypes[b->memory_type].propertyFlags;
    b->allocation_bytes = requirements.size;
    VkMemoryAllocateInfo allocate = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size, .memoryTypeIndex = b->memory_type};
    VkDeviceMemory memory = VK_NULL_HANDLE;
    value = vkAllocateMemory(info->device, &allocate, NULL, &memory);
    observe(observer, user, FFRS_VK_BUFFER_ALLOCATE, value);
    if (value != VK_SUCCESS)
        return rollback(b, outcome(FFRS_VK_BUFFER_VULKAN_ERROR, FFRS_VK_BUFFER_ALLOCATE, value));
    b->memory = memory;
    value = vkBindBufferMemory(info->device, buffer, memory, 0);
    observe(observer, user, FFRS_VK_BUFFER_BIND, value);
    if (value != VK_SUCCESS)
        return rollback(b, outcome(FFRS_VK_BUFFER_VULKAN_ERROR, FFRS_VK_BUFFER_BIND, value));
    void *mapped = NULL;
    value = vkMapMemory(info->device, memory, 0, VK_WHOLE_SIZE, 0, &mapped);
    observe(observer, user, FFRS_VK_BUFFER_MAP, value);
    if (value != VK_SUCCESS)
        return rollback(b, outcome(FFRS_VK_BUFFER_VULKAN_ERROR, FFRS_VK_BUFFER_MAP, value));
    b->mapped = mapped;
    return outcome(FFRS_VK_BUFFER_OK, FFRS_VK_BUFFER_MAP, VK_SUCCESS);
}

static bool mapped_resource(const FfrsVkBufferResource *b) {
    return b && b->device && b->memory && b->mapped &&
           (b->memory_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
}

static VkMappedMemoryRange whole_range(const FfrsVkBufferResource *b) {
    return (VkMappedMemoryRange){.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
        .memory = b->memory, .offset = 0, .size = VK_WHOLE_SIZE};
}

FfrsVkBufferResult ffrs_vk_buffer_flush(const FfrsVkBufferResource *b) {
    if (!mapped_resource(b))
        return outcome(FFRS_VK_BUFFER_INVALID_ARGUMENT, FFRS_VK_BUFFER_FLUSH, VK_SUCCESS);
    VkMappedMemoryRange range = whole_range(b);
    VkResult value = vkFlushMappedMemoryRanges(b->device, 1, &range);
    return outcome(value == VK_SUCCESS ? FFRS_VK_BUFFER_OK : FFRS_VK_BUFFER_VULKAN_ERROR,
                   FFRS_VK_BUFFER_FLUSH, value);
}

FfrsVkBufferResult ffrs_vk_buffer_invalidate(
    const FfrsVkBufferResource *const *buffers, uint32_t count, VkMappedMemoryRange *scratch) {
    if (!buffers || !count || !scratch || !mapped_resource(buffers[0]))
        return outcome(FFRS_VK_BUFFER_INVALID_ARGUMENT, FFRS_VK_BUFFER_INVALIDATE, VK_SUCCESS);
    for (uint32_t i = 0; i < count; ++i) {
        if (!mapped_resource(buffers[i]) || buffers[i]->device != buffers[0]->device)
            return outcome(FFRS_VK_BUFFER_INVALID_ARGUMENT, FFRS_VK_BUFFER_INVALIDATE, VK_SUCCESS);
        scratch[i] = whole_range(buffers[i]);
    }
    VkResult value = vkInvalidateMappedMemoryRanges(buffers[0]->device, count, scratch);
    return outcome(value == VK_SUCCESS ? FFRS_VK_BUFFER_OK : FFRS_VK_BUFFER_VULKAN_ERROR,
                   FFRS_VK_BUFFER_INVALIDATE, value);
}
