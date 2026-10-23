/* SPDX-License-Identifier: MIT — typed FFRS adapter to LAS command records. */
#ifndef FFRS_LAS_TYPED_TRANSFER_H
#define FFRS_LAS_TYPED_TRANSFER_H
#include "transfer_backend.h"
#include "../nvn/include/nvn/nvn.h"
#include "../../runtime/nso/native_boundary.h"
enum { FFRS_LAS_MAX_OBJECTS=8 };
typedef struct FfrsLasPoolSpan {const void *key;void *memory;size_t bytes;} FfrsLasPoolSpan;
typedef struct FfrsLasCommandObject {
    NVNcommandBuffer *key;
    const void *pool_key;
    unsigned char *memory;
    size_t capacity,used;
    uint64_t handle;
    bool recording,sealed;
} FfrsLasCommandObject;
typedef struct FfrsLasTyped {
    uint64_t magic,owner;
    const NVNdevice *device_key;
    FfrsLasBackend *backend;
    FfrsLasStatus error;
    FfrsLasPoolSpan pools[FFRS_LAS_MAX_OBJECTS];
    FfrsLasCommandObject commands[FFRS_LAS_MAX_OBJECTS];
} FfrsLasTyped;
typedef struct FfrsLasTypedTarget {
    const char *name;
    void (*host_target)(void);
    nb_signature signature;
} FfrsLasTypedTarget;

/* Borrowed side tables, never write guest opaque object/builder bytes. This
 * first subset attaches already-owned command-memory pool spans explicitly;
 * it does not claim NVN pool allocation/builder/private-layout compatibility.
 * Resource address mappings belong to backend_bind and existing buffer_resource.
 * All calls require normal host x18. Explicit per-thread enter/leave scopes are
 * non-nesting and do not extend object lifetime; keep context alive until leave.
 * No context on a new thread and no stale process-global host-thunk caching.
 */
FfrsLasStatus ffrs_las_typed_init(FfrsLasTyped *context,FfrsLasBackend *backend,
                                const NVNdevice *device_key);
FfrsLasStatus ffrs_las_typed_enter(FfrsLasTyped *context);
FfrsLasStatus ffrs_las_typed_leave(FfrsLasTyped *context);
FfrsLasStatus ffrs_las_typed_attach_pool(FfrsLasTyped *context,const NVNmemoryPool *key,
                                      void *memory,size_t bytes);
FfrsLasStatus ffrs_las_typed_close(FfrsLasTyped *context);
FfrsLasStatus ffrs_las_typed_status(const FfrsLasTyped *context);
FfrsLasStatus ffrs_las_typed_clear_error(FfrsLasTyped *context);
FfrsLasStatus ffrs_las_typed_encode(FfrsLasTyped *context,VkCommandBuffer command,
                                  NVNcommandHandle handle);
const FfrsLasTypedTarget *ffrs_las_typed_targets(size_t *count);
/* Host target metadata ONLY: future runtime prebinds v1 native_boundary thunks
 * while scope is idle, preserving owner/thread lifetime. No bind, resolver or
 * dispatch mutation here. Never expose these bare host targets to guest code.
 * No QueueSubmitCommands export: encode writes into caller-owned command buffer;
 * actual queue/fence calls remain with its device owner and are observed below.
 * Unsupported OOM callback/JUMP/CALL, memory chaining, nonzero barrier/copy flags,
 * and invalid object/order/capacity latch error and invalidate all local handles.
 */
#endif
