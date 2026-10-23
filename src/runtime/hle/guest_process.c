#define _GNU_SOURCE
#if !defined(__ANDROID__) && !(defined(NSO_WITH_CXX_GUARDS) && NSO_WITH_CXX_GUARDS)
#error "owned startup profile requires the real typed C++ guard runtime"
#endif
#include "guest_process.h"
#include "guest_context.h"
#include "guest_pages.h"
#include "native_boundary.h"
#include "guest_os_tls.h"
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
    nb_scope boundary;
    nb_import *bindings;
    size_t bindings_count;
    int boundary_initialized,boundary_prepared,guest_session_ended,init_attempted;
    uint64_t guest_x18;
    guest_os_tls_manager *os_tls;
    guest_os_tls_thread *os_thread;
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
    if(r.status==NSO_OK) {
        struct guest_os_tls_result created=guest_os_tls_create(&p->os_tls);
        if(created.status!=GUEST_OS_TLS_OK) {
            r=status(NSO_OWNER_ERROR,created.native_error,0);
            struct guest_pages_result pages=guest_pages_destroy(p->pages);
            if(pages.status!=GUEST_PAGES_OK)r=status(NSO_RETAINED,r.native_error,EIO);
            else {
                p->pages=NULL;struct nso_result cleanup=from_context(guest_context_destroy(p->context));
                if(cleanup.status!=NSO_OK)r=status(NSO_RETAINED,r.native_error,cleanup.native_error);
                else p->context=NULL;
            }
            if(r.status==NSO_RETAINED)p->quarantined=1;
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
        nso_require_executor(p->module);
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
       !p->module || !p->module_ticket || !p->module_committed || !p->pages || !p->os_tls || !p->os_thread ||
       p->thread.state!=THREAD_RUNNING || !pthread_equal(p->thread.native,pthread_self()) ||
       p->heap.state!=HEAP_READY_EMPTY || p->heap.capacity || p->heap.blocks)
        return status(NSO_INVALID,EINVAL,0);
    struct guest_context_usage use;
    struct guest_primary_stack stack;
    struct guest_pages_usage pages;
    struct guest_os_tls_counts tls;
    if(guest_pages_usage(p->pages,&pages).status!=GUEST_PAGES_OK ||
       guest_os_tls_count(p->os_tls,&tls).status!=GUEST_OS_TLS_OK || tls.quarantined || tls.registered_threads!=1 ||
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
static guest_process *tls_current(void) {
    guest_process *p=required_current();
    if(!p->os_tls || !p->os_thread || !p->running || p->thread.state!=THREAD_RUNNING || p->quarantined) {
        fputs("[os-tls-invalid] no live actual-primary OS record\n",stderr);abort();
    }
    return p;
}
static void tls_failure(const char *operation,struct guest_os_tls_result r) {
    fprintf(stderr,"[os-tls-fatal] op=%s status=%d native=%d; no guessed SDK Result, owners retained until process shutdown\n",operation,r.status,r.native_error);abort();
}
static uint32_t tls_allocate(uint32_t *out,uint64_t destructor) {
    guest_process *p=tls_current();uint64_t ignored;
    if(!out || (destructor && (destructor<nso_base(p->module) ||
       nso_entry_address(p->module,destructor-nso_base(p->module),&ignored).status!=NSO_OK))) {
        fputs("[os-tls-fatal] unsupported output/callback identity; no registration\n",stderr);abort();
    }
    struct guest_os_tls_result r=guest_os_tls_allocate(p->os_tls,GUEST_OS_TLS_APP,destructor,out);
    if(r.status==GUEST_OS_TLS_EXHAUSTED)return 0x1203; /* Verified public APP exhausted result only. */
    if(r.status!=GUEST_OS_TLS_OK)tls_failure("allocate",r);
    fprintf(stderr,"[os-tls-allocate] app_slot=%u callback=0x%" PRIx64 " result=0; logical OS storage, not ThreadType/ELF TLS\n",*out,destructor);
    return 0;
}
static void tls_free(uint32_t slot) {
    guest_process *p=tls_current();struct guest_os_tls_result r=guest_os_tls_free(p->os_tls,slot);
    if(r.status!=GUEST_OS_TLS_OK)tls_failure("free",r);
}
static uint64_t tls_get(uint32_t slot) {
    guest_process *p=tls_current();uint64_t value=0;
    struct guest_os_tls_result r=guest_os_tls_get(p->os_tls,p->os_thread,slot,&value);
    if(r.status!=GUEST_OS_TLS_OK)tls_failure("get",r);
    return value;
}
static void tls_set(uint32_t slot,uint64_t value) {
    guest_process *p=tls_current();struct guest_os_tls_result r=guest_os_tls_set(p->os_tls,p->os_thread,slot,value);
    if(r.status!=GUEST_OS_TLS_OK)tls_failure("set",r);
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
    if(!strcmp(name,"_ZN2nn2os15AllocateTlsSlotEPNS0_7TlsSlotEPFvmE"))return (uintptr_t)&tls_allocate;
    if(!strcmp(name,"_ZN2nn2os11FreeTlsSlotENS0_7TlsSlotE"))return (uintptr_t)&tls_free;
    if(!strcmp(name,"_ZN2nn2os11GetTlsValueENS0_7TlsSlotE"))return (uintptr_t)&tls_get;
    if(!strcmp(name,"_ZN2nn2os11SetTlsValueENS0_7TlsSlotEm"))return (uintptr_t)&tls_set;
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
        /* Registration is safe; finalize/exit can invoke guest callbacks and
         * require a separately supported callback/reentry boundary. */
        "atexit","__cxa_atexit","_Exit","_exit",NULL};
    for(size_t i=0;typed[i];i++)if(!strcmp(name,typed[i]))return nso_host_resolve(index,name);
    if(!strcmp(name,"abort"))return (uintptr_t)&abort;
    return 0;
}
static int import_signature(const char *name,nb_signature *out) {
    unsigned arguments=0,returns=1;
    static const char *const one[]={"strlen","__cxa_guard_acquire","atexit","_ZN2nn2os11GetTlsValueENS0_7TlsSlotE",
        "_ZN2nn2os17FreeAddressRegionEm","_ZN2nn2os12TryLockMutexEPNS0_9MutexTypeE",
        "nnosTryLockMutex","_ZN2nn2os28IsMutexLockedByCurrentThreadEPKNS0_9MutexTypeE",NULL};
    static const char *const two[]={"strnlen","strcmp","strcpy","strchr","strrchr","strstr","_ZN2nn2os15AllocateTlsSlotEPNS0_7TlsSlotEPFvmE",
        "_ZN2nn2os21AllocateAddressRegionEPmm","_ZN2nn2os19AllocateMemoryPagesEmm","_ZN2nn2os15FreeMemoryPagesEmm",NULL};
    static const char *const three[]={"memset","memcpy","memmove","memcmp","memchr","strncmp","strncpy","__cxa_atexit",NULL};
    static const char *const void_one[]={"__cxa_guard_release","__cxa_guard_abort","_Exit","_exit","_ZN2nn2os11FreeTlsSlotENS0_7TlsSlotE",
        "_ZN2nn2os15QueryMemoryInfoEPNS0_10MemoryInfoE","_ZN2nn4util11ReferSymbolEPKv","nnutilReferSymbol",
        "_ZN2nn2os13FinalizeMutexEPNS0_9MutexTypeE","nnosFinalizeMutex",
        "_ZN2nn2os9LockMutexEPNS0_9MutexTypeE","nnosLockMutex",
        "_ZN2nn2os11UnlockMutexEPNS0_9MutexTypeE","nnosUnlockMutex",NULL};
    const char *const *lists[]={one,two,three,void_one};
    for(unsigned group=0;group<4;group++)for(size_t i=0;lists[group][i];i++)if(!strcmp(name,lists[group][i])) {
        arguments=group==3?1:group+1;returns=group==3?0:1;goto found;
    }
    if(!strcmp(name,"_ZN2nn2os15InitializeMutexEPNS0_9MutexTypeEbi") || !strcmp(name,"nnosInitializeMutex")) {arguments=3;returns=0;}
    else if(!strcmp(name,"_ZN2nn2os11SetTlsValueENS0_7TlsSlotEm")) {arguments=2;returns=0;}
    else if(!strcmp(name,"abort")) {arguments=0;returns=0;}
    else return 0;
found:
    *out=(nb_signature){.version=1,.flags=NB_SIG_REGISTER_ONLY,
        .gp_arguments=(uint8_t)arguments,.gp_returns=(uint8_t)returns};
    return 1;
}
static struct nso_result prepare_boundary_locked(guest_process *p) {
    if(p->guest_session_ended)return status(NSO_UNSUPPORTED,ENOTSUP,0);
    if(p->boundary_prepared)return status(NSO_OK,0,0);
    if(p->boundary_initialized || p->bindings)return status(NSO_RETAINED,EBUSY,0);
    size_t count=nso_bound_import_count(p->module);
    if(count>SIZE_MAX/sizeof(nb_import) || count>SIZE_MAX/sizeof(struct nso_import_target))return status(NSO_OWNER_ERROR,EOVERFLOW,0);
    struct nso_import_target *targets=count?calloc(count,sizeof(*targets)):NULL;
    nb_import *bindings=count?calloc(count,sizeof(*bindings)):NULL;
    if(count && (!targets || !bindings)) {free(targets);free(bindings);return status(NSO_OWNER_ERROR,ENOMEM,0);}
    for(size_t i=0;i<count;i++) {
        nb_signature signature;
        if(nso_bound_import_at(p->module,i,&targets[i]).status!=NSO_OK || !import_signature(targets[i].name,&signature)) {
            free(targets);free(bindings);return status(NSO_UNSUPPORTED,ENOTSUP,0);
        }
    }
    nb_status s=nb_scope_init(&p->boundary);
    if(s!=NB_OK) {free(targets);free(bindings);fprintf(stderr,"[boundary-error] scope-init status=%d; no guest execution\n",s);return status(s==NB_UNSUPPORTED?NSO_UNSUPPORTED:NSO_OWNER_ERROR,s==NB_UNSUPPORTED?ENOTSUP:EIO,0);}
    p->boundary_initialized=1;p->bindings=bindings;p->bindings_count=count;
    struct nso_result r=status(NSO_OK,0,0);
    for(size_t i=0;i<count;i++) {
        nb_signature signature;(void)import_signature(targets[i].name,&signature);
        s=nb_import_bind(&bindings[i],&p->boundary,(void(*)(void))(uintptr_t)targets[i].target,&signature);
        if(s!=NB_OK) {
            fprintf(stderr,"[boundary-error] bind index=%u name=%s status=%d mapping=%p entry=%p; ownership retained\n",targets[i].index,targets[i].name,s,bindings[i].mapping,(void *)bindings[i].entry);
            if(s==NB_RETAINED)fputs("[boundary-retained] non-callable RX rollback owner retained; no import retarget/publication\n",stderr);
            r=status(NSO_RETAINED,EIO,0);break;
        }
        targets[i].target=(uintptr_t)nb_import_entry(&bindings[i]);
    }
    if(r.status==NSO_OK)r=nso_retarget_imports(p->module,targets,count);
    free(targets);
    if(r.status!=NSO_OK) {p->quarantined=1;return status(NSO_RETAINED,r.native_error,r.cleanup_error);}
    p->boundary_prepared=1;
    fprintf(stderr,"[boundary-ready] imports=%zu actual-primary-owned; bindings retained through module unload\n",count);
    return r;
}
struct nso_result guest_process_call(guest_process *p,uint64_t off,const uint64_t arguments[6],unsigned count,uint64_t *value) {
    if(!p || !arguments || !value || count>6)return status(NSO_INVALID,EINVAL,0);
    pthread_mutex_lock(&p->mutex);
    uint64_t address;
    struct nso_result r=status(NSO_INVALID,EINVAL,0);
    if(current_process==p && p->running && !p->quarantined && p->thread.state==THREAD_RUNNING &&
       pthread_equal(p->thread.native,pthread_self()))r=nso_entry_address(p->module,off,&address);
    if(r.status==NSO_OK)r=prepare_boundary_locked(p);
    pthread_mutex_unlock(&p->mutex);
    if(r.status!=NSO_OK)return r;
    nb_registers packet={.guest_x18=p->guest_x18};
    for(unsigned i=0;i<count;i++)packet.x[i]=arguments[i];
    nb_status s=nb_call(&p->boundary,(void(*)(void))(uintptr_t)address,&packet);
    if(s!=NB_OK) {
        pthread_mutex_lock(&p->mutex);p->quarantined=1;pthread_mutex_unlock(&p->mutex);
        fprintf(stderr,"[boundary-error] call offset=0x%" PRIx64 " status=%d; no raw fallback; owner retained\n",off,s);return status(NSO_RETAINED,EIO,0);
    }
    p->guest_x18=packet.guest_x18;*value=packet.x[0];return status(NSO_OK,0,0);
}
static struct nso_result profile_executor(void *user,nso_module *module,uint64_t off,const uint64_t arguments[6],uint64_t *value) {
    guest_process *p=user;
    if(module!=p->module)return status(NSO_INVALID,EINVAL,0);
    return guest_process_call(p,off,arguments,0,value);
}
struct nso_result guest_process_initialize(guest_process *p,nso_init_policy policy) {
    if(!p || current_process!=p || (policy!=NSO_INIT_ELF && policy!=NSO_INIT_DT_INIT_OWNS_ARRAY))return status(NSO_INVALID,EINVAL,0);
    pthread_mutex_lock(&p->mutex);
    struct nso_result r=(p->init_attempted || p->quarantined || !p->running || p->thread.state!=THREAD_RUNNING)?status(NSO_INVALID,EBUSY,0):prepare_boundary_locked(p);
    if(r.status==NSO_OK)p->init_attempted=1;
    pthread_mutex_unlock(&p->mutex);
    return r.status==NSO_OK?nso_run_init_execute(p->module,policy,profile_executor,p):r;
}
static struct guest_os_tls_invoke_result invoke_os_destructor(guest_os_tls_manager *manager,
        guest_os_tls_thread *thread,uint64_t destructor,uint64_t value,void *opaque) {
    guest_process *p=opaque;
    if(current_process!=p || manager!=p->os_tls || thread!=p->os_thread || destructor<nso_base(p->module))
        return (struct guest_os_tls_invoke_result){GUEST_OS_TLS_INVOKE_FAILED,EINVAL};
    const uint64_t args[6]={value};uint64_t ignored;
    struct nso_result r=guest_process_call(p,destructor-nso_base(p->module),args,1,&ignored);
    return (struct guest_os_tls_invoke_result){r.status==NSO_OK?GUEST_OS_TLS_INVOKE_OK:GUEST_OS_TLS_INVOKE_FAILED,r.status};
}
static void cleanup_os_primary(guest_process *p) {
    struct guest_os_tls_cleanup_report report={0};
    struct guest_os_tls_result r=guest_os_tls_cleanup(p->os_tls,p->os_thread,invoke_os_destructor,p,&report);
    if(r.status!=GUEST_OS_TLS_OK) {
        /* Do not return to Context, mark ThreadReturned or refund its stack.
         * No recoverable stack-retention API exists in this limited runner. */
        fprintf(stderr,"[os-tls-provider-fatal] status=%d round=%u slot=%u callback=0x%" PRIx64 " value=0x%" PRIx64 " provider=(%d,%d); ThreadRecord/stack/module/bindings remain owned until process shutdown\n",
            r.status,report.failed_round,report.failed_slot,report.failed_destructor,report.failed_value,report.invocation.status,report.invocation.provider_error);
        abort();
    }
    fprintf(stderr,"[os-tls-cleanup] rounds=%u invocations=%zu remaining=%zu; actual primary still RUNNING\n",report.rounds_scanned,report.invocations,report.remaining_nonzero);
    r=guest_os_tls_unregister(p->os_tls,p->os_thread);
    if(r.status!=GUEST_OS_TLS_OK)tls_failure("unregister",r);
    p->os_thread=NULL;
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
    struct guest_os_tls_result registered=guest_os_tls_register(p->os_tls,&p->os_thread);
    if(p->heap.state==HEAP_UNINITIALIZED && initialize_empty_heap(&p->heap))p->quarantined=1;
    struct guest_memory_info initial;
    p->callback_status=registered.status==GUEST_OS_TLS_OK?snapshot_locked(p,&initial):status(NSO_OWNER_ERROR,registered.native_error,0);
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
            cleanup_os_primary(p);
            e=nso_fault_scope_end(p->module);
            if(e) {p->callback_status=status(NSO_RETAINED,0,e);p->diagnostic_retained=1;}
        }
    }
    if(p->os_thread)cleanup_os_primary(p);
    pthread_mutex_lock(&p->mutex);p->thread.state=THREAD_RETURNED;
    if(p->boundary_initialized)p->guest_session_ended=1;
    pthread_mutex_unlock(&p->mutex);current_process=NULL;
    return value;
}
struct nso_result guest_process_run(guest_process *p,guest_process_callback callback,void *arg,void **value) {
    if(!p || !callback || !value)return status(NSO_INVALID,EINVAL,0);
    *value=NULL;
    pthread_mutex_lock(&p->mutex);
    if(!p->module_ready || p->quarantined || p->running || p->thread.state!=THREAD_ABSENT || p->guest_session_ended) {
        pthread_mutex_unlock(&p->mutex);return status(p->guest_session_ended?NSO_UNSUPPORTED:NSO_INVALID,EBUSY,0);
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
    if(p->os_thread) {pthread_mutex_unlock(&p->mutex);return status(NSO_RETAINED,EBUSY,0);}
    p->module_ready=0;
    if(p->os_tls) {
        struct guest_os_tls_result released=guest_os_tls_destroy(p->os_tls);
        if(released.status!=GUEST_OS_TLS_OK) {pthread_mutex_unlock(&p->mutex);return status(NSO_RETAINED,released.native_error,0);}
        p->os_tls=NULL;
    }
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
    if(r.status==NSO_OK && p->boundary_initialized) {
        /* The image and unload observer are gone before any thunk can become
         * stale. Dead idle owner cleanup is explicitly supported by boundary. */
        for(size_t i=0;i<p->bindings_count;i++) {
            nb_status s=nb_import_unbind(&p->bindings[i]);
            if(s!=NB_OK) {fprintf(stderr,"[boundary-retained] unbind=%zu status=%d\n",i,s);r=status(NSO_RETAINED,0,EIO);break;}
        }
        if(r.status==NSO_OK) {
            nb_status s=nb_scope_close(&p->boundary);
            if(s!=NB_OK) {fprintf(stderr,"[boundary-retained] scope-close status=%d\n",s);r=status(NSO_RETAINED,0,EIO);}
            else {p->boundary_initialized=0;free(p->bindings);p->bindings=NULL;p->bindings_count=0;}
        }
    }
    if(r.status==NSO_OK)r=from_context(guest_context_destroy(p->context));
    if(r.status!=NSO_OK) {p->quarantined=1;pthread_mutex_unlock(&p->mutex);return r;}
    p->context=NULL;pthread_mutex_unlock(&p->mutex);
    pthread_mutex_lock(&exclusive_mutex);exclusive_process=NULL;pthread_mutex_unlock(&exclusive_mutex);
    pthread_mutex_destroy(&p->mutex);free(p);*process=NULL;return status(NSO_OK,0,0);
}
