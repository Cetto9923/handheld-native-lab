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
/* Capability for the configured native VA-owner policy, not full Horizon VM.
 * Live primary/owner/coherent ledgers required; invalid leaves out untouched.
 * An unconfigured resource policy returns canonical false. */
struct nso_result guest_process_va_enabled(guest_process *,int *out);
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
/* Native opaque API subset, NOT an SDK ThreadType/TCB memory layout. Tokens
 * are dedicated PROT_NONE pages, never pthread_t/host object addresses. Only
 * this actual live primary (including normal OS cleanup) can obtain/consume its
 * token. GetThreadId returns the actual native OS TID as uint64, not a fabricated
 * Horizon ID. Foreign/fresh/retired calls leave outputs untouched. Retired token
 * pages are held until quiescent destroy to prevent same-Process address reuse;
 * they are host administrative VA, not guest physical/backing quota bytes. */
struct nso_result guest_process_current_thread(guest_process *,uintptr_t *token);
struct nso_result guest_process_thread_id(guest_process *,uintptr_t token,uint64_t *tid);
#if defined(NSO_PROFILE_IDENTITY_TESTING)
/* Quiescent test setup only, separate binary; production has neither hook. */
void guest_process_test_identity_vm(void *(*map)(size_t),int (*unmap)(void *,size_t));
#endif
/* Limited SDK allocator-pair provider, separate from OS slots and ELF/TCB TLS.
 * Registration is once-only, nonnull, module-text callbacks; it does not call
 * either target. Native consumers require this actual primary and an IDLE
 * prepared native boundary. Allocate passes (bytes,16), free passes (ptr,bytes)
 * and null free does not call the guest. No HOST reentry/raw C cast is used.
 * Every successful allocation must be freed before primary return. A failed
 * or uncertain callback is retained, never retried as if no side effect ran;
 * live/uncertain records block module/pages destruction. These are logical
 * suballocation identities, not a second physical/backing charge.
 */
struct guest_tls_allocator_info {
    uint64_t allocate_callback,free_callback;
    size_t allocations,uncertain_allocations;
    int initialized,busy;
};
struct nso_result guest_process_register_tls_allocator(guest_process *,uint64_t allocate,uint64_t release);
struct nso_result guest_process_tls_allocator_info(guest_process *,struct guest_tls_allocator_info *out);
struct nso_result guest_process_allocate_thread_local(guest_process *,size_t bytes,void **out);
struct nso_result guest_process_free_thread_local(guest_process *,void *pointer,size_t bytes);
#if defined(NSO_PROFILE_TLS_ALLOCATOR_TESTING)
/* Separate test binary only. Production has neither fault injection nor hook. */
void guest_process_test_tls_allocator(unsigned metadata_failures,void (*query_probe)(guest_process *));
#endif
uint64_t guest_process_resolve(uint32_t,const char *);
/* Quiescent only. Failed release retains *process for retry/process shutdown. */
struct nso_result guest_process_destroy(guest_process **process);
#ifdef __cplusplus
}
#endif
#endif
