#define _GNU_SOURCE
#if !defined(__ANDROID__) && !(defined(NSO_WITH_CXX_GUARDS) && NSO_WITH_CXX_GUARDS)
#error "owned startup profile requires the real typed C++ guard runtime"
#endif
#include "guest_process.h"
#include "guest_context.h"
#include "guest_pages.h"
#include "exit_registry.h"
#include "nso_host.h"
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Explicit compiler runtime references, independent of another translation
 * unit's HLE feature flags. Never reach the extension fallback for guards. */
extern int __cxa_guard_acquire(uint64_t *);
extern void __cxa_guard_release(uint64_t *);
extern void __cxa_guard_abort(uint64_t *);

enum heap_state {HEAP_UNINITIALIZED,HEAP_READY_EMPTY};
struct empty_heap_manager {enum heap_state state;size_t capacity,blocks;};
enum thread_state {THREAD_ABSENT,THREAD_STARTING,THREAD_RUNNING,THREAD_RETURNED};
struct thread_record {enum thread_state state;pthread_t native;struct guest_primary_stack stack;};
struct guest_process {
    pthread_mutex_t mutex;
    guest_context *context; /* Private: no raw tickets/Context escape this owner. */
    guest_pages *pages; /* Sole VA/page/ticket authority. */
    nso_module *module;
    void *module_ticket;
    size_t module_bytes;
    int module_committed,module_ready,running,quarantined,diagnostic_retained;
    struct empty_heap_manager heap;
    struct thread_record thread;
    uint64_t baseline;
    guest_process_callback callback;
    void *argument;
    struct nso_result callback_status;
};
static pthread_mutex_t exclusive_mutex=PTHREAD_MUTEX_INITIALIZER;
static guest_process *exclusive_process;
static _Thread_local guest_process *current_process;
static struct nso_result status(enum nso_status s,int e,int cleanup) {return (struct nso_result){s,e,cleanup};}
static struct nso_result from_context(struct guest_context_result r) {
    enum nso_status s=NSO_OWNER_ERROR;
    if(r.status==GUEST_CONTEXT_OK)s=NSO_OK;
    else if(r.status==GUEST_CONTEXT_LIMIT)s=NSO_LIMIT;
    else if(r.status==GUEST_CONTEXT_INVALID)s=NSO_INVALID;
    else if(r.status==GUEST_CONTEXT_QUARANTINED)s=NSO_RETAINED;
    return status(s,r.native_error,r.cleanup_error);
}
static void page_diagnostic(const char *operation,uintptr_t address,size_t bytes,
        struct guest_pages_result r) {
    fprintf(stderr,"[page-owner-error] op=%s address=0x%" PRIxPTR " bytes=%zu status=%d native=%d context=(%d,%d,%d) ticket=(%d,%d,%d) vm=(%d,%d,%d,%d) rollback=(%d,%d,%d,%d) attempted=(%d,%d,%d) region=0x%x\n",
        operation,address,bytes,r.status,r.native_error,r.context.status,r.context.native_error,r.context.cleanup_error,
        r.ticket_cleanup.status,r.ticket_cleanup.native_error,r.ticket_cleanup.cleanup_error,
        r.vm.status,r.vm.detail,r.vm.native_errno,r.vm.restore_errno,
        r.rollback_vm.status,r.rollback_vm.detail,r.rollback_vm.native_errno,r.rollback_vm.restore_errno,
        r.vm_attempted,r.rollback_attempted,r.region_attempted,r.region_result);
}
static struct nso_result page_failure(struct guest_pages_result r) {
    int native=r.native_error?r.native_error:r.context.native_error?r.context.native_error:r.vm.native_errno;
    int cleanup=r.ticket_cleanup.native_error?r.ticket_cleanup.native_error:r.vm.restore_errno;
    if(!cleanup)cleanup=r.rollback_vm.native_errno?r.rollback_vm.native_errno:r.rollback_vm.restore_errno;
    return status(r.status==GUEST_PAGES_QUOTA?NSO_LIMIT:NSO_OWNER_ERROR,native,cleanup);
}
/* Lock order: process -> pages owner -> Context OR pages owner -> VA registry.
 * Module/primary ownership is stable during the executing primary snapshot.
 * The raw APIs are private/quiescent in this exclusive profile. Context never calls out while
 * holding its mutex. Loader owner callbacks execute under process lifecycle
 * lock; no accounting uses the existing unload observer. */
