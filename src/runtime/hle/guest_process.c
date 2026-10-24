#define _GNU_SOURCE
#if !defined(__ANDROID__) && !(defined(NSO_WITH_CXX_GUARDS) && NSO_WITH_CXX_GUARDS)
#error "owned startup profile requires the real typed C++ guard runtime"
#endif
#include "guest_process.h"
#include "guest_context.h"
#include "guest_pages.h"
#include "native_boundary.h"
#include "guest_os_tls.h"
#include "module_tls.h"
#include "exit_registry.h"
#include "guest_tp.h"
#include "nso_host.h"
#include <errno.h>
#include <ctype.h>
#include <wctype.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__ANDROID__)
#include <strings.h>
#endif
#include <sys/mman.h>
#include <unistd.h>
#if defined(__APPLE__)
static int guest_finite(double x) { return isfinite(x); }
#else
extern int finite(double);
#define guest_finite finite
#endif
/* Explicit compiler runtime references, independent of another translation
 * unit's HLE feature flags. Never reach the extension fallback for guards. */
extern int __cxa_guard_acquire(uint64_t *);
extern void __cxa_guard_release(uint64_t *);
extern void __cxa_guard_abort(uint64_t *);

enum heap_state {HEAP_UNINITIALIZED,HEAP_READY_EMPTY};
struct empty_heap_manager {enum heap_state state;size_t capacity,blocks;};
enum thread_state {THREAD_ABSENT,THREAD_STARTING,THREAD_RUNNING,THREAD_RETURNED};
struct thread_record {enum thread_state state;pthread_t native;struct guest_primary_stack stack;};
enum tls_allocation_state {TLS_ALLOCATION_LIVE,TLS_ALLOCATION_UNCERTAIN};
struct tls_allocation {
    struct tls_allocation *next;
    void *pointer;
    size_t bytes;
    enum tls_allocation_state state;
};
struct primary_identity {
    struct primary_identity *next;
    void *token;
    size_t bytes;
    uint64_t tid;
    int active;
};
struct guest_dso_record {
    uint64_t base;
    uint64_t text_token;
    uint64_t tls_start;
    size_t tls_init_size;
    size_t tls_total_size;
    size_t tls_alignment;
    uint64_t plt_got_start, plt_got_end;
    uint64_t rela_start, rela_end;
    uint64_t jmprel_start, jmprel_end;
    uint64_t dynamic;
    uint64_t raw_words[21];
    int registered;
    nso_module_tls_template tls_template;
    nso_module_tls_image *primary_tls_image;
};
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
    void *argument_mapping;
    size_t argument_bytes;
    guest_charge_ticket *argument_ticket;
    int argument_committed,argument_count;
    size_t resource_quota;
    uint64_t tls_allocate_callback,tls_free_callback;
    struct tls_allocation *tls_allocations;
    size_t tls_allocation_count;
    int tls_allocator_busy;
    struct primary_identity *identities,*active_identity;
    struct guest_dso_record dso;
};
static pthread_mutex_t exclusive_mutex=PTHREAD_MUTEX_INITIALIZER;
static guest_process *exclusive_process;
static _Thread_local guest_process *current_process;
static _Thread_local guest_os_tls_thread *current_os_thread;
static struct guest_os_tls_invoke_result invoke_os_destructor(guest_os_tls_manager *manager,
        guest_os_tls_thread *thread,uint64_t destructor,uint64_t value,void *opaque);
#if defined(NSO_PROFILE_IDENTITY_TESTING)
static void *(*identity_test_map)(size_t);
static int (*identity_test_unmap)(void *,size_t);
void guest_process_test_identity_vm(void *(*map)(size_t),int (*unmap)(void *,size_t)) {
    identity_test_map=map;identity_test_unmap=unmap;
}
#endif
#if defined(NSO_PROFILE_TLS_ALLOCATOR_TESTING)
static _Thread_local unsigned tls_metadata_failures;
static _Thread_local void (*tls_query_probe)(guest_process *);
void guest_process_test_tls_allocator(unsigned failures,void (*probe)(guest_process *)) {
    tls_metadata_failures=failures;tls_query_probe=probe;
}
#endif
static struct nso_result status(enum nso_status s,int e,int cleanup) {return (struct nso_result){s,e,cleanup};}
static struct nso_result from_context(struct guest_context_result r) {
    enum nso_status s=NSO_OWNER_ERROR;
    if(r.status==GUEST_CONTEXT_OK)s=NSO_OK;
    else if(r.status==GUEST_CONTEXT_LIMIT)s=NSO_LIMIT;
    else if(r.status==GUEST_CONTEXT_INVALID)s=NSO_INVALID;
    else if(r.status==GUEST_CONTEXT_QUARANTINED)s=NSO_RETAINED;
    return status(s,r.native_error,r.cleanup_error);
}
static int native_thread_id(uint64_t *out) {
#if defined(__ANDROID__)
    pid_t tid=gettid();if(tid<=0)return errno?errno:EINVAL;*out=(uint64_t)tid;return 0;
#elif defined(__APPLE__)
    uint64_t tid=0;int e=pthread_threadid_np(NULL,&tid);if(e)return e;
    if(!tid)return EINVAL;*out=tid;return 0;
#else
    (void)out;return ENOTSUP;
#endif
}
static int unmap_identity(void *token,size_t bytes) {
#if defined(NSO_PROFILE_IDENTITY_TESTING)
    if(identity_test_unmap)return identity_test_unmap(token,bytes);
#endif
    return munmap(token,bytes);
}
/* Process lock held. Allocate the ownership record before mmap and publish
 * only after a real native TID and protected page have both been obtained. */
