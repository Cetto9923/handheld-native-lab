/* SPDX-License-Identifier: MIT */
#ifndef FFRS_VULKAN_BUFFER_RESOURCE_H
#define FFRS_VULKAN_BUFFER_RESOURCE_H

#include <vulkan_core.h>
#include <stdbool.h>
#include <stdint.h>

/* Owns one exclusive VkBuffer and one dedicated HOST_VISIBLE allocation,
 * persistently mapped in full. Zero-initialize before first use; do not copy an
 * owning resource. Device/memory properties belong to the caller and must match.
 * Calls and mapped CPU access require external synchronization. No queue work,
 * barriers, fence waits, device ownership, NVN ABI or suballocation is provided.
 */
typedef struct FfrsVkBufferResource {
    VkDevice device;
    VkBuffer buffer;
    VkDeviceMemory memory;
    void *mapped;
    VkDeviceSize bytes, allocation_bytes;
    uint32_t memory_type;
    VkMemoryPropertyFlags memory_flags;
} FfrsVkBufferResource;

typedef enum FfrsVkBufferOperation {
    FFRS_VK_BUFFER_CREATE, FFRS_VK_BUFFER_ALLOCATE, FFRS_VK_BUFFER_BIND,
    FFRS_VK_BUFFER_MAP, FFRS_VK_BUFFER_FLUSH, FFRS_VK_BUFFER_INVALIDATE
} FfrsVkBufferOperation;
typedef enum FfrsVkBufferStatus {
    FFRS_VK_BUFFER_OK, FFRS_VK_BUFFER_VULKAN_ERROR,
    FFRS_VK_BUFFER_NO_HOST_MEMORY, FFRS_VK_BUFFER_INVALID_ARGUMENT
} FfrsVkBufferStatus;
typedef struct FfrsVkBufferReleaseStats {
    uint32_t destroyed_objects, unmapped_allocations;
} FfrsVkBufferReleaseStats;
typedef struct FfrsVkBufferResult {
    FfrsVkBufferStatus status;
    FfrsVkBufferOperation operation;
    /* Meaningful only for OK/VULKAN_ERROR; local errors are not VkResult calls. */
    VkResult vk_result;
    FfrsVkBufferReleaseStats rollback;
} FfrsVkBufferResult;
typedef struct FfrsVkBufferCreateInfo {
    VkDevice device;
    const VkPhysicalDeviceMemoryProperties *memory_properties;
    VkDeviceSize bytes;
    VkBufferUsageFlags usage;
} FfrsVkBufferCreateInfo;

/* Optional observation of the four actual VkResult calls during creation.
 * The observer must not mutate/reenter the resource. It cannot change results.
 */
typedef void (*FfrsVkBufferObserver)(void *, FfrsVkBufferOperation, VkResult);
FfrsVkBufferResult ffrs_vk_buffer_create(FfrsVkBufferResource *resource,
    const FfrsVkBufferCreateInfo *info, FfrsVkBufferObserver observer, void *user);

/* Whole-allocation flush/invalidate is valid for coherent and noncoherent
 * HOST_VISIBLE memory, including nonCoherentAtomSize alignment at allocation
 * end. Coherent memory still receives the real Vulkan call. These operations
 * do NOT establish GPU completion or host/device execution dependencies.
 * Invalidate requires completed device writes and must precede CPU reads.
 * For a batch, buffers and scratch each contain count entries; all devices match.
 */
FfrsVkBufferResult ffrs_vk_buffer_flush(const FfrsVkBufferResource *resource);
FfrsVkBufferResult ffrs_vk_buffer_invalidate(
    const FfrsVkBufferResource *const *buffers, uint32_t count, VkMappedMemoryRange *scratch);

/* Caller must confirm all GPU references are quiescent (e.g. never submitted,
 * or its fence completed). false retains every handle/mapping and returns false.
 * No implicit wait; timeout/device loss is not a completion certificate.
 * Successful release is idempotent and clears ownership, retaining size/type
 * metadata for diagnostics. stats may be NULL; otherwise overwritten, not added.
 * create rolls back confirmed partial ownership before returning a failure;
 * its rollback counts are separate from later destroy counts.
 */
bool ffrs_vk_buffer_destroy(FfrsVkBufferResource *resource, bool gpu_quiescent,
                          FfrsVkBufferReleaseStats *stats);

#endif
