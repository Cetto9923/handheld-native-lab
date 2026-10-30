/* SPDX-License-Identifier: MIT — FFRS adapter; LAS record attribution is separate. */
#ifndef FFRS_LAS_TRANSFER_BACKEND_H
#define FFRS_LAS_TRANSFER_BACKEND_H
#include "command_record.h"
#include "../vulkan/buffer_resource.h"
#include <stdbool.h>

enum { FFRS_LAS_MAX_BUFFERS=16, FFRS_LAS_MAX_RECORDS=64 };
typedef enum FfrsLasStatus {
    FFRS_LAS_OK, FFRS_LAS_INVALID, FFRS_LAS_UNSUPPORTED, FFRS_LAS_BUSY,
    FFRS_LAS_WRONG_THREAD, FFRS_LAS_QUARANTINED, FFRS_LAS_NO_ACTIVE, FFRS_LAS_LIMIT
} FfrsLasStatus;
typedef enum FfrsLasPhase {
    FFRS_LAS_IDLE, FFRS_LAS_ENCODED, FFRS_LAS_PENDING, FFRS_LAS_COMPLETE,
    FFRS_LAS_UNCERTAIN
} FfrsLasPhase;
typedef struct FfrsLasBufferBinding {
    uint64_t address, bytes;
    VkDeviceSize offset;
    FfrsVkBufferResource *resource; /* borrowed, never copied or destroyed here */
    VkBufferUsageFlags usage;     /* caller certifies actual creation usage */
} FfrsLasBufferBinding;
typedef struct FfrsLasBackend {
    uint64_t magic, owner;
    VkDevice device;
    VkFence fence;
    FfrsLasPhase phase;
    FfrsLasBufferBinding buffers[FFRS_LAS_MAX_BUFFERS];
    uint32_t copy_count, barrier_count;
    uint64_t copied_bytes;
    bool submit_accepted, fatal;
    VkResult last_vk_result;
} FfrsLasBackend;

/* Normal host ABI/x18 only. Monotonic per-thread identity is not a TP/TCB write.
 * Caller-owned zero-init objects, no copying/concurrent access. Every API checks
 * its creating thread, including reused OS thread IDs. Keep all borrowed resource
 * owners, handles and mapped spans alive until unbind succeeds; never destroy
 * them through another API while registered. Public fields are internal state;
 * callers must not modify them. No guest opaque bytes are touched.
 */
uint64_t ffrs_las_thread_id(void);
FfrsLasStatus ffrs_las_backend_check(const FfrsLasBackend *backend);
FfrsLasStatus ffrs_las_backend_init(FfrsLasBackend *backend,VkDevice device);
FfrsLasStatus ffrs_las_backend_bind(FfrsLasBackend *backend,uint64_t address,
    uint64_t bytes,FfrsVkBufferResource *resource,VkDeviceSize offset,VkBufferUsageFlags usage);
FfrsLasStatus ffrs_las_backend_unbind(FfrsLasBackend *backend,uint64_t address);
FfrsLasStatus ffrs_las_backend_close(FfrsLasBackend *backend);

/* Validates and snapshots the WHOLE bounded list before issuing any Vk command.
 * Caller supplies a readable, stable span and recording VkCommandBuffer from the
 * matching device/queue family, outside a render pass. END/BARRIER/aligned COPY
 * only; no nonzero copy flags, overlapping same-buffer copy, JUMP/CALL or shader.
 * core Vulkan 1.0 barriers, no optional feature/extension requirements.
 * ENCODED pins all bindings until cancel (after actual command reset/free) or
 * submit->successful wait->retire. Functions below OBSERVE the actual results of
 * the caller's queue/fence calls, never synthesize a submission or completion.
 */
FfrsLasStatus ffrs_las_backend_encode(FfrsLasBackend *backend,VkCommandBuffer command,
    const void *records,size_t bytes);
FfrsLasStatus ffrs_las_backend_submitted(FfrsLasBackend *backend,VkFence fence,VkResult actual_result);
FfrsLasStatus ffrs_las_backend_waited(FfrsLasBackend *backend,VkFence fence,VkResult actual_result);
FfrsLasStatus ffrs_las_backend_retire(FfrsLasBackend *backend);
FfrsLasStatus ffrs_las_backend_cancel(FfrsLasBackend *backend,bool command_reset_or_freed);
/* TIMEOUT can be retried against the SAME fence after an accepted submission.
 * Submit failure or DEVICE_LOST/error latches permanent quarantine: even a later
 * SUCCESS observation is not accepted as an idle certificate. No force-reset or
 * implicit DeviceWaitIdle; uncertain owners survive for device-owner teardown.
 */
#endif