static struct nso_result create_identity_locked(guest_process *p) {
    uint64_t tid;int e=native_thread_id(&tid);
    if(e)return status(NSO_OWNER_ERROR,e,0);
    long page=sysconf(_SC_PAGESIZE);
    if(page<=0 || ((size_t)page&((size_t)page-1)) || p->active_identity)return status(NSO_OWNER_ERROR,EINVAL,0);
    struct primary_identity *id=calloc(1,sizeof(*id));
    if(!id)return status(NSO_OWNER_ERROR,ENOMEM,0);
    void *token;
#if defined(NSO_PROFILE_IDENTITY_TESTING)
    if(identity_test_map)token=identity_test_map((size_t)page);
    else
#endif
    token=mmap(NULL,(size_t)page,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if(token==MAP_FAILED) {e=errno;free(id);return status(NSO_OWNER_ERROR,e,0);}
    id->token=token;id->bytes=(size_t)page;id->tid=tid;
    id->next=p->identities;p->identities=id;
    if(!token) {
        /* A zero-address owner is retained rather than published as NULL. */
        if(unmap_identity(token,id->bytes)) {p->quarantined=1;return status(NSO_RETAINED,EINVAL,errno);}
        p->identities=id->next;free(id);return status(NSO_OWNER_ERROR,EINVAL,0);
    }
    id->active=1;p->active_identity=id;
    return status(NSO_OK,0,0);
}
static int identity_live_locked(guest_process *p) {
    return current_process==p && p->running && !p->quarantined && p->module_ready && p->module &&
        p->thread.state==THREAD_RUNNING && pthread_equal(p->thread.native,pthread_self()) &&
        p->active_identity && p->active_identity->active;
}
struct nso_result guest_process_current_thread(guest_process *p,uintptr_t *token) {
    if(!p || !token)return status(NSO_INVALID,EINVAL,0);
    pthread_mutex_lock(&p->mutex);uint64_t tid=0;struct nso_result r=status(NSO_INVALID,EINVAL,0);
    if(identity_live_locked(p)) {
        int e=native_thread_id(&tid);
        if(e)r=status(NSO_OWNER_ERROR,e,0);
        else if(tid==p->active_identity->tid) {*token=(uintptr_t)p->active_identity->token;r=status(NSO_OK,0,0);}
    }
    pthread_mutex_unlock(&p->mutex);return r;
}
struct nso_result guest_process_thread_id(guest_process *p,uintptr_t token,uint64_t *tid) {
    if(!p || !tid)return status(NSO_INVALID,EINVAL,0);
    pthread_mutex_lock(&p->mutex);uint64_t current=0;struct nso_result r=status(NSO_INVALID,EINVAL,0);
    if(identity_live_locked(p) && token==(uintptr_t)p->active_identity->token) {
        int e=native_thread_id(&current);
        if(e)r=status(NSO_OWNER_ERROR,e,0);
        else if(current==p->active_identity->tid) {*tid=current;r=status(NSO_OK,0,0);}
    }
    pthread_mutex_unlock(&p->mutex);return r;
}
static uintptr_t current_thread_token(void) {
    uintptr_t token;struct nso_result r=guest_process_current_thread(current_process,&token);
    if(r.status==NSO_OK) return token;
    return (uintptr_t)nso_host_get_current_thread();
}
static uint64_t current_thread_tid(uintptr_t token) {
    uint64_t tid;struct nso_result r=guest_process_thread_id(current_process,token,&tid);
    if(r.status==NSO_OK) return tid;
    return nso_host_get_thread_id((const void *)token);
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
/* Lock order: process -> pages owner -> VA registry -> terminal Context, or
 * process -> pages owner -> Context. Context never reenters either owner.
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

static struct nso_result create_base(const struct guest_process_config *config,size_t resources,guest_process **out) {
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
    struct nso_result r=from_context(resources?guest_context_create_with_resources(&c,resources,&p->context):guest_context_create(&c,&p->context));
    if(r.status==NSO_OK)p->resource_quota=resources;
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
#if defined(NSO_PROFILE_ARGUMENT_TESTING)
static _Thread_local void *(*argument_test_map)(size_t,size_t);
static _Thread_local int (*argument_test_unmap)(void *,size_t);
void guest_process_test_argument_vm(void *(*map)(size_t,size_t),int (*unmap)(void *,size_t)) {
    argument_test_map=map;argument_test_unmap=unmap;
}
#endif
static void *map_arguments(guest_process *p,size_t bytes) {
#if defined(NSO_PROFILE_ARGUMENT_TESTING)
    if(argument_test_map) {
        struct guest_context_usage usage;
        if(guest_context_usage(p->context,&usage).status!=GUEST_CONTEXT_OK) {errno=EINVAL;return MAP_FAILED;}
        return argument_test_map(bytes,usage.pending_bytes);
    }
#else
    (void)p;
#endif
    return mmap(NULL,bytes,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
}
static int unmap_arguments(void *mapping,size_t bytes) {
#if defined(NSO_PROFILE_ARGUMENT_TESTING)
    if(argument_test_unmap)return argument_test_unmap(mapping,bytes);
#endif
    return munmap(mapping,bytes);
}
static struct nso_result copy_arguments(guest_process *p,size_t argc,const char *const argv[]) {
    if(argc>INT_MAX || (argc && !argv) || argc>SIZE_MAX/sizeof(char *)-1)return status(NSO_INVALID,EINVAL,0);
    struct argument_source {const char *text;size_t bytes;};
    if(argc>SIZE_MAX/sizeof(struct argument_source))return status(NSO_INVALID,EOVERFLOW,0);
    struct argument_source *source=argc?calloc(argc,sizeof(*source)):NULL;
    if(argc && !source)return status(NSO_OWNER_ERROR,ENOMEM,0);
    struct nso_result r=status(NSO_INVALID,EOVERFLOW,0);
    size_t vector_bytes=(argc+1)*sizeof(char *),total=vector_bytes;
    for(size_t i=0;i<argc;i++) {
        if(!argv[i]) {r=status(NSO_INVALID,EINVAL,0);goto done;}
        source[i].text=argv[i];size_t n=strlen(source[i].text);
        if(n==SIZE_MAX || n+1>SIZE_MAX-total)goto done;
        source[i].bytes=n+1;
        total+=n+1;
    }
    long native_page=sysconf(_SC_PAGESIZE);
    if(native_page<=0 || ((size_t)native_page&((size_t)native_page-1)) || total>SIZE_MAX-((size_t)native_page-1))goto done;
    size_t bytes=(total+(size_t)native_page-1)&~((size_t)native_page-1);
    r=from_context(guest_charge_reserve(p->context,bytes,&p->argument_ticket));
    if(r.status!=NSO_OK)goto done;
    p->argument_bytes=bytes;
    void *mapping=map_arguments(p,bytes);
    if(mapping==MAP_FAILED) {r=status(NSO_OWNER_ERROR,errno,0);goto done;}
    p->argument_mapping=mapping;
    char **vector=mapping;char *cursor=(char *)mapping+vector_bytes;
    for(size_t i=0;i<argc;i++) {
        size_t n=source[i].bytes;vector[i]=cursor;memcpy(cursor,source[i].text,n);cursor+=n;
    }
    vector[argc]=NULL;
    r=from_context(guest_charge_commit(p->argument_ticket));
    if(r.status==NSO_OK) {p->argument_committed=1;p->argument_count=(int)argc;}
done:
    free(source);
    return r;
}
static struct nso_result create_process(const struct guest_process_config *config,size_t resources,
        size_t argc,const char *const argv[],guest_process **out) {
    if(!out)return status(NSO_INVALID,EINVAL,0);
    *out=NULL;
    guest_process *p=NULL;struct nso_result r=create_base(config,resources,&p);
    if(r.status!=NSO_OK) {*out=p;return r;}
    r=copy_arguments(p,argc,argv);
    if(r.status==NSO_OK) {*out=p;return r;}
    struct nso_result cleanup=guest_process_destroy(&p);
    if(cleanup.status!=NSO_OK) {*out=p;return status(NSO_RETAINED,r.native_error,cleanup.cleanup_error?cleanup.cleanup_error:cleanup.native_error);}
    return r;
}
struct nso_result guest_process_create_with_arguments(const struct guest_process_config *config,
        size_t argc,const char *const argv[],guest_process **out) {
    return create_process(config,0,argc,argv,out);
}
struct nso_result guest_process_create_with_resources(const struct guest_process_config *config,
        size_t resources,size_t argc,const char *const argv[],guest_process **out) {
    if(!resources) {if(out)*out=NULL;return status(NSO_INVALID,EINVAL,0);}
    return create_process(config,resources,argc,argv,out);
}
struct nso_result guest_process_create(const struct guest_process_config *config,guest_process **out) {
    return guest_process_create_with_arguments(config,0,NULL,out);
}
static void guest_process_text_filter(void *text, size_t bytes, void *opaque) {
    (void)opaque;
    struct guest_tp_patch_report report;
    guest_tp_patch_text(text, bytes, &report);
    guest_tp_register_text(text, bytes);
    if(report.patched > 0) {
        fprintf(stderr,"[guest-tp] patched %zu MRS TPIDR_EL0 sites in text\n",report.patched);
    }
}
struct nso_result guest_process_load(guest_process *p,const char *path,char *err,size_t len) {
    if(!p)return status(NSO_INVALID,EINVAL,0);
    pthread_mutex_lock(&p->mutex);
    if(p->module || p->running || p->quarantined) {pthread_mutex_unlock(&p->mutex);return status(NSO_INVALID,EBUSY,0);}
    nso_set_text_filter(guest_process_text_filter, p);
    struct nso_load_result loaded=nso_load_file_owned(path,guest_process_resolve,&module_ops,p,err,len);
    nso_set_text_filter(NULL, NULL);
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
static struct nso_result snapshot_locked(guest_process *p,struct guest_memory_info *out,struct guest_resource_usage *resources,struct guest_pages_usage *page_snapshot) {
    if(current_process!=p || !p->running || p->quarantined || !p->module_ready ||
       !p->module || !p->module_ticket || !p->module_committed || !p->pages || !p->os_tls || !p->os_thread ||
       !p->argument_mapping || !p->argument_ticket || !p->argument_committed || !p->argument_bytes ||
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
       !use.primary_active || use.quarantined || use.pending_bytes || pages.ticket_count>SIZE_MAX-3 ||
       use.tickets!=3+pages.ticket_count || p->module_bytes!=nso_image_size(p->module) ||
       p->module_bytes>SIZE_MAX-stack.bytes || pages.charged_bytes>SIZE_MAX-p->module_bytes-stack.bytes ||
       p->argument_bytes>SIZE_MAX-p->module_bytes-stack.bytes-pages.charged_bytes ||
       use.charged_bytes!=p->module_bytes+stack.bytes+pages.charged_bytes+p->argument_bytes ||
       stack.address!=p->thread.stack.address || stack.bytes!=p->thread.stack.bytes)
        return status(NSO_INVALID,EINVAL,0);
    /* Private module/primary identities plus the sole page owner's validated
     * per-allocation tickets. Its locked snapshot includes Context and raw VM;
     * module/stack mutations are excluded during this primary callback. */
    if(p->resource_quota && (!pages.resources.valid || pages.resources.total_bytes!=p->resource_quota))
        return status(NSO_INVALID,EINVAL,0);
    if(resources)*resources=pages.resources;
    if(page_snapshot)*page_snapshot=pages;
    if(!p->baseline)p->baseline=use.charged_bytes-p->heap.capacity-stack.bytes;
    *out=(struct guest_memory_info){use.quota_bytes,use.charged_bytes,p->heap.capacity,
        p->heap.blocks,p->baseline,stack.bytes,1};
    return status(NSO_OK,0,0);
}
struct nso_result guest_process_memory_info(guest_process *p,struct guest_memory_info *out) {
    if(!p || !out)return status(NSO_INVALID,EINVAL,0);
    struct guest_memory_info value;
    pthread_mutex_lock(&p->mutex);
    struct nso_result r=snapshot_locked(p,&value,NULL,NULL);
    if(r.status==NSO_OK) {
        _Static_assert(offsetof(struct guest_memory_info,thread_objects)==0x30,"verified Query count offset");
        memcpy(out,&value,0x34); /* Never write trailing +0x34..0x37 padding. */
    }
    pthread_mutex_unlock(&p->mutex);return r;
}
struct nso_result guest_process_resource_info(guest_process *p,struct guest_virtual_resource_usage *out) {
    if(!p || !out)return status(NSO_INVALID,EINVAL,0);
    pthread_mutex_lock(&p->mutex);
    struct guest_memory_info info;struct guest_resource_usage resource={0};
    struct nso_result r=p->resource_quota?snapshot_locked(p,&info,&resource,NULL):status(NSO_UNSUPPORTED,0,0);
    if(r.status==NSO_OK) {
        if(!resource.valid || resource.used_bytes>resource.total_bytes)r=status(NSO_INVALID,EINVAL,0);
        else {
            uint64_t remaining=resource.total_bytes-resource.used_bytes;
            uint64_t margin=remaining<UINT64_C(0x80000)?remaining:UINT64_C(0x80000);
            *out=(struct guest_virtual_resource_usage){resource.total_bytes,resource.used_bytes+margin};
            fprintf(stderr,"[va-resource-policy] total=%zu raw_metadata_used=%zu blocks=%zu reported_used=%" PRIu64 " SDK_margin=0x80000; logical node pool, not Horizon page tables/RSS\n",
                resource.total_bytes,resource.used_bytes,resource.blocks,out->used);
        }
    }
    pthread_mutex_unlock(&p->mutex);return r;
}
struct nso_result guest_process_va_enabled(guest_process *p,int *out) {
    if(!p || !out)return status(NSO_INVALID,EINVAL,0);
    pthread_mutex_lock(&p->mutex);
    struct guest_memory_info info;struct guest_resource_usage resource={0};struct guest_pages_usage pages;
    struct nso_result r=snapshot_locked(p,&info,&resource,&pages);
    if(r.status==NSO_OK) {
        /* SDK checks successful Info16 and nonzero total, not free quota or an
         * already allocated guest range. A live empty owner is a valid state.
         * Our admitted functionality is only the existing native VA/page owner. */
        *out=p->resource_quota && resource.valid && resource.total_bytes!=0;
        fprintf(stderr,"[va-capability-policy] enabled=%d live_owner=true regions=%zu allocations=%zu resource_total=%zu; native owned reserve/map/exact-free only, not full Horizon VM\n",
            *out,pages.region_count,pages.allocation_count,resource.total_bytes);
    }
    pthread_mutex_unlock(&p->mutex);return r;
}
static uint32_t virtual_address_enabled(void) {
    int enabled=0;struct nso_result r=guest_process_va_enabled(current_process,&enabled);
    if(r.status!=NSO_OK) {fprintf(stderr,"[va-capability-invalid] status=%d native=%d; no capability answer\n",r.status,r.native_error);abort();}
    return (uint32_t)enabled;
}
static __uint128_t virtual_resource_usage(void) {
    struct guest_virtual_resource_usage value;
    struct nso_result r=guest_process_resource_info(current_process,&value);
    if(r.status!=NSO_OK) {fprintf(stderr,"[va-resource-invalid] status=%d native=%d; no valid resource return\n",r.status,r.native_error);abort();}
    /* Exact known little-endian AArch64 x0/x1 integer-pair ABI. */
    return ((__uint128_t)value.used<<64)|value.total;
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
#if defined(NSO_PROFILE_TLS_ALLOCATOR_TESTING)
    if(tls_query_probe)tls_query_probe(p);
#endif
    fprintf(stderr,"[query-port-policy] total=%" PRIu64 " used=%" PRIu64 " heap=%" PRIu64 "/%" PRIu64
        " baseline=%" PRIu64 " stacks=%" PRIu64 " thread_objects=%u valid=0x7f; not original SVC measurements\n",
        out->total,out->used,out->heap_capacity,out->heap_blocks,out->baseline,out->stack_bytes,out->thread_objects);
    uint64_t base=nso_base(p->module);
    if(caller>=base && caller-base<nso_image_size(p->module))
        fprintf(stderr,"[query-caller] original module return_offset=0x%" PRIx64 "\n",(uint64_t)caller-base);
}
static guest_process *required_current(void) {
    guest_process *p=current_process?current_process:exclusive_process;
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
    guest_os_tls_thread *t=current_os_thread?current_os_thread:p->os_thread;
    struct guest_os_tls_result r=guest_os_tls_get(p->os_tls,t,slot,&value);
    if(r.status!=GUEST_OS_TLS_OK)tls_failure("get",r);
    return value;
}
static void tls_set(uint32_t slot,uint64_t value) {
    guest_process *p=tls_current();
    guest_os_tls_thread *t=current_os_thread?current_os_thread:p->os_thread;
    struct guest_os_tls_result r=guest_os_tls_set(p->os_tls,t,slot,value);
    if(r.status!=GUEST_OS_TLS_OK)tls_failure("set",r);
}
static int tls_primary_locked(guest_process *p) {
    return current_process==p && p->module_ready && p->running && !p->quarantined &&
        p->thread.state==THREAD_RUNNING && pthread_equal(p->thread.native,pthread_self());
}
struct nso_result guest_process_register_tls_allocator(guest_process *p,uint64_t allocate,uint64_t release) {
    if(!p || !allocate || !release)return status(NSO_INVALID,EINVAL,0);
    pthread_mutex_lock(&p->mutex);uint64_t ignored;struct nso_result r=status(NSO_INVALID,EINVAL,0);
    if(tls_primary_locked(p) && p->module && !p->tls_allocate_callback && !p->tls_free_callback) {
        /* A valid created Process need not have a module yet. Reject that
         * lifecycle before consulting the loader's nonnull module accessor. */
        uint64_t base=nso_base(p->module);
        if(allocate>=base && release>=base &&
           nso_entry_address(p->module,allocate-base,&ignored).status==NSO_OK &&
           nso_entry_address(p->module,release-base,&ignored).status==NSO_OK) {
            p->tls_allocate_callback=allocate;p->tls_free_callback=release;r=status(NSO_OK,0,0);
        }
    }
    pthread_mutex_unlock(&p->mutex);return r;
}
struct nso_result guest_process_tls_allocator_info(guest_process *p,struct guest_tls_allocator_info *out) {
    if(!p || !out)return status(NSO_INVALID,EINVAL,0);
    pthread_mutex_lock(&p->mutex);
    struct guest_tls_allocator_info value={p->tls_allocate_callback,p->tls_free_callback,
        p->tls_allocation_count,0,p->tls_allocate_callback!=0,p->tls_allocator_busy};
    for(struct tls_allocation *a=p->tls_allocations;a;a=a->next)
        if(a->state==TLS_ALLOCATION_UNCERTAIN)value.uncertain_allocations++;
    *out=value;pthread_mutex_unlock(&p->mutex);return status(NSO_OK,0,0);
}
/* Actual-primary control-plane IDLE or an existing import's HOST phase only.
 * Never reset the phase or bind a new scope while a guest frame is active. */
static struct nso_result tls_consumer_locked(guest_process *p,int needs_boundary) {
    if(!tls_primary_locked(p))return status(NSO_INVALID,EINVAL,0);
    uint64_t phase=__atomic_load_n(&p->boundary.phase,__ATOMIC_ACQUIRE);
    if((needs_boundary && !p->boundary_prepared) || (phase!=0 && phase!=2))
        return status(NSO_UNSUPPORTED,ENOTSUP,0);
    if(p->tls_allocator_busy)return status(NSO_UNSUPPORTED,EBUSY,0);
    return status(NSO_OK,0,0);
}
static struct nso_result call_tls_allocator(guest_process *p,uint64_t callback,const uint64_t args[6],uint64_t *out) {
    /* The allocator operation owns the live module and busy flag across this
     * synchronous call, but holds no Process lock: Query/page imports reenter. */
    uint64_t phase=__atomic_load_n(&p->boundary.phase,__ATOMIC_ACQUIRE);
    if(!phase)return guest_process_call(p,callback-nso_base(p->module),args,2,out);
    if(phase!=2)return status(NSO_UNSUPPORTED,ENOTSUP,0);
    uint64_t address;struct nso_result r=nso_entry_address(p->module,callback-nso_base(p->module),&address);
    if(r.status!=NSO_OK)return r;
    nb_registers packet={.guest_x18=p->boundary.active_guest_x18};
    packet.x[0]=args[0];packet.x[1]=args[1];
    nb_status s=nb_call_from_host(&p->boundary,(void(*)(void))(uintptr_t)address,&packet);
    if(s!=NB_OK) {
        fprintf(stderr,"[tls-host-callback-retained] status=%d offset=0x%" PRIx64 "; no raw fallback/retry\n",s,callback-nso_base(p->module));
        return status(NSO_RETAINED,EIO,0);
    }
    /* The boundary restores the outer HOST scope and its active guest x18.
     * The callback's B/C x18 is observational output, NOT Process's outer A. */
    *out=packet.x[0];return status(NSO_OK,0,0);
}
struct nso_result guest_process_allocate_thread_local(guest_process *p,size_t bytes,void **out) {
    if(!p || !out)return status(NSO_INVALID,EINVAL,0);
    pthread_mutex_lock(&p->mutex);
    struct nso_result r=tls_consumer_locked(p,1);
    if(r.status==NSO_OK && !p->tls_allocate_callback)r=status(NSO_UNSUPPORTED,ENOENT,0);
    if(r.status==NSO_OK && p->tls_allocation_count==SIZE_MAX)r=status(NSO_OWNER_ERROR,EOVERFLOW,0);
    struct tls_allocation *record=NULL;
    if(r.status==NSO_OK) {
#if defined(NSO_PROFILE_TLS_ALLOCATOR_TESTING)
        if(tls_metadata_failures)tls_metadata_failures--;
        else
#endif
        record=calloc(1,sizeof(*record)); /* Host administrative metadata, before guest side effects. */
        if(!record)r=status(NSO_OWNER_ERROR,ENOMEM,0);
    }
    if(r.status!=NSO_OK) {pthread_mutex_unlock(&p->mutex);return r;}
    uint64_t callback=p->tls_allocate_callback;
    record->bytes=bytes;record->state=TLS_ALLOCATION_UNCERTAIN;
    record->next=p->tls_allocations;p->tls_allocations=record;p->tls_allocation_count++;
    p->tls_allocator_busy=1;
    pthread_mutex_unlock(&p->mutex);
    const uint64_t args[6]={bytes,16};uint64_t pointer=0;
    r=call_tls_allocator(p,callback,args,&pointer);
    pthread_mutex_lock(&p->mutex);p->tls_allocator_busy=0;
    if(r.status==NSO_OK && !pointer) {
        p->tls_allocations=record->next;p->tls_allocation_count--;
        free(record);pthread_mutex_unlock(&p->mutex);return status(NSO_LIMIT,ENOMEM,0);
    }
    record->pointer=(void *)(uintptr_t)pointer;record->bytes=bytes;
    int duplicate=0;
    for(struct tls_allocation *a=p->tls_allocations;a;a=a->next)
        if(a!=record && pointer && a->pointer==record->pointer)duplicate=1;
    record->state=r.status==NSO_OK && !(pointer&15) && !duplicate?TLS_ALLOCATION_LIVE:TLS_ALLOCATION_UNCERTAIN;
    if(record->state!=TLS_ALLOCATION_LIVE) {
        /* The callback may have allocated already. Keep even an unknown ptr,
         * code and owner; no host free, automatic retry or successful unload. */
        p->quarantined=1;r=status(NSO_RETAINED,r.native_error?r.native_error:EINVAL,r.cleanup_error);
    }
    else {*out=record->pointer;r=status(NSO_OK,0,0);}
    pthread_mutex_unlock(&p->mutex);return r;
}
struct nso_result guest_process_free_thread_local(guest_process *p,void *pointer,size_t bytes) {
    if(!p)return status(NSO_INVALID,EINVAL,0);
    pthread_mutex_lock(&p->mutex);struct nso_result r=tls_consumer_locked(p,pointer!=NULL);
    if(r.status!=NSO_OK || !pointer) {pthread_mutex_unlock(&p->mutex);return r;}
    struct tls_allocation **link=&p->tls_allocations;
    while(*link && (*link)->pointer!=pointer)link=&(*link)->next;
    struct tls_allocation *record=*link;
    if(!record || record->bytes!=bytes || record->state!=TLS_ALLOCATION_LIVE || !p->tls_free_callback) {
        pthread_mutex_unlock(&p->mutex);return status(NSO_UNSUPPORTED,EINVAL,0);
    }
    uint64_t callback=p->tls_free_callback;p->tls_allocator_busy=1;
    pthread_mutex_unlock(&p->mutex);
    const uint64_t args[6]={(uintptr_t)pointer,bytes};uint64_t ignored;
    r=call_tls_allocator(p,callback,args,&ignored);
    pthread_mutex_lock(&p->mutex);p->tls_allocator_busy=0;
    if(r.status==NSO_OK) {*link=record->next;p->tls_allocation_count--;free(record);}
    else {record->state=TLS_ALLOCATION_UNCERTAIN;p->quarantined=1;r.status=NSO_RETAINED;}
    pthread_mutex_unlock(&p->mutex);return r;
}
static void set_tls_allocator(uint64_t allocate,uint64_t release) {
    struct nso_result r=guest_process_register_tls_allocator(required_current(),allocate,release);
    if(r.status!=NSO_OK) {fprintf(stderr,"[tls-allocator-invalid] once-only nonnull module callbacks required status=%d; pair unchanged\n",r.status);abort();}
    fprintf(stderr,"[tls-allocator-register] allocate=0x%" PRIx64 " free=0x%" PRIx64 "; retained module callbacks, no immediate allocation; ELF/TCB TLS not initialized\n",allocate,release);
}
static uint32_t tls_allocator_initialized(void) {
    struct guest_tls_allocator_info info;
    if(guest_process_tls_allocator_info(required_current(),&info).status!=NSO_OK)abort();
    return (uint32_t)info.initialized;
}
static void *sdk_allocate_thread_local(size_t bytes) {
    void *pointer=NULL;struct nso_result r=guest_process_allocate_thread_local(required_current(),bytes,&pointer);
    if(r.status==NSO_OK)return pointer;
    if(r.status==NSO_LIMIT && r.native_error==ENOMEM)return NULL; /* Actual guest callback returned NULL. */
    fprintf(stderr,"[tls-host-consumer-fatal] allocate bytes=%zu status=%d native=%d; no guessed SDK success, owners retained\n",bytes,r.status,r.native_error);abort();
}
static void sdk_free_thread_local(void *pointer,size_t bytes) {
    if(!pointer)return; /* SDK144650 skips even an uninitialized allocator. */
    struct nso_result r=guest_process_free_thread_local(required_current(),pointer,bytes);
    if(r.status!=NSO_OK) {fprintf(stderr,"[tls-host-consumer-fatal] free pointer=%p bytes=%zu status=%d native=%d; owners retained\n",pointer,bytes,r.status,r.native_error);abort();}
}
/* SDK hash-matched strncasecmp_l ignores x3 and folds unsigned ASCII only.
 * Bionic's mature non-_l implementation has precisely that contract. Never
 * pass the opaque guest value as a host locale_t or alter global locale. */
static int32_t guest_strncasecmp_l(const char *a,const char *b,uint64_t n,uintptr_t guest_locale) {
    (void)guest_locale;
    _Static_assert(sizeof(size_t)==8,"reviewed 64-bit count ABI");
#if defined(__ANDROID__)
    return strncasecmp(a,b,(size_t)n);
#else
    /* Deterministic host evidence path; independent of Darwin's global locale. */
    for(;n;n--,a++,b++) {
        unsigned x=(unsigned char)*a,y=(unsigned char)*b;
        if(x>='A' && x<='Z')x+='a'-'A';
        if(y>='A' && y<='Z')y+='a'-'A';
        if(x!=y || !x)return (int32_t)x-(int32_t)y;
    }
    return 0;
#endif
}
/* SDK+59bdb0: unsigned bytes, same fixed ASCII tolower as strncasecmp_l,
 * exact signed difference. Bionic uses the already reviewed fixed ASCII map. */
static int32_t guest_strcasecmp(const char *a,const char *b) {
#if defined(__ANDROID__)
    return strcasecmp(a,b);
#else
    for(;;a++,b++) {
        unsigned x=(unsigned char)*a,y=(unsigned char)*b;
        if(x>='A' && x<='Z')x+='a'-'A';
        if(y>='A' && y<='Z')y+='a'-'A';
        if(x!=y || !x)return (int32_t)x-(int32_t)y;
    }
#endif
}
static int host_argument_count(void) {
    guest_process *p=required_current();
    if(!p->argument_mapping || !p->argument_committed) {fputs("[guest-argv-fatal] arguments not owned/committed\n",stderr);abort();}
    return p->argument_count;
}
static char **host_argument_vector(void) {
    guest_process *p=required_current();(void)host_argument_count();
    return p->argument_mapping;
}
uint32_t guest_process_nnmusl_init_dso(
    uint64_t a0,uint64_t a1,uint64_t a2,uint64_t a3,uint64_t a4,
    uint64_t a5,uint64_t a6,uint64_t a7,uint64_t a8,uint64_t a9,
    uint64_t a10,uint64_t a11,uint64_t a12,uint64_t a13,uint64_t a14,
    uint64_t a15,uint64_t a16,uint64_t a17,uint64_t a18,uint64_t a19,uint64_t a20) {
    guest_process *p=current_process;
    if(!p || !p->module || !p->running || p->quarantined)return 2;
    uint64_t base=nso_base(p->module);
    if(a0!=base)return 2;
    pthread_mutex_lock(&p->mutex);
    if(p->dso.registered) {
        pthread_mutex_unlock(&p->mutex);
        return 1;
    }
    const uint64_t raw[21]={a0,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11,a12,a13,a14,a15,a16,a17,a18,a19,a20};
    memcpy(p->dso.raw_words,raw,sizeof(raw));
    p->dso.base=a0;
    p->dso.text_token=a1;
    p->dso.tls_start=a2;
    if(a3<a2 || a7<a3) {
        pthread_mutex_unlock(&p->mutex);
        return 2;
    }
    p->dso.tls_init_size=(size_t)(a3-a2);
    p->dso.tls_total_size=(size_t)(a7-a2);
    size_t align1=(size_t)(a5>=a2?a5-a2:8);
    size_t align2=(size_t)(a9>=a6?a9-a6:8);
    p->dso.tls_alignment=align1>align2?align1:align2;
    if(!p->dso.tls_alignment)p->dso.tls_alignment=8;
    p->dso.plt_got_start=a10;
    p->dso.plt_got_end=a11;
    p->dso.rela_start=a12;
    p->dso.rela_end=a13;
    p->dso.jmprel_start=a16;
    p->dso.jmprel_end=a17;
    p->dso.dynamic=a20;
    p->dso.tls_template=(nso_module_tls_template){
        .initialized_data=(const void*)(uintptr_t)p->dso.tls_start,
        .initialized_size=p->dso.tls_init_size,
        .total_size=p->dso.tls_total_size,
        .alignment=p->dso.tls_alignment,
        .reserved_header_size=0x10,
    };
    int err=nso_module_tls_create(&p->dso.tls_template,&p->dso.primary_tls_image);
    if(err) {
        fprintf(stderr,"[dso-error] nso_module_tls_create failed err=%d\n",err);
        pthread_mutex_unlock(&p->mutex);
        return 2;
    }
    guest_tp_template tpl = {
        .initialized_data = p->dso.tls_template.initialized_data,
        .initialized_size = p->dso.tls_template.initialized_size,
        .total_size = p->dso.tls_template.total_size,
        .alignment = p->dso.tls_template.alignment,
        .reserved_header_size = p->dso.tls_template.reserved_header_size,
    };
    guest_tp_mirror *mirror = NULL;
    if(guest_tp_mirror_create(&tpl, &mirror).status == GUEST_TP_OK && mirror) {
        if(guest_tp_current_base() != NULL) {
            guest_tp_unbind_current();
        }
        guest_tp_bind_current(mirror);
        fprintf(stderr,"[guest-tp] primary thread bound guest TP base=%p span=%zu\n",
                (void*)guest_tp_mirror_base(mirror),guest_tp_mirror_span(mirror));
    }
    p->dso.registered=1;
    fprintf(stderr,"[dso-registered] base=0x%" PRIx64 " tls_start=0x%" PRIx64 " init_size=%zu total_size=%zu align=%zu; registration complete, returning 1\n",
        p->dso.base,p->dso.tls_start,p->dso.tls_init_size,p->dso.tls_total_size,p->dso.tls_alignment);
    pthread_mutex_unlock(&p->mutex);
    return 1;
}
struct nso_result guest_process_dso_info(guest_process *p, struct guest_dso_info *out) {
    if(!p || !out)return status(NSO_INVALID,EINVAL,0);
    pthread_mutex_lock(&p->mutex);
    out->base=p->dso.base;
    out->text_token=p->dso.text_token;
    out->tls_start=p->dso.tls_start;
    out->tls_init_size=p->dso.tls_init_size;
    out->tls_total_size=p->dso.tls_total_size;
    out->tls_alignment=p->dso.tls_alignment;
    out->plt_got_start=p->dso.plt_got_start;
    out->plt_got_end=p->dso.plt_got_end;
    out->rela_start=p->dso.rela_start;
    out->rela_end=p->dso.rela_end;
    out->jmprel_start=p->dso.jmprel_start;
    out->jmprel_end=p->dso.jmprel_end;
    out->dynamic=p->dso.dynamic;
    out->registered=p->dso.registered;
    if(p->dso.primary_tls_image) {
        const nso_module_tls_view *view=nso_module_tls_get_view(p->dso.primary_tls_image);
        out->tls_anchor=(uintptr_t)view->anchor;
        out->tls_payload=(uintptr_t)view->payload;
        out->tls_storage_size=view->storage_size;
    } else {
        out->tls_anchor=0;
        out->tls_payload=0;
        out->tls_storage_size=0;
    }
    pthread_mutex_unlock(&p->mutex);
    return status(NSO_OK,0,0);
}
nb_scope *guest_process_current_boundary(void) {
    guest_process *p=current_process?current_process:exclusive_process;
    if (p && p->boundary_initialized) {
        return &p->boundary;
    }
    return NULL;
}
int guest_process_current_tls_template(guest_tp_template *out) {
    if (!out) return 0;
    guest_process *p=current_process?current_process:exclusive_process;
    if (p && p->dso.registered) {
        *out = (guest_tp_template){
            .initialized_data = p->dso.tls_template.initialized_data,
            .initialized_size = p->dso.tls_template.initialized_size,
            .total_size = p->dso.tls_template.total_size,
            .alignment = p->dso.tls_template.alignment,
            .reserved_header_size = p->dso.tls_template.reserved_header_size,
        };
        return 1;
    }
    return 0;
}
void guest_process_bind_child_thread(void) {
    pthread_mutex_lock(&exclusive_mutex);
    current_process = exclusive_process;
    guest_process *p = exclusive_process;
    if(p && p->os_tls) {
        struct guest_os_tls_result r = guest_os_tls_register(p->os_tls, &current_os_thread);
        if(r.status != GUEST_OS_TLS_OK) {
            fprintf(stderr, "[os-tls-fatal] failed to register child thread os_tls: status=%d\n", r.status);
            abort();
        }
        fprintf(stderr, "[hle] child thread bound guest OS TLS record=%p\n", (void *)current_os_thread);
    }
    pthread_mutex_unlock(&exclusive_mutex);
}
void guest_process_unbind_child_thread(void) {
    pthread_mutex_lock(&exclusive_mutex);
    guest_process *p = current_process ? current_process : exclusive_process;
    if(p && p->os_tls && current_os_thread) {
        struct guest_os_tls_cleanup_report report = {0};
        guest_os_tls_cleanup(p->os_tls, current_os_thread, invoke_os_destructor, p, &report);
        guest_os_tls_unregister(p->os_tls, current_os_thread);
        current_os_thread = NULL;
    }
    current_process = NULL;
    pthread_mutex_unlock(&exclusive_mutex);
}
static void guest_protect_relro(const void *a0,const void *a1,const void *a2,const void *a3,const void *a4) {
    (void)a0;(void)a1;(void)a2;(void)a3;(void)a4;
    fprintf(stderr,"[protect-relro] start=%p end=%p; returning to init_array walk\n",a0,a1);
}
static void guest_vibration_converter_ctor(void *self, int sample_rate) {
    (void)self;(void)sample_rate;
}
static void guest_vibration_converter_dtor(void *self) {
    (void)self;
}
uint64_t guest_process_resolve(uint32_t index,const char *name) {
    if(!name)return 0;
    if(!strcmp(name,"_ZN2nn2os36GetVirtualAddressMemoryResourceUsageEv"))
        return exclusive_process && exclusive_process->resource_quota?(uintptr_t)&virtual_resource_usage:0;
#define PURE(fn) if(!strcmp(name,#fn))return (uint64_t)(uintptr_t)&fn
    PURE(memset);PURE(memcpy);PURE(memmove);PURE(memcmp);PURE(memchr);
    PURE(strlen);PURE(strnlen);PURE(strcmp);PURE(strncmp);PURE(strcpy);PURE(strncpy);
    PURE(strchr);PURE(strrchr);PURE(strstr);
    PURE(strtol);
    PURE(iswdigit);PURE(isspace);PURE(iswupper);PURE(iswspace);
    PURE(iswalnum);PURE(iswxdigit);PURE(iswalpha);PURE(iswlower);
    PURE(isalpha);PURE(tolower);
    if(!strcmp(name,"finite"))return (uintptr_t)&guest_finite;
    if(!strcmp(name,"snprintf"))return (uintptr_t)&vsnprintf;
    PURE(logf); /* Standard float(float), current original caller uses s0. */
    PURE(pow);PURE(powf);
    PURE(atan2);PURE(atan2f);
    PURE(fmod);PURE(fmodf);
    PURE(hypot);
    PURE(sin);PURE(sinf);
    PURE(cos);PURE(cosf);
    PURE(tan);PURE(tanf);
    PURE(asin);PURE(asinf);
    PURE(acos);PURE(acosf);
    PURE(atan);PURE(atanf);
    PURE(exp);PURE(expf);
    PURE(log);PURE(log2);PURE(log10);
    PURE(log2f);PURE(log10f);
    PURE(sqrt);PURE(sqrtf);
    PURE(sinhf);PURE(tanhf);
    PURE(ceil);PURE(ceilf);
    PURE(floor);PURE(floorf);
    PURE(round);PURE(roundf);
    PURE(trunc);PURE(truncf);
    PURE(fabs);PURE(fabsf);
    PURE(ldexp);PURE(ldexpf);
    PURE(modf);PURE(modff);
    PURE(logbf);PURE(exp2);PURE(exp2f);
    PURE(nextafterf);PURE(fmaf);
    PURE(lrint);PURE(lrintf);
    PURE(rand);PURE(srand);
    PURE(isupper);PURE(atoi);PURE(atof);PURE(atoll);
    PURE(strtoll);PURE(strtoull);PURE(strtoul);PURE(strtod);
    PURE(strcspn);PURE(strspn);PURE(strncat);PURE(strcat);PURE(strncasecmp);
    PURE(bsearch);PURE(qsort);
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
    if(!strcmp(name,"_ZN2nn2os32SetMemoryAllocatorForThreadLocalEPFPvmmEPFvS1_mE"))return (uintptr_t)&set_tls_allocator;
    if(!strcmp(name,"_ZN2nn2os40MemoryAllocatorForThreadLocalInitializedEv"))return (uintptr_t)&tls_allocator_initialized;
    if(!strcmp(name,"nnosAllocateMemoryForThreadLocal"))return (uintptr_t)&sdk_allocate_thread_local;
    if(!strcmp(name,"nnosFreeMemoryForThreadLocal"))return (uintptr_t)&sdk_free_thread_local;
    if(!strcmp(name,"__nnmusl_init_dso"))return (uintptr_t)&guest_process_nnmusl_init_dso;
    if(!strcmp(name,"_ZN2nn2ro12ProtectRelroEPKvS2_S2_S2_S2_"))return (uintptr_t)&guest_protect_relro;
    if(!strcmp(name,"_ZN2nn3hid18VibrationConverterC1Ei"))return (uintptr_t)&guest_vibration_converter_ctor;
    if(!strcmp(name,"_ZN2nn3hid18VibrationConverterD1Ev"))return (uintptr_t)&guest_vibration_converter_dtor;
    if(!strcmp(name,"strcasecmp"))return (uintptr_t)&guest_strcasecmp;
    if(!strcmp(name,"_ZN2nn2os29IsVirtualAddressMemoryEnabledEv"))return (uintptr_t)&virtual_address_enabled;
    if(!strcmp(name,"strncasecmp_l"))return (uintptr_t)&guest_strncasecmp_l;
    if(!strcmp(name,"_ZN2nn2os11GetHostArgcEv"))return (uintptr_t)&host_argument_count;
    if(!strcmp(name,"_ZN2nn2os11GetHostArgvEv"))return (uintptr_t)&host_argument_vector;
    if(!strcmp(name,"_ZN2nn2os16GetCurrentThreadEv") || !strcmp(name,"nnosGetCurrentThread"))return (uintptr_t)&current_thread_token;
    if(!strcmp(name,"_ZN2nn2os11GetThreadIdEPKNS0_10ThreadTypeE") || !strcmp(name,"nnosGetThreadId"))return (uintptr_t)&current_thread_tid;
    if(!strcmp(name,"__cxa_guard_acquire"))return (uintptr_t)&__cxa_guard_acquire;
    if(!strcmp(name,"__cxa_guard_release"))return (uintptr_t)&__cxa_guard_release;
    if(!strcmp(name,"__cxa_guard_abort"))return (uintptr_t)&__cxa_guard_abort;
    /* Every entry is an existing typed branch, never the extension fallthrough.
     * No nvn/dlsym/unowned backing/thread/heap service is admitted. */
    static const char *const typed[]={
        "_ZN2nn2fs12SetAllocatorEPFPvmEPFvS1_mE","nnfsSetAllocator",
        "_ZN2nn2fs22QueryMountRomCacheSizeEPm","nnfsQueryMountRomCacheSize",
        "_ZN2nn2fs8MountRomEPKcPvm","nnfsMountRom",
        "_ZN2nn2fs7UnmountEPKc","nnfsUnmount",
        "_ZN2nn2fs8OpenFileEPNS0_10FileHandleEPKci","nnfsOpenFile",
        "_ZN2nn2fs11GetFileSizeEPlNS0_10FileHandleE","nnfsGetFileSize",
        "_ZN2nn2fs8ReadFileEPmNS0_10FileHandleElPvm","nnfsReadFile",
        "_ZN2nn2fs8ReadFileENS0_10FileHandleElPvm",
        "_ZN2nn2fs9CloseFileENS0_10FileHandleE","nnfsCloseFile",
        "_ZN2nn2fs9FlushFileENS0_10FileHandleE","nnfsFlushFile",
        "_ZN2nn2fs11SetFileSizeENS0_10FileHandleEl","nnfsSetFileSize",
        "_ZN2nn2fs9WriteFileENS0_10FileHandleElPKvmRKNS0_11WriteOptionE","nnfsWriteFile",
        "_ZN2nn2fs12GetEntryTypeEPNS0_18DirectoryEntryTypeEPKc","nnfsGetEntryType",
        "_ZN2nn2fs13OpenDirectoryEPNS0_15DirectoryHandleEPKci","nnfsOpenDirectory",
        "_ZN2nn2fs22GetDirectoryEntryCountEPlNS0_15DirectoryHandleE","nnfsGetDirectoryEntryCount",
        "_ZN2nn2fs13ReadDirectoryEPlPNS0_14DirectoryEntryENS0_15DirectoryHandleEl","nnfsReadDirectory",
        "_ZN2nn2fs14CloseDirectoryENS0_15DirectoryHandleE","nnfsCloseDirectory",
        "_ZN2nn2fs15CreateDirectoryEPKc","nnfsCreateDirectory",
        "_ZN2nn2fs26DisableGlobalFileDataCacheEv",
        "_ZN2nn2fs25EnableGlobalFileDataCacheEPvm",
        "_ZN2nn3aoc17CountAddOnContentEv","nnaocCountAddOnContent",
        "_ZN2nn3aoc16ListAddOnContentEPiii","nnaocListAddOnContent",
        "_ZN2nn3aoc31GetAddOnContentListChangedEventEPNS_2os11SystemEventE",
        "_ZN2nn2os15WaitSystemEventEPNS0_15SystemEventTypeE",
        "_ZN2nn2os17SignalSystemEventEPNS0_15SystemEventTypeE",
        "_ZN2nn2os18DestroySystemEventEPNS0_15SystemEventTypeE",
        "_ZN2nn2fs31QueryMountAddOnContentCacheSizeEPmi","nnfsQueryMountAddOnContentCacheSize",
        "_ZN2nn2fs17MountAddOnContentEPKciPvm","nnfsMountAddOnContent",
        "_ZN2nn4nifm10InitializeEv","nnnifmInitialize",
        "_ZN2nn6socket10InitializeEPvmmi","nnsocketInitialize",
        "_ZN2nn7account10InitializeEv","nnaccountInitialize",
        "_ZN2nn2oe10InitializeEv","nnoeInitialize",
        "_ZN2nn2oe18GetDesiredLanguageEv","nnoeGetDesiredLanguage",
        "_ZN2nn4util11ReferSymbolEPKv","nnutilReferSymbol",
        "_ZN2nn2os15InitializeMutexEPNS0_9MutexTypeEbi","nnosInitializeMutex",
        "_ZN2nn2os13FinalizeMutexEPNS0_9MutexTypeE","nnosFinalizeMutex",
        "_ZN2nn2os9LockMutexEPNS0_9MutexTypeE","nnosLockMutex",
        "_ZN2nn2os12TryLockMutexEPNS0_9MutexTypeE","nnosTryLockMutex",
        "_ZN2nn2os11UnlockMutexEPNS0_9MutexTypeE","nnosUnlockMutex",
        "_ZN2nn2os28IsMutexLockedByCurrentThreadEPKNS0_9MutexTypeE",
        "_ZN2nn2os26InitializeReaderWriterLockEPNS0_20ReaderWriterLockTypeE","nnosInitializeReaderWriterLock",
        "_ZN2nn2os24FinalizeReaderWriterLockEPNS0_20ReaderWriterLockTypeE","nnosFinalizeReaderWriterLock",
        "_ZN2nn2os15AcquireReadLockEPNS0_20ReaderWriterLockTypeE","nnosAcquireReadLock",
        "_ZN2nn2os18TryAcquireReadLockEPNS0_20ReaderWriterLockTypeE","nnosTryAcquireReadLock",
        "_ZN2nn2os15ReleaseReadLockEPNS0_20ReaderWriterLockTypeE","nnosReleaseReadLock",
        "_ZN2nn2os16AcquireWriteLockEPNS0_20ReaderWriterLockTypeE","nnosAcquireWriteLock",
        "_ZN2nn2os19TryAcquireWriteLockEPNS0_20ReaderWriterLockTypeE","nnosTryAcquireWriteLock",
        "_ZN2nn2os16ReleaseWriteLockEPNS0_20ReaderWriterLockTypeE","nnosReleaseWriteLock",
        "_ZN2nn2os30IsWriteLockHeldByCurrentThreadEPKNS0_20ReaderWriterLockTypeE",
        "_ZN2nn2os35IsWriteLockHeldByCurrentThreadEPKNS0_20ReaderWriterLockTypeE",
        "_ZN2nn2os29IsReaderWriterLockOwnerThreadEPKNS0_20ReaderWriterLockTypeE",
        "_ZN2nn2os14IsReadLockHeldEPKNS0_20ReaderWriterLockTypeE",
        "_ZN2nn2os15InitializeEventEPNS0_9EventTypeEbNS0_14EventClearModeE","nnosInitializeEvent",
        "_ZN2nn2os13FinalizeEventEPNS0_9EventTypeE","nnosFinalizeEvent",
        "_ZN2nn2os11SignalEventEPNS0_9EventTypeE","nnosSignalEvent",
        "_ZN2nn2os10ClearEventEPNS0_9EventTypeE",
        "_ZN2nn2os9WaitEventEPNS0_9EventTypeE","nnosWaitEvent",
        "_ZN2nn2os12TryWaitEventEPNS0_9EventTypeE",
        "_ZN2nn2os14TimedWaitEventEPNS0_9EventTypeENS_8TimeSpanE",
        "_ZN2nn2os12CreateThreadEPNS0_10ThreadTypeEPFvPvES3_S3_mi","nnosCreateThread",
        "_ZN2nn2os12CreateThreadEPNS0_10ThreadTypeEPFvPvES3_S3_mii",
        "_ZN2nn2os11StartThreadEPNS0_10ThreadTypeE","nnosStartThread",
        "_ZN2nn2os10WaitThreadEPNS0_10ThreadTypeE","nnosWaitThread",
        "_ZN2nn2os13DestroyThreadEPNS0_10ThreadTypeE","nnosDestroyThread",
        "_ZN2nn2os13SetThreadNameEPNS0_10ThreadTypeEPKc",
        "_ZN2nn2os20SetThreadNamePointerEPNS0_10ThreadTypeEPKc","nnosSetThreadNamePointer",
        "_ZN2nn2os20GetThreadNamePointerEPKNS0_10ThreadTypeE",
        "_ZN2nn2os17GetThreadPriorityEPKNS0_10ThreadTypeE","nnosGetThreadPriority",
        "_ZN2nn2os24GetThreadCurrentPriorityEPKNS0_10ThreadTypeE",
        "_ZN2nn2os20ChangeThreadPriorityEPNS0_10ThreadTypeEi",
        "_ZN2nn2os11SleepThreadENS_8TimeSpanE","nnosSleepThread",
        "_ZN2nn2os11YieldThreadEv",
        "_ZN2nn2os17SetThreadCoreMaskEPNS0_10ThreadTypeEim","nnosSetThreadCoreMask",
        "_ZN2nn2os17GetThreadCoreMaskEPiPmPKNS0_10ThreadTypeE","nnosGetThreadCoreMask",
        "_ZN2nn2os26GetThreadAvailableCoreMaskEv",
        "_ZN2nn2os23GetCurrentProcessHandleEv","nnosGetCurrentProcessHandle",
        "_ZN5movie17GetThreadPriorityEv",
        "_ZN5movie17SetThreadPriorityEi",
        "_ZN2nn4util12GenerateUuidEv",
        "_ZN2nn4time10InitializeEv",
        "_ZN2nn4time13IsInitializedEv",
        "_ZN2nn4time23StandardUserSystemClock14GetCurrentTimeEPNS0_9PosixTimeE",
        "_ZN2nn4time21GetDeviceLocationNameEPNS0_12LocationNameE",
        "_ZN2nn4time16LoadTimeZoneRuleEPNS0_12TimeZoneRuleERKNS0_12LocationNameE",
        "_ZN2nn4time14ToCalendarTimeEPNS0_12CalendarTimeEPNS0_22CalendarAdditionalInfoERKNS0_9PosixTimeERKNS0_12TimeZoneRuleE",
        "_ZN2nn4time19ToCalendarTimeInUtcERKNS0_9PosixTimeE",
        "_ZN2nn4time12GetDayOfWeekEiii",
        "_ZN2nn2os22GetSystemTickFrequencyEv","nnosGetSystemTickFrequency",
        "_ZN2nn2os13GetSystemTickEv","nnosGetSystemTick",
        "_ZTHN13LowLevelTasks5FTask10ActiveTaskE",
        "_ZTHN13LowLevelTasks7Private20FOversubscriptionTls26bIsOversubscriptionAllowedE",
        "_ZTHN30FOverridableSerializationLogic20OverriddenPropertiesE",
        "_ZTHN30FOverridableSerializationLogic28bUseOverridableSerializationE",
        "_ZTHN17FPropertyTagScope18CurrentPropertyTagE",
        /* Registration is safe; finalize/exit can invoke guest callbacks and
         * require a separately supported callback/reentry boundary. */
        "atexit","__cxa_atexit","_Exit","_exit",
        "posix_memalign","memalign","aligned_alloc","valloc","free",
        "__cxa_thread_atexit","__cxa_thread_atexit_impl",
        "_ZN2nn4diag6detail9AbortImplEv",
        "_ZN2nn4diag12GetBacktraceEPmi",
        "_ZNSt3__15mutex4lockEv",
        "_ZNSt3__15mutex6unlockEv",
        "_ZNSt3__118condition_variable10notify_oneEv",
        "_ZNSt3__118condition_variable10notify_allEv",
        "_ZNSt3__118condition_variable4waitERNS_11unique_lockINS_5mutexEEE",
        "_ZNSt3__16chrono12steady_clock3nowEv",
        "pthread_cond_clockwait",
        "_ZNSt3__120__throw_system_errorEiPKc",
        "_ZNSt3__122__libcpp_verbose_abortEPKcz",
        "setlocale","getenv","nl_langinfo","gettimeofday","getpid",
        "__cxa_pure_virtual",NULL};
    for(size_t i=0;typed[i];i++)if(!strcmp(name,typed[i]))return nso_host_resolve(index,name);
    if(!strcmp(name,"abort"))return (uintptr_t)&abort;
    return 0;
}
static int import_signature(const char *name,nb_signature *out) {
    if(!strcmp(name,"logf") || !strcmp(name,"log") || !strcmp(name,"log2") || !strcmp(name,"log2f") ||
       !strcmp(name,"log10") || !strcmp(name,"log10f") || !strcmp(name,"exp") || !strcmp(name,"expf") ||
       !strcmp(name,"sqrt") || !strcmp(name,"sqrtf") || !strcmp(name,"sin") || !strcmp(name,"sinf") ||
       !strcmp(name,"cos") || !strcmp(name,"cosf") || !strcmp(name,"tan") || !strcmp(name,"tanf") ||
       !strcmp(name,"asin") || !strcmp(name,"asinf") || !strcmp(name,"acos") || !strcmp(name,"acosf") ||
       !strcmp(name,"atan") || !strcmp(name,"atanf") || !strcmp(name,"sinhf") || !strcmp(name,"tanhf") ||
       !strcmp(name,"ceil") || !strcmp(name,"ceilf") || !strcmp(name,"floor") || !strcmp(name,"floorf") ||
       !strcmp(name,"round") || !strcmp(name,"roundf") || !strcmp(name,"trunc") || !strcmp(name,"truncf") ||
       !strcmp(name,"fabs") || !strcmp(name,"fabsf") ||
       !strcmp(name,"logbf") || !strcmp(name,"exp2") || !strcmp(name,"exp2f")) {
        *out=(nb_signature){.version=1,.flags=NB_SIG_REGISTER_ONLY,.fp_arguments=1,.fp_returns=1};
        return 1;
    }
    if(!strcmp(name,"pow") || !strcmp(name,"powf") || !strcmp(name,"atan2") || !strcmp(name,"atan2f") ||
       !strcmp(name,"fmod") || !strcmp(name,"fmodf") || !strcmp(name,"hypot") || !strcmp(name,"nextafterf")) {
        *out=(nb_signature){.version=1,.flags=NB_SIG_REGISTER_ONLY,.fp_arguments=2,.fp_returns=1};
        return 1;
    }
    if(!strcmp(name,"fmaf")) {
        *out=(nb_signature){.version=1,.flags=NB_SIG_REGISTER_ONLY,.fp_arguments=3,.fp_returns=1};
        return 1;
    }
    if(!strcmp(name,"ldexp") || !strcmp(name,"ldexpf") || !strcmp(name,"modf") || !strcmp(name,"modff")) {
        *out=(nb_signature){.version=1,.flags=NB_SIG_REGISTER_ONLY,.gp_arguments=1,.fp_arguments=1,.fp_returns=1};
        return 1;
    }
    if(!strcmp(name,"finite") || !strcmp(name,"lrint") || !strcmp(name,"lrintf")) {
        *out=(nb_signature){.version=1,.flags=NB_SIG_REGISTER_ONLY,.fp_arguments=1,.gp_returns=1};
        return 1;
    }
    if(!strcmp(name,"atof")) {
        *out=(nb_signature){.version=1,.flags=NB_SIG_REGISTER_ONLY,.gp_arguments=1,.fp_returns=1};
        return 1;
    }
    if(!strcmp(name,"strtod")) {
        *out=(nb_signature){.version=1,.flags=NB_SIG_REGISTER_ONLY,.gp_arguments=2,.fp_returns=1};
        return 1;
    }
    unsigned arguments=0,returns=1;
    static const char *const one[]={"strlen","__cxa_guard_acquire","atexit","_ZN2nn2os11GetTlsValueENS0_7TlsSlotE",
        "_ZN2nn2os17FreeAddressRegionEm","_ZN2nn2os12TryLockMutexEPNS0_9MutexTypeE",
        "nnosTryLockMutex","_ZN2nn2os28IsMutexLockedByCurrentThreadEPKNS0_9MutexTypeE",
        "_ZN2nn2os19TryAcquireWriteLockEPNS0_20ReaderWriterLockTypeE","nnosTryAcquireWriteLock",
        "_ZN2nn2os18TryAcquireReadLockEPNS0_20ReaderWriterLockTypeE","nnosTryAcquireReadLock",
        "_ZN2nn2os30IsWriteLockHeldByCurrentThreadEPKNS0_20ReaderWriterLockTypeE",
        "_ZN2nn2os35IsWriteLockHeldByCurrentThreadEPKNS0_20ReaderWriterLockTypeE",
        "_ZN2nn2os29IsReaderWriterLockOwnerThreadEPKNS0_20ReaderWriterLockTypeE",
        "_ZN2nn2os14IsReadLockHeldEPKNS0_20ReaderWriterLockTypeE",
        "_ZN2nn2os12TryWaitEventEPNS0_9EventTypeE",
        "_ZN2nn4time23StandardUserSystemClock14GetCurrentTimeEPNS0_9PosixTimeE",
        "_ZN2nn4time19ToCalendarTimeInUtcERKNS0_9PosixTimeE",
        "_ZN2nn4time21GetDeviceLocationNameEPNS0_12LocationNameE",
        "_ZN2nn2os20GetThreadNamePointerEPKNS0_10ThreadTypeE",
        "_ZN2nn2os17GetThreadPriorityEPKNS0_10ThreadTypeE","nnosGetThreadPriority",
        "_ZN2nn2os24GetThreadCurrentPriorityEPKNS0_10ThreadTypeE",
        "_ZN5movie17GetThreadPriorityEv",
        "iswdigit","isspace","iswupper","iswspace","iswalnum","iswxdigit",
        "iswalpha","iswlower","isalpha","tolower","isupper","atoi","atoll","valloc","getenv","nl_langinfo",NULL};
    static const char *const two[]={"strnlen","strcmp","strcasecmp","strcpy","strchr","strrchr","strstr","_ZN2nn2os15AllocateTlsSlotEPNS0_7TlsSlotEPFvmE",
        "_ZN2nn2os21AllocateAddressRegionEPmm","_ZN2nn2os19AllocateMemoryPagesEmm","_ZN2nn2os15FreeMemoryPagesEmm",
        "_ZN2nn4time16LoadTimeZoneRuleEPNS0_12TimeZoneRuleERKNS0_12LocationNameE",
        "_ZN2nn2os14TimedWaitEventEPNS0_9EventTypeENS_8TimeSpanE",
        "strcspn","strspn","strcat","memalign","aligned_alloc","setlocale","gettimeofday",NULL};
    static const char *const three[]={"memset","memcpy","memmove","memcmp","memchr","strncmp","strncpy","__cxa_atexit","strtol",
        "_ZN2nn4time12GetDayOfWeekEiii",
        "strtoll","strtoull","strtoul","strncat","strncasecmp","posix_memalign",
        "__cxa_thread_atexit","__cxa_thread_atexit_impl",NULL};
    static const char *const void_one[]={"__cxa_guard_release","__cxa_guard_abort","_Exit","_exit","_ZN2nn2os11FreeTlsSlotENS0_7TlsSlotE",
        "_ZN2nn2os15QueryMemoryInfoEPNS0_10MemoryInfoE","_ZN2nn4util11ReferSymbolEPKv","nnutilReferSymbol",
        "_ZN2nn2os13FinalizeMutexEPNS0_9MutexTypeE","nnosFinalizeMutex",
        "_ZN2nn2os9LockMutexEPNS0_9MutexTypeE","nnosLockMutex",
        "_ZN2nn2os11UnlockMutexEPNS0_9MutexTypeE","nnosUnlockMutex",
        "_ZN2nn2os26InitializeReaderWriterLockEPNS0_20ReaderWriterLockTypeE","nnosInitializeReaderWriterLock",
        "_ZN2nn2os24FinalizeReaderWriterLockEPNS0_20ReaderWriterLockTypeE","nnosFinalizeReaderWriterLock",
        "_ZN2nn2os15AcquireReadLockEPNS0_20ReaderWriterLockTypeE","nnosAcquireReadLock",
        "_ZN2nn2os15ReleaseReadLockEPNS0_20ReaderWriterLockTypeE","nnosReleaseReadLock",
        "_ZN2nn2os16AcquireWriteLockEPNS0_20ReaderWriterLockTypeE","nnosAcquireWriteLock",
        "_ZN2nn2os16ReleaseWriteLockEPNS0_20ReaderWriterLockTypeE","nnosReleaseWriteLock","srand",
        "_ZN2nn2os11StartThreadEPNS0_10ThreadTypeE","nnosStartThread",
        "_ZN2nn2os10WaitThreadEPNS0_10ThreadTypeE","nnosWaitThread",
        "_ZN2nn2os13DestroyThreadEPNS0_10ThreadTypeE","nnosDestroyThread",
        "_ZN2nn2os11SleepThreadENS_8TimeSpanE","nnosSleepThread",
        "_ZN2nn2os13FinalizeEventEPNS0_9EventTypeE","nnosFinalizeEvent",
        "_ZN2nn2os11SignalEventEPNS0_9EventTypeE","nnosSignalEvent",
        "_ZN2nn2os10ClearEventEPNS0_9EventTypeE",
        "_ZN2nn2os9WaitEventEPNS0_9EventTypeE","nnosWaitEvent",
        "_ZN2nn3aoc31GetAddOnContentListChangedEventEPNS_2os11SystemEventE",
        "_ZN2nn2os15WaitSystemEventEPNS0_15SystemEventTypeE",
        "_ZN2nn2os17SignalSystemEventEPNS0_15SystemEventTypeE",
        "_ZN2nn2os18DestroySystemEventEPNS0_15SystemEventTypeE",
        "_ZNSt3__15mutex4lockEv",
        "_ZNSt3__15mutex6unlockEv",
        "_ZNSt3__118condition_variable10notify_oneEv",
        "_ZNSt3__118condition_variable10notify_allEv",
        "_ZN5movie17SetThreadPriorityEi","free",NULL};
    const char *const *lists[]={one,two,three,void_one};
    for(unsigned group=0;group<4;group++)for(size_t i=0;lists[group][i];i++)if(!strcmp(name,lists[group][i])) {
        arguments=group==3?1:group+1;returns=group==3?0:1;goto found;
    }
    if(!strcmp(name,"rand") || !strcmp(name,"getpid")) {arguments=0;returns=1;}
    else if(!strcmp(name,"qsort")) {arguments=4;returns=0;}
    else if(!strcmp(name,"bsearch")) {arguments=5;returns=1;}
    else if(!strcmp(name,"_ZN2nn3hid18VibrationConverterC1Ei")) {arguments=2;returns=0;}
    else if(!strcmp(name,"_ZN2nn3hid18VibrationConverterD1Ev")) {arguments=1;returns=0;}
    else if(!strcmp(name,"_ZN2nn2os29IsVirtualAddressMemoryEnabledEv")) {arguments=0;returns=1;}
    else if(!strcmp(name,"_ZN2nn2os22GetSystemTickFrequencyEv") || !strcmp(name,"nnosGetSystemTickFrequency") ||
            !strcmp(name,"_ZN2nn2os13GetSystemTickEv") || !strcmp(name,"nnosGetSystemTick")) {arguments=0;returns=1;}
    else if(!strcmp(name,"_ZN2nn4time10InitializeEv") || !strcmp(name,"_ZN2nn4time13IsInitializedEv") ||
            !strcmp(name,"_ZN2nn4nifm10InitializeEv") || !strcmp(name,"nnnifmInitialize") ||
            !strcmp(name,"_ZN2nn7account10InitializeEv") || !strcmp(name,"nnaccountInitialize") ||
            !strcmp(name,"_ZN2nn2oe10InitializeEv") || !strcmp(name,"nnoeInitialize") ||
            !strcmp(name,"_ZN2nn2oe18GetDesiredLanguageEv") || !strcmp(name,"nnoeGetDesiredLanguage") ||
            !strcmp(name,"_ZN2nn2os23GetCurrentProcessHandleEv") || !strcmp(name,"nnosGetCurrentProcessHandle")) {arguments=0;returns=1;}
    else if(!strcmp(name,"_ZN2nn4util12GenerateUuidEv")) {arguments=0;returns=2;}
    else if(!strcmp(name,"strncasecmp_l") || !strcmp(name,"_ZN2nn4time14ToCalendarTimeEPNS0_12CalendarTimeEPNS0_22CalendarAdditionalInfoERKNS0_9PosixTimeERKNS0_12TimeZoneRuleE")) {arguments=4;returns=1;}
    else if(!strcmp(name,"_ZN2nn2os36GetVirtualAddressMemoryResourceUsageEv")) {arguments=0;returns=2;}
    else if(!strcmp(name,"_ZN2nn2ro12ProtectRelroEPKvS2_S2_S2_S2_")) {arguments=5;returns=0;}
    else if(!strcmp(name,"_ZN2nn2os15InitializeEventEPNS0_9EventTypeEbNS0_14EventClearModeE") ||
            !strcmp(name,"nnosInitializeEvent")) {arguments=3;returns=0;}
    else if(!strcmp(name,"_ZN2nn2os15InitializeMutexEPNS0_9MutexTypeEbi") || !strcmp(name,"nnosInitializeMutex")) {arguments=3;returns=0;}
    else if(!strcmp(name,"_ZN2nn2os12CreateThreadEPNS0_10ThreadTypeEPFvPvES3_S3_mi") ||
            !strcmp(name,"nnosCreateThread")) {arguments=6;returns=1;}
    else if(!strcmp(name,"_ZN2nn2os12CreateThreadEPNS0_10ThreadTypeEPFvPvES3_S3_mii")) {arguments=7;returns=1;}
    else if(!strcmp(name,"_ZN2nn2os13SetThreadNameEPNS0_10ThreadTypeEPKc") ||
            !strcmp(name,"_ZN2nn2os20SetThreadNamePointerEPNS0_10ThreadTypeEPKc") ||
            !strcmp(name,"nnosSetThreadNamePointer") ||
            !strcmp(name,"_ZN2nn2os20ChangeThreadPriorityEPNS0_10ThreadTypeEi")) {arguments=2;returns=0;}
    else if(!strcmp(name,"_ZN2nn2os17SetThreadCoreMaskEPNS0_10ThreadTypeEim") ||
            !strcmp(name,"nnosSetThreadCoreMask") ||
            !strcmp(name,"_ZN2nn2os17GetThreadCoreMaskEPiPmPKNS0_10ThreadTypeE") ||
            !strcmp(name,"nnosGetThreadCoreMask")) {arguments=3;returns=0;}
    else if(!strcmp(name,"_ZN2nn2os26GetThreadAvailableCoreMaskEv")) {arguments=0;returns=1;}
    else if(!strcmp(name,"_ZN2nn2os11YieldThreadEv")) {arguments=0;returns=0;}
    else if(!strcmp(name,"nnosGetThreadId")) {arguments=1;returns=1;}
    else if(!strcmp(name,"nnosGetCurrentThread")) {arguments=0;returns=1;}
    else if(!strcmp(name,"_ZN2nn2os11GetHostArgcEv") || !strcmp(name,"_ZN2nn2os11GetHostArgvEv")) {arguments=0;returns=1;}
    else if(!strcmp(name,"_ZN2nn2os16GetCurrentThreadEv")) {arguments=0;returns=1;}
    else if(!strcmp(name,"_ZN2nn2os11GetThreadIdEPKNS0_10ThreadTypeE")) {arguments=1;returns=1;}
    else if(!strcmp(name,"_ZN2nn2os11SetTlsValueENS0_7TlsSlotEm")) {arguments=2;returns=0;}
    else if(!strcmp(name,"_ZNSt3__118condition_variable4waitERNS_11unique_lockINS_5mutexEEE")) {arguments=2;returns=0;}
    else if(!strcmp(name,"_ZNSt3__16chrono12steady_clock3nowEv")) {arguments=0;returns=1;}
    else if(!strcmp(name,"pthread_cond_clockwait")) {arguments=4;returns=1;}
    else if(!strcmp(name,"_ZNSt3__120__throw_system_errorEiPKc")) {arguments=2;returns=0;}
    else if(!strcmp(name,"_ZNSt3__122__libcpp_verbose_abortEPKcz")) {arguments=1;returns=0;}
    else if(!strcmp(name,"__cxa_pure_virtual")) {arguments=0;returns=0;}
    else if(!strcmp(name,"_ZN2nn2fs12SetAllocatorEPFPvmEPFvS1_mE") ||
            !strcmp(name,"nnfsSetAllocator")) {arguments=2;returns=0;}
    else if(!strcmp(name,"_ZN2nn2fs22QueryMountRomCacheSizeEPm") ||
            !strcmp(name,"nnfsQueryMountRomCacheSize")) {arguments=1;returns=1;}
    else if(!strcmp(name,"_ZN2nn2fs8MountRomEPKcPvm") ||
            !strcmp(name,"nnfsMountRom")) {arguments=3;returns=1;}
    else if(!strcmp(name,"_ZN2nn2fs7UnmountEPKc") ||
            !strcmp(name,"nnfsUnmount")) {arguments=1;returns=1;}
    else if(!strcmp(name,"_ZN2nn2fs8OpenFileEPNS0_10FileHandleEPKci") ||
            !strcmp(name,"nnfsOpenFile")) {arguments=3;returns=1;}
    else if(!strcmp(name,"_ZN2nn2fs11GetFileSizeEPlNS0_10FileHandleE") ||
            !strcmp(name,"nnfsGetFileSize")) {arguments=2;returns=1;}
    else if(!strcmp(name,"_ZN2nn2fs8ReadFileEPmNS0_10FileHandleElPvm") ||
            !strcmp(name,"nnfsReadFile")) {arguments=5;returns=1;}
    else if(!strcmp(name,"_ZN2nn2fs8ReadFileENS0_10FileHandleElPvm")) {arguments=4;returns=1;}
    else if(!strcmp(name,"_ZN2nn2fs9CloseFileENS0_10FileHandleE") ||
            !strcmp(name,"nnfsCloseFile")) {arguments=1;returns=0;}
    else if(!strcmp(name,"_ZN2nn2fs9FlushFileENS0_10FileHandleE") ||
            !strcmp(name,"nnfsFlushFile")) {arguments=1;returns=1;}
    else if(!strcmp(name,"_ZN2nn2fs11SetFileSizeENS0_10FileHandleEl") ||
            !strcmp(name,"nnfsSetFileSize")) {arguments=2;returns=1;}
    else if(!strcmp(name,"_ZN2nn2fs9WriteFileENS0_10FileHandleElPKvmRKNS0_11WriteOptionE") ||
            !strcmp(name,"nnfsWriteFile")) {arguments=5;returns=1;}
    else if(!strcmp(name,"_ZN2nn2fs12GetEntryTypeEPNS0_18DirectoryEntryTypeEPKc") ||
            !strcmp(name,"nnfsGetEntryType")) {arguments=2;returns=1;}
    else if(!strcmp(name,"_ZN2nn2fs13OpenDirectoryEPNS0_15DirectoryHandleEPKci") ||
            !strcmp(name,"nnfsOpenDirectory")) {arguments=3;returns=1;}
    else if(!strcmp(name,"_ZN2nn2fs22GetDirectoryEntryCountEPlNS0_15DirectoryHandleE") ||
            !strcmp(name,"nnfsGetDirectoryEntryCount")) {arguments=2;returns=1;}
    else if(!strcmp(name,"_ZN2nn2fs13ReadDirectoryEPlPNS0_14DirectoryEntryENS0_15DirectoryHandleEl") ||
            !strcmp(name,"nnfsReadDirectory")) {arguments=4;returns=1;}
    else if(!strcmp(name,"_ZN2nn2fs14CloseDirectoryENS0_15DirectoryHandleE") ||
            !strcmp(name,"nnfsCloseDirectory")) {arguments=1;returns=0;}
    else if(!strcmp(name,"_ZN2nn2fs15CreateDirectoryEPKc") ||
            !strcmp(name,"nnfsCreateDirectory")) {arguments=1;returns=1;}
    else if(!strcmp(name,"_ZN2nn2fs26DisableGlobalFileDataCacheEv")) {arguments=0;returns=0;}
    else if(!strcmp(name,"_ZN2nn2fs25EnableGlobalFileDataCacheEPvm")) {arguments=2;returns=0;}
    else if(!strcmp(name,"_ZN2nn3aoc17CountAddOnContentEv") ||
            !strcmp(name,"nnaocCountAddOnContent")) {arguments=0;returns=1;}
    else if(!strcmp(name,"_ZN2nn3aoc16ListAddOnContentEPiii") ||
            !strcmp(name,"nnaocListAddOnContent")) {arguments=3;returns=1;}
    else if(!strcmp(name,"_ZN2nn2fs31QueryMountAddOnContentCacheSizeEPmi") ||
            !strcmp(name,"nnfsQueryMountAddOnContentCacheSize")) {arguments=2;returns=1;}
    else if(!strcmp(name,"_ZN2nn2fs17MountAddOnContentEPKciPvm") ||
            !strcmp(name,"nnfsMountAddOnContent")) {arguments=4;returns=1;}
    else if(!strcmp(name,"_ZN2nn6socket10InitializeEPvmmi") ||
            !strcmp(name,"nnsocketInitialize")) {arguments=4;returns=1;}
    else if(!strcmp(name,"_ZN2nn2os32SetMemoryAllocatorForThreadLocalEPFPvmmEPFvS1_mE")) {arguments=2;returns=0;}
    else if(!strcmp(name,"_ZN2nn2os40MemoryAllocatorForThreadLocalInitializedEv")) {arguments=0;returns=1;}
    else if(!strcmp(name,"nnosAllocateMemoryForThreadLocal")) {arguments=1;returns=1;}
    else if(!strcmp(name,"nnosFreeMemoryForThreadLocal")) {arguments=2;returns=0;}
    else if(!strcmp(name,"__nnmusl_init_dso")) {
        *out=(nb_signature){.version=2,.flags=NB_SIG_FIXED_GP21,
            .stack_argument_bytes=104,.gp_arguments=8,.gp_returns=1};
        return 1;
    }
    else if(!strcmp(name,"snprintf")) {
        *out=(nb_signature){.version=3,.flags=NB_SIG_VARIADIC_SNPRINTF,
            .gp_arguments=3,.gp_returns=1,.variadic=1};
        return 1;
    }
    else if(!strcmp(name,"_ZN2nn4diag6detail9AbortImplEv")) {arguments=0;returns=1;}
    else if(!strcmp(name,"_ZN2nn4diag12GetBacktraceEPmi")) {arguments=2;returns=1;}
    else if(!strcmp(name,"abort")) {arguments=0;returns=0;}
    else if(!strcmp(name,"_ZTHN13LowLevelTasks5FTask10ActiveTaskE") ||
            !strcmp(name,"_ZTHN13LowLevelTasks7Private20FOversubscriptionTls26bIsOversubscriptionAllowedE") ||
            !strcmp(name,"_ZTHN30FOverridableSerializationLogic20OverriddenPropertiesE") ||
            !strcmp(name,"_ZTHN30FOverridableSerializationLogic28bUseOverridableSerializationE") ||
            !strcmp(name,"_ZTHN17FPropertyTagScope18CurrentPropertyTagE")) {arguments=0;returns=0;}
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
            fprintf(stderr, "[boundary-unsupported] index=%u name=%s\n", targets[i].index, targets[i].name);
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
    if(r.status!=NSO_OK) {
        fprintf(stderr,"[boundary-error] nso_retarget_imports failed status=%d native=%d\n",r.status,r.native_error);
        p->quarantined=1;return status(NSO_RETAINED,r.native_error,r.cleanup_error);
    }
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
    if(current_process!=p || manager!=p->os_tls || (thread!=p->os_thread && thread!=current_os_thread) || destructor<nso_base(p->module))
        return (struct guest_os_tls_invoke_result){GUEST_OS_TLS_INVOKE_FAILED,EINVAL};
    if(thread==p->os_thread) {
        const uint64_t args[6]={value};uint64_t ignored;
        struct nso_result r=guest_process_call(p,destructor-nso_base(p->module),args,1,&ignored);
        return (struct guest_os_tls_invoke_result){r.status==NSO_OK?GUEST_OS_TLS_INVOKE_OK:GUEST_OS_TLS_INVOKE_FAILED,r.status};
    }
    nb_scope *scope=guest_process_current_boundary();
    if(scope) {
        nb_registers pkt={.x={value}};
        nb_status s=nb_call_child(scope,(void(*)(void))(uintptr_t)destructor,&pkt,0);
        return (struct guest_os_tls_invoke_result){s==NB_OK?GUEST_OS_TLS_INVOKE_OK:GUEST_OS_TLS_INVOKE_FAILED,s};
    }
    return (struct guest_os_tls_invoke_result){GUEST_OS_TLS_INVOKE_OK,0};
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
    current_os_thread=NULL;
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
    struct nso_result identity=create_identity_locked(p);
    struct guest_os_tls_result registered={0};
    if(identity.status==NSO_OK)registered=guest_os_tls_register(p->os_tls,&p->os_thread);
    if(registered.status==GUEST_OS_TLS_OK)current_os_thread=p->os_thread;
    if(p->heap.state==HEAP_UNINITIALIZED && initialize_empty_heap(&p->heap))p->quarantined=1;
    struct guest_memory_info initial;
    p->callback_status=identity.status!=NSO_OK?identity:
        registered.status==GUEST_OS_TLS_OK?snapshot_locked(p,&initial,NULL,NULL):status(NSO_OWNER_ERROR,registered.native_error,0);
    pthread_mutex_unlock(&p->mutex);
    int e=0;
    void *value=NULL;
    if(p->callback_status.status==NSO_OK) {
        if(guest_tp_current_base() == NULL) {
            guest_tp_template initial_tpl = {
                .initialized_data = NULL,
                .initialized_size = 0,
                .total_size = 0x1019,
                .alignment = 8,
                .reserved_header_size = 0x10,
            };
            guest_tp_mirror *init_mirror = NULL;
            if(guest_tp_mirror_create(&initial_tpl, &init_mirror).status == GUEST_TP_OK && init_mirror) {
                guest_tp_bind_current(init_mirror);
            }
        }
        e=nso_fault_scope_begin(p->module);
        if(e) {
            int cleanup=nso_fault_scope_end(p->module);
            p->callback_status=status(cleanup?NSO_RETAINED:NSO_OWNER_ERROR,e,cleanup);
            if(cleanup)p->diagnostic_retained=1;
        }
        else {
            guest_tp_install_trap_handler();
            value=p->callback(p,p->argument);
            guest_tp_remove_trap_handler();
            cleanup_os_primary(p);
            e=nso_fault_scope_end(p->module);
            if(e) {p->callback_status=status(NSO_RETAINED,0,e);p->diagnostic_retained=1;}
        }
        guest_tp_unbind_current();
    }
    if(p->os_thread)cleanup_os_primary(p);
    pthread_mutex_lock(&p->mutex);
    if(p->active_identity) {p->active_identity->active=0;p->active_identity=NULL;}
    p->thread.state=THREAD_RETURNED;
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
    if(p->tls_allocator_busy || p->tls_allocations || p->tls_allocation_count) {
        fprintf(stderr,"[tls-allocator-retained] allocations=%zu busy=%d; module/callbacks/pages remain owned, no host free\n",p->tls_allocation_count,p->tls_allocator_busy);
        pthread_mutex_unlock(&p->mutex);return status(NSO_RETAINED,EBUSY,0);
    }
    if(p->active_identity) {pthread_mutex_unlock(&p->mutex);return status(NSO_RETAINED,EBUSY,0);}
    while(p->identities) {
        struct primary_identity *id=p->identities;
        if(id->active || unmap_identity(id->token,id->bytes)) {
            int e=id->active?EBUSY:errno;p->quarantined=1;
            pthread_mutex_unlock(&p->mutex);return status(NSO_RETAINED,0,e);
        }
        p->identities=id->next;free(id);
    }
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
    if(r.status==NSO_OK) {p->tls_allocate_callback=0;p->tls_free_callback=0;}
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
    if(r.status==NSO_OK && p->argument_mapping) {
        if(unmap_arguments(p->argument_mapping,p->argument_bytes))r=status(NSO_RETAINED,0,errno);
        else p->argument_mapping=NULL;
    }
    if(r.status==NSO_OK && p->argument_ticket) {
        r=from_context(p->argument_committed?guest_charge_release(p->argument_ticket):guest_charge_cancel(p->argument_ticket));
        if(r.status==NSO_OK) {p->argument_ticket=NULL;p->argument_bytes=0;p->argument_committed=0;p->argument_count=0;}
    }
    if(r.status==NSO_OK && p->dso.primary_tls_image) {
        nso_module_tls_free(p->dso.primary_tls_image);
        p->dso.primary_tls_image=NULL;
        p->dso.registered=0;
    }
    if(r.status==NSO_OK)r=from_context(guest_context_destroy(p->context));
    if(r.status!=NSO_OK) {p->quarantined=1;pthread_mutex_unlock(&p->mutex);return r;}
    p->context=NULL;pthread_mutex_unlock(&p->mutex);
    pthread_mutex_lock(&exclusive_mutex);exclusive_process=NULL;pthread_mutex_unlock(&exclusive_mutex);
    pthread_mutex_destroy(&p->mutex);free(p);*process=NULL;return status(NSO_OK,0,0);
}