static struct nso_result reserve_module(void *owner,size_t bytes,void **out) {
    guest_process *p=owner;
    guest_charge_ticket *ticket=NULL;
    struct nso_result r=from_context(guest_charge_reserve(p->context,bytes,&ticket));
    if(r.status==NSO_OK) {p->module_ticket=ticket;p->module_bytes=bytes;*out=ticket;}
    return r;
}
static struct nso_result commit_module(void *owner,void *ticket) {
    guest_process *p=owner;
    if(ticket!=p->module_ticket)return status(NSO_OWNER_ERROR,EINVAL,0);
    struct nso_result r=from_context(guest_charge_commit(ticket));
    if(r.status==NSO_OK)p->module_committed=1;
    return r;
}
static struct nso_result drop_module(void *owner,void *ticket,int committed) {
    guest_process *p=owner;
    if(ticket!=p->module_ticket || committed!=p->module_committed)return status(NSO_OWNER_ERROR,EINVAL,0);
    struct nso_result r=from_context(committed?guest_charge_release(ticket):guest_charge_cancel(ticket));
    if(r.status==NSO_OK) {p->module_ticket=NULL;p->module_bytes=0;p->module_committed=0;}
    return r;
}
static struct nso_result cancel_module(void *owner,void *ticket) {return drop_module(owner,ticket,0);}
static struct nso_result release_module(void *owner,void *ticket) {return drop_module(owner,ticket,1);}
static const struct nso_owner_ops module_ops={reserve_module,commit_module,cancel_module,release_module};

struct nso_result guest_process_create(const struct guest_process_config *config,guest_process **out) {
    if(!out)return status(NSO_INVALID,EINVAL,0);
    *out=NULL;
    if(!config)return status(NSO_INVALID,EINVAL,0);
    pthread_mutex_lock(&exclusive_mutex);
    if(exclusive_process) {pthread_mutex_unlock(&exclusive_mutex);return status(NSO_INVALID,EBUSY,0);}
    guest_process *p=calloc(1,sizeof(*p));
    if(!p) {pthread_mutex_unlock(&exclusive_mutex);return status(NSO_OWNER_ERROR,ENOMEM,0);}
    int e=pthread_mutex_init(&p->mutex,NULL);
    if(e) {free(p);pthread_mutex_unlock(&exclusive_mutex);return status(NSO_OWNER_ERROR,e,0);}
    struct guest_context_config c={config->quota_bytes,config->primary_stack_bytes};
    struct nso_result r=from_context(guest_context_create(&c,&p->context));
    if(r.status==NSO_OK) {
        struct guest_pages_result created=guest_pages_create(p->context,&p->pages);
        if(created.status!=GUEST_PAGES_OK) {
            page_diagnostic("create",0,0,created);r=page_failure(created);
            struct nso_result cleanup=from_context(guest_context_destroy(p->context));
            if(cleanup.status!=NSO_OK) {p->quarantined=1;r=status(NSO_RETAINED,r.native_error,cleanup.native_error);}
            else p->context=NULL;
        }
    }
    if(r.status!=NSO_OK && r.status!=NSO_RETAINED) {pthread_mutex_destroy(&p->mutex);free(p);}
    else {exclusive_process=p;*out=p;}
    pthread_mutex_unlock(&exclusive_mutex);return r;
}
struct nso_result guest_process_load(guest_process *p,const char *path,char *err,size_t len) {
    if(!p)return status(NSO_INVALID,EINVAL,0);
    pthread_mutex_lock(&p->mutex);
    if(p->module || p->running || p->quarantined) {pthread_mutex_unlock(&p->mutex);return status(NSO_INVALID,EBUSY,0);}
    struct nso_load_result loaded=nso_load_file_owned(path,guest_process_resolve,&module_ops,p,err,len);
    p->module=loaded.module;
    if(loaded.result.status==NSO_RETAINED)p->quarantined=1;
    if(loaded.result.status==NSO_OK) {
        if(nso_host_exit_scope_begin(p->module)) {
            struct nso_result cleanup=nso_release(&p->module);
            loaded.result=status(cleanup.status==NSO_OK?NSO_OWNER_ERROR:NSO_RETAINED,EBUSY,cleanup.cleanup_error);
            if(cleanup.status!=NSO_OK)p->quarantined=1;
        } else {
            nso_set_unload_observer(p->module,nso_host_exit_scope_end,p->module);
            p->module_ready=1;
        }
    }
    pthread_mutex_unlock(&p->mutex);return loaded.result;
}
nso_module *guest_process_module(guest_process *p) {return p?p->module:NULL;}
/* A functional empty native heap, not a zero-filled SDK manager. It can be
 * initialized once, owns no backing and exposes NO allocator mutator capability.
 * Vamm page backing is separately owned; no heap/block allocator is admitted. */
