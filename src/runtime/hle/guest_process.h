#ifndef NSO_GUEST_PROCESS_H
#define NSO_GUEST_PROCESS_H
#include "../nso/nso_loader.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct guest_process guest_process;
struct guest_process_config {size_t quota_bytes,primary_stack_bytes;};
/* Field names describe the native port policy, not recovered SDK C names.
 * The observed ABI writes six u64 values and one u32; final padding is untouched. */
struct guest_memory_info {
    uint64_t total,used,heap_capacity,heap_blocks,baseline,stack_bytes;
    uint32_t thread_objects;
};
typedef void *(*guest_process_callback)(guest_process *,void *);
/* Exclusive single-guest experiment. External raw loader/VA/Context operations
 * must be quiescent. Lifecycle calls are serialized by the caller; Query and VA
 * services run only on this process's registered native primary thread. No
 * extension resolver, backing mutator, SDK initialization or TP write is used. */
struct nso_result guest_process_create(const struct guest_process_config *,guest_process **out);
struct nso_result guest_process_load(guest_process *,const char *path,char *err,size_t errlen);
/* Borrowed inspection/call handle, never independently release it. */
nso_module *guest_process_module(guest_process *);
struct nso_result guest_process_run(guest_process *,guest_process_callback,void *arg,void **value);
/* Native typed snapshot: failure writes none of out. The guest void ABI wrapper
 * treats invalid state as a precise fatal error, never silent success. */
struct nso_result guest_process_memory_info(guest_process *,struct guest_memory_info *out);
uint64_t guest_process_resolve(uint32_t,const char *);
/* Quiescent only. Failed release retains *process for retry/process shutdown. */
struct nso_result guest_process_destroy(guest_process **process);
#ifdef __cplusplus
}
#endif
#endif
