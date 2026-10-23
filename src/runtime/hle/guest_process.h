#ifndef NSO_GUEST_PROCESS_H
#define NSO_GUEST_PROCESS_H
#include "../nso/nso_loader.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct guest_process guest_process;
struct guest_primary_stack;
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
 * services run only on this process's registered native primary thread. One
 * private guest_pages owner supplies VA and exact page-allocation identities.
 * Only disjoint owned maps and whole-allocation frees are admitted; unsupported
 * transitions terminate with diagnostics, never a guessed SDK failure Result.
 * No extension, unrelated backing mutator, SDK init or TP write is used. */
/* A logical 32-slot OS manager belongs to this process; each actual primary
 * registers a separate record. Normal return drains callbacks through the idle
 * native gateway before unregister/ThreadReturned/stack refund. Provider failure
 * failstops while those owners remain alive; no recoverable stack-retention or
 * SDK ThreadType/ELF TLS layout is claimed. */
struct nso_result guest_process_create(const struct guest_process_config *,guest_process **out);
/* Complete explicit guest vector, including caller-selected argv[0]. Inputs
 * and its NUL-terminated strings must remain readable and unchanged until this
 * call returns (ordinary C caller contract). Copies live
 * in one charged RW mapping, including a NULL tail even for argc0. The mutable
 * guest vector never controls ownership/free addresses. Existing create means
 * an explicitly empty guest command line, not diagnostic host arguments. */
struct nso_result guest_process_create_with_arguments(const struct guest_process_config *,
        size_t argc,const char *const argv[],guest_process **out);
/* Explicit management-resource policy; no default or original kernel/RSS
 * equivalence. Arguments retain the create_with_arguments ownership contract. */
struct nso_result guest_process_create_with_resources(const struct guest_process_config *,
        size_t resource_bytes,size_t argc,const char *const argv[],guest_process **out);
struct guest_virtual_resource_usage {uint64_t total,used;};
/* Same live-primary ownership checks as Query; invalid leaves out untouched.
 * used includes the original SDK's saturating 0x80000 reporting margin. */
struct nso_result guest_process_resource_info(guest_process *,struct guest_virtual_resource_usage *out);
#if defined(NSO_PROFILE_ARGUMENT_TESTING)
/* Normal VM failure contracts only; not compiled into production. */
void guest_process_test_argument_vm(void *(*map)(size_t bytes,size_t pending),
        int (*unmap)(void *,size_t));
#endif
struct nso_result guest_process_load(guest_process *,const char *path,char *err,size_t errlen);
/* Borrowed inspection/call handle, never independently release it. */
nso_module *guest_process_module(guest_process *);
struct nso_result guest_process_run(guest_process *,guest_process_callback,void *arg,void **value);
/* Known integer/pointer register ABI only. Up to six explicit scalar arguments,
 * one x0 result, no foreign unwind/stack arguments/unknown aggregate ABI.
 * Android native boundary only: unsupported platforms never execute raw code.
 * Multiple calls share one actual primary session. After that primary returns,
 * this module cannot execute again; bindings survive until real module unload. */
struct nso_result guest_process_call(guest_process *,uint64_t offset,
        const uint64_t arguments[6],unsigned argument_count,uint64_t *result);
struct nso_result guest_process_initialize(guest_process *,nso_init_policy);
/* Native typed snapshot: failure writes none of out. The guest void ABI wrapper
 * treats invalid state as a precise fatal error, never silent success. */
struct nso_result guest_process_memory_info(guest_process *,struct guest_memory_info *out);
/* Executing primary only: copy the verified logical/raw/native descriptor. */
struct nso_result guest_process_primary_stack(guest_process *,struct guest_primary_stack *out);
uint64_t guest_process_resolve(uint32_t,const char *);
/* Quiescent only. Failed release retains *process for retry/process shutdown. */
struct nso_result guest_process_destroy(guest_process **process);
#ifdef __cplusplus
}
#endif
#endif