static int initialize_empty_heap(struct empty_heap_manager *heap) {
    if(heap->state!=HEAP_UNINITIALIZED || heap->capacity || heap->blocks)return EINVAL;
    heap->state=HEAP_READY_EMPTY;return 0;
}
static struct nso_result snapshot_locked(guest_process *p,struct guest_memory_info *out) {
    if(current_process!=p || !p->running || p->quarantined || !p->module_ready ||
       !p->module || !p->module_ticket || !p->module_committed || !p->pages ||
       p->thread.state!=THREAD_RUNNING || !pthread_equal(p->thread.native,pthread_self()) ||
       p->heap.state!=HEAP_READY_EMPTY || p->heap.capacity || p->heap.blocks)
        return status(NSO_INVALID,EINVAL,0);
    struct guest_context_usage use;
    struct guest_primary_stack stack;
    struct guest_pages_usage pages;
    if(guest_pages_usage(p->pages,&pages).status!=GUEST_PAGES_OK ||
       guest_context_primary_stack(p->context,&stack).status!=GUEST_CONTEXT_OK)
        return status(NSO_INVALID,EINVAL,0);
    use=pages.context;
    if(!pages.backing_valid || !pages.ledger_valid || pages.quarantined_regions || pages.pending_bytes ||
       !pages.vm.mapped_guest_bytes_valid || pages.vm.quarantined_regions ||
       pages.ticket_count!=pages.allocation_count || pages.backing_bytes!=pages.charged_bytes ||
       !use.primary_active || use.quarantined || use.pending_bytes || pages.ticket_count>SIZE_MAX-2 ||
       use.tickets!=2+pages.ticket_count || p->module_bytes!=nso_image_size(p->module) ||
       p->module_bytes>SIZE_MAX-stack.bytes || pages.charged_bytes>SIZE_MAX-p->module_bytes-stack.bytes ||
       use.charged_bytes!=p->module_bytes+stack.bytes+pages.charged_bytes ||
       stack.address!=p->thread.stack.address || stack.bytes!=p->thread.stack.bytes)
        return status(NSO_INVALID,EINVAL,0);
    /* Private module/primary identities plus the sole page owner's validated
     * per-allocation tickets. Its locked snapshot includes Context and raw VM;
     * module/stack mutations are excluded during this primary callback. */
    if(!p->baseline)p->baseline=use.charged_bytes-p->heap.capacity-stack.bytes;
    *out=(struct guest_memory_info){use.quota_bytes,use.charged_bytes,p->heap.capacity,
        p->heap.blocks,p->baseline,stack.bytes,1};
    return status(NSO_OK,0,0);
}
struct nso_result guest_process_memory_info(guest_process *p,struct guest_memory_info *out) {
    if(!p || !out)return status(NSO_INVALID,EINVAL,0);
    struct guest_memory_info value;
    pthread_mutex_lock(&p->mutex);
    struct nso_result r=snapshot_locked(p,&value);
    if(r.status==NSO_OK) {
        _Static_assert(offsetof(struct guest_memory_info,thread_objects)==0x30,"verified Query count offset");
        memcpy(out,&value,0x34); /* Never write trailing +0x34..0x37 padding. */
    }
    pthread_mutex_unlock(&p->mutex);return r;
}
struct nso_result guest_process_primary_stack(guest_process *p,struct guest_primary_stack *out) {
    if(!p || !out)return status(NSO_INVALID,EINVAL,0);
    pthread_mutex_lock(&p->mutex);
    struct nso_result r=status(NSO_INVALID,EINVAL,0);
    if(current_process==p && p->running && !p->quarantined &&
       p->thread.state==THREAD_RUNNING && pthread_equal(p->thread.native,pthread_self())) {
        *out=p->thread.stack;r=status(NSO_OK,0,0);
    }
    pthread_mutex_unlock(&p->mutex);return r;
}
static void query_memory_info(struct guest_memory_info *out) {
    guest_process *p=current_process;
    uintptr_t caller=(uintptr_t)__builtin_return_address(0);
    struct nso_result r=guest_process_memory_info(p,out);
    if(r.status!=NSO_OK) {
        fprintf(stderr,"[query-invalid] native single-primary policy has incomplete/retained ownership status=%d errno=%d; output untouched\n",r.status,r.native_error);
        abort();
    }
    fprintf(stderr,"[query-port-policy] total=%" PRIu64 " used=%" PRIu64 " heap=%" PRIu64 "/%" PRIu64
        " baseline=%" PRIu64 " stacks=%" PRIu64 " thread_objects=%u valid=0x7f; not original SVC measurements\n",
        out->total,out->used,out->heap_capacity,out->heap_blocks,out->baseline,out->stack_bytes,out->thread_objects);
    uint64_t base=nso_base(p->module);
    if(caller>=base && caller-base<nso_image_size(p->module))
        fprintf(stderr,"[query-caller] original module return_offset=0x%" PRIx64 "\n",(uint64_t)caller-base);
}
static guest_process *required_current(void) {
    guest_process *p=current_process;
    if(!p) {fputs("[profile-invalid] service outside native primary scope\n",stderr);abort();}
    return p;
}
static uint32_t finish_page_service(guest_process *p,const char *operation,
        uintptr_t address,size_t bytes,struct guest_pages_result r) {
    if(r.status!=GUEST_PAGES_OK)p->quarantined=1;
    pthread_mutex_unlock(&p->mutex);
    if(r.status!=GUEST_PAGES_OK) {
        page_diagnostic(operation,address,bytes,r);
        fputs("[page-unsupported] no unproved SDK Result mapping; owner/tickets retained until isolated process shutdown\n",stderr);
        abort();
    }
    fprintf(stderr,"[page-port-policy] op=%s address=0x%" PRIxPTR " bytes=%zu result=0 after real VM/accounting transition\n",
        operation,address,bytes);
    return 0;
}
static uint32_t allocate_region(uintptr_t *out,size_t bytes) {
    guest_process *p=required_current();
    if(!out) {fputs("[profile-invalid] null reservation output\n",stderr);abort();}
    pthread_mutex_lock(&p->mutex);
    struct guest_pages_result r=guest_pages_reserve(p->pages,bytes,out);
    return finish_page_service(p,"reserve",r.status==GUEST_PAGES_OK?*out:0,bytes,r);
}
static uint32_t free_region(uintptr_t address) {
    guest_process *p=required_current();pthread_mutex_lock(&p->mutex);
    struct guest_pages_result r=guest_pages_release_region(p->pages,address);
    return finish_page_service(p,"release-region",address,0,r);
}
static uint32_t allocate_pages(uintptr_t address,size_t bytes) {
    guest_process *p=required_current();pthread_mutex_lock(&p->mutex);
    struct guest_pages_result r=guest_pages_map(p->pages,address,bytes);
    return finish_page_service(p,"map",address,bytes,r);
}
static uint32_t free_pages(uintptr_t address,size_t bytes) {
    guest_process *p=required_current();pthread_mutex_lock(&p->mutex);
    struct guest_pages_result r=guest_pages_free_exact(p->pages,address,bytes);
    return finish_page_service(p,"free-exact",address,bytes,r);
}
uint64_t guest_process_resolve(uint32_t index,const char *name) {
    if(!name)return 0;
#define PURE(fn) if(!strcmp(name,#fn))return (uint64_t)(uintptr_t)&fn
    PURE(memset);PURE(memcpy);PURE(memmove);PURE(memcmp);PURE(memchr);
    PURE(strlen);PURE(strnlen);PURE(strcmp);PURE(strncmp);PURE(strcpy);PURE(strncpy);
    PURE(strchr);PURE(strrchr);PURE(strstr);
#undef PURE
    if(!strcmp(name,"_ZN2nn2os15QueryMemoryInfoEPNS0_10MemoryInfoE"))return (uintptr_t)&query_memory_info;
    if(!strcmp(name,"_ZN2nn2os21AllocateAddressRegionEPmm"))return (uintptr_t)&allocate_region;
    if(!strcmp(name,"_ZN2nn2os17FreeAddressRegionEm"))return (uintptr_t)&free_region;
    if(!strcmp(name,"_ZN2nn2os19AllocateMemoryPagesEmm"))return (uintptr_t)&allocate_pages;
    if(!strcmp(name,"_ZN2nn2os15FreeMemoryPagesEmm"))return (uintptr_t)&free_pages;
    if(!strcmp(name,"__cxa_guard_acquire"))return (uintptr_t)&__cxa_guard_acquire;
    if(!strcmp(name,"__cxa_guard_release"))return (uintptr_t)&__cxa_guard_release;
    if(!strcmp(name,"__cxa_guard_abort"))return (uintptr_t)&__cxa_guard_abort;
    /* Every entry is an existing typed branch, never the extension fallthrough.
     * No nvn/dlsym/unowned backing/thread/heap service is admitted. */
    static const char *const typed[]={
        "_ZN2nn4util11ReferSymbolEPKv","nnutilReferSymbol",
        "_ZN2nn2os15InitializeMutexEPNS0_9MutexTypeEbi","nnosInitializeMutex",
        "_ZN2nn2os13FinalizeMutexEPNS0_9MutexTypeE","nnosFinalizeMutex",
        "_ZN2nn2os9LockMutexEPNS0_9MutexTypeE","nnosLockMutex",
        "_ZN2nn2os12TryLockMutexEPNS0_9MutexTypeE","nnosTryLockMutex",
        "_ZN2nn2os11UnlockMutexEPNS0_9MutexTypeE","nnosUnlockMutex",
        "_ZN2nn2os28IsMutexLockedByCurrentThreadEPKNS0_9MutexTypeE",
        "atexit","__cxa_atexit","__cxa_finalize","exit","_Exit","_exit",NULL};
    for(size_t i=0;typed[i];i++)if(!strcmp(name,typed[i]))return nso_host_resolve(index,name);
    if(!strcmp(name,"abort"))return (uintptr_t)&abort;
    return 0;
}
static void *invoke_primary(guest_context *context,void *opaque) {
    guest_process *p=opaque;
    struct guest_primary_stack stack;
    if(current_process || guest_context_primary_stack(context,&stack).status!=GUEST_CONTEXT_OK) {
        p->callback_status=status(NSO_OWNER_ERROR,EINVAL,0);return NULL;
    }
    current_process=p;
    pthread_mutex_lock(&p->mutex);
    p->thread=(struct thread_record){THREAD_RUNNING,pthread_self(),stack};
    if(p->heap.state==HEAP_UNINITIALIZED && initialize_empty_heap(&p->heap))p->quarantined=1;
    struct guest_memory_info initial;
    p->callback_status=snapshot_locked(p,&initial);
    pthread_mutex_unlock(&p->mutex);
    int e=0;
    void *value=NULL;
    if(p->callback_status.status==NSO_OK) {
        e=nso_fault_scope_begin(p->module);
        if(e) {
            int cleanup=nso_fault_scope_end(p->module);
            p->callback_status=status(cleanup?NSO_RETAINED:NSO_OWNER_ERROR,e,cleanup);
            if(cleanup)p->diagnostic_retained=1;
        }
        else {
            value=p->callback(p,p->argument);
            e=nso_fault_scope_end(p->module);
            if(e) {p->callback_status=status(NSO_RETAINED,0,e);p->diagnostic_retained=1;}
        }
    }
    pthread_mutex_lock(&p->mutex);p->thread.state=THREAD_RETURNED;
    pthread_mutex_unlock(&p->mutex);current_process=NULL;
    return value;
}
struct nso_result guest_process_run(guest_process *p,guest_process_callback callback,void *arg,void **value) {
    if(!p || !callback || !value)return status(NSO_INVALID,EINVAL,0);
    *value=NULL;
    pthread_mutex_lock(&p->mutex);
    if(!p->module_ready || p->quarantined || p->running || p->thread.state!=THREAD_ABSENT) {
        pthread_mutex_unlock(&p->mutex);return status(NSO_INVALID,EBUSY,0);
    }
    p->running=1;p->thread.state=THREAD_STARTING;p->callback=callback;p->argument=arg;
    p->callback_status=status(NSO_OK,0,0);
    pthread_mutex_unlock(&p->mutex);
    struct nso_result r=from_context(guest_context_run_primary(p->context,invoke_primary,p,value));
    pthread_mutex_lock(&p->mutex);
    if(r.status==NSO_RETAINED) {
        struct guest_context_usage retained;
        (void)guest_context_usage(p->context,&retained);
        p->quarantined=1;
        /* Failed join retains a live record/argument. A joined stack whose
         * munmap failed can instead be retried by Context destroy. */
        if(!retained.primary_active) {
            p->running=0;p->thread=(struct thread_record){0};p->callback=NULL;p->argument=NULL;
        }
    }
    else {
        if(r.status==NSO_OK)r=p->callback_status;
        p->running=0;p->thread=(struct thread_record){0};p->callback=NULL;p->argument=NULL;
        if(r.status==NSO_RETAINED)p->quarantined=1;
    }
    pthread_mutex_unlock(&p->mutex);return r;
}
struct nso_result guest_process_destroy(guest_process **process) {
    if(!process)return status(NSO_INVALID,EINVAL,0);
    guest_process *p=*process;if(!p)return status(NSO_OK,0,0);
    pthread_mutex_lock(&p->mutex);
    if(p->running || p->diagnostic_retained) {pthread_mutex_unlock(&p->mutex);return status(NSO_RETAINED,EBUSY,0);}
    p->module_ready=0;
    if(p->pages) {
        struct guest_pages_result released=guest_pages_destroy(p->pages);
        if(released.status!=GUEST_PAGES_OK) {
            page_diagnostic("destroy",0,0,released);p->quarantined=1;
            struct nso_result failure=page_failure(released);failure.status=NSO_RETAINED;
            pthread_mutex_unlock(&p->mutex);return failure;
        }
        p->pages=NULL;
    }
    struct nso_result r=nso_release(&p->module);
    if(r.status==NSO_OK)r=from_context(guest_context_destroy(p->context));
    if(r.status!=NSO_OK) {p->quarantined=1;pthread_mutex_unlock(&p->mutex);return r;}
    p->context=NULL;pthread_mutex_unlock(&p->mutex);
    pthread_mutex_lock(&exclusive_mutex);exclusive_process=NULL;pthread_mutex_unlock(&exclusive_mutex);
    pthread_mutex_destroy(&p->mutex);free(p);*process=NULL;return status(NSO_OK,0,0);
}
