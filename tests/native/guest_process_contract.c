#define _GNU_SOURCE
#include "../../src/runtime/hle/guest_process.h"
#include "../../src/runtime/hle/guest_context.h"
#include "../../src/runtime/hle/nso_host.h"
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_vm.h>
#endif
#define CHECK(x) do {if(!(x)) {fprintf(stderr,"process contract line=%d: %s\n",__LINE__,#x);return 1;}} while(0)
static size_t page;
static unsigned errors,calls,extension_calls;
static int returned_token;
static uint64_t extension(uint32_t i,const char *n) {(void)i;(void)n;extension_calls++;return 1;}
static int succeeded(struct nso_result r) {
    if(r.status==NSO_OK)return 1;
    fprintf(stderr,"process status=%d native=%d cleanup=%d\n",r.status,r.native_error,r.cleanup_error);return 0;
}
static int all_bytes(const void *p,size_t n,unsigned char value) {
    const unsigned char *b=p;for(size_t i=0;i<n;i++)if(b[i]!=value)return 0;return 1;
}
static void expect_value(const char *tag,uint64_t actual,uint64_t expected) {
    if(actual!=expected) {
        errors++;
        fprintf(stderr,"[check-fail] %s actual=0x%" PRIx64 " expected=0x%" PRIx64 "\n",tag,actual,expected);
    }
}
static void expect_condition(const char *tag,int condition) {
    if(!condition) {errors++;fprintf(stderr,"[check-fail] %s\n",tag);}
}
static int inaccessible(uintptr_t address) {
#if defined(__APPLE__)
    mach_vm_address_t base=address;mach_vm_size_t bytes=0;
    vm_region_basic_info_data_64_t info;mach_msg_type_number_t count=VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t object=MACH_PORT_NULL;
    kern_return_t r=mach_vm_region(mach_task_self(),&base,&bytes,VM_REGION_BASIC_INFO_64,(vm_region_info_t)&info,&count,&object);
    if(object!=MACH_PORT_NULL)mach_port_deallocate(mach_task_self(),object);
    return r==KERN_SUCCESS && base<=address && address-base<bytes && info.protection==VM_PROT_NONE;
#else
    FILE *maps=fopen("/proc/self/maps","r");if(!maps)return 0;
    char line[1024],permissions[5];uintptr_t base,end;int found=0;
    while(fgets(line,sizeof(line),maps))if(sscanf(line,"%" SCNxPTR "-%" SCNxPTR " %4s",&base,&end,permissions)==3 && base<=address && address<end) {
        found=permissions[0]=='-' && permissions[1]=='-' && permissions[2]=='-';break;
    }
    fclose(maps);return found;
#endif
}
struct callback_input {size_t quota,image,live_page_bytes;};
static void *exercise(guest_process *process,void *opaque) {
    struct callback_input *input=opaque;calls++;
    struct guest_memory_info info;
    memset(&info,0xa5,sizeof(info));
    if(!succeeded(guest_process_memory_info(process,&info))) {expect_condition("query-initial-status",0);return NULL;}
    expect_value("query-total",info.total,input->quota);
    expect_value("query-used",info.used,input->image+page+65536+input->live_page_bytes);
    expect_value("query-heap-capacity",info.heap_capacity,0);
    expect_value("query-heap-blocks",info.heap_blocks,0);
    expect_value("query-baseline",info.baseline,input->image+page);
    expect_value("query-stack-bytes",info.stack_bytes,65536);
    expect_value("query-thread-count",info.thread_objects,1);
    expect_condition("query-initial-padding",all_bytes((unsigned char *)&info+0x34,sizeof(info)-0x34,0xa5));
    uintptr_t stack_low,stack_high,sp;
#if defined(__APPLE__)
    stack_high=(uintptr_t)pthread_get_stackaddr_np(pthread_self());
    stack_low=stack_high-pthread_get_stacksize_np(pthread_self());
#else
    pthread_attr_t attr;void *stack;size_t bytes;
    int getattr_error=pthread_getattr_np(pthread_self(),&attr);
    if(getattr_error) {fprintf(stderr,"[check-fail] pthread-getattr error=%d\n",getattr_error);errors++;return NULL;}
    int getstack_error=pthread_attr_getstack(&attr,&stack,&bytes);
    int attr_destroy_error=pthread_attr_destroy(&attr);
    if(getstack_error) {fprintf(stderr,"[check-fail] pthread-getstack error=%d\n",getstack_error);errors++;return NULL;}
    expect_value("pthread-attr-destroy",(uint64_t)attr_destroy_error,0);
    stack_low=(uintptr_t)stack;stack_high=stack_low+bytes;
#endif
#if defined(__aarch64__)
    __asm__ volatile("mov %0, sp":"=r"(sp));
#else
#error "this ordinary SP contract requires the selected AArch64 target"
#endif
    fprintf(stderr,"[check-values] native-stack call=%u low=0x%" PRIxPTR " high=0x%" PRIxPTR
        " reported_bytes=%" PRIuPTR " expected_owned_bytes=65536 sp=0x%" PRIxPTR " local=0x%" PRIxPTR "\n",
        calls,stack_low,stack_high,stack_high-stack_low,sp,(uintptr_t)&info);
    struct guest_primary_stack owned;
    struct nso_result owned_status=guest_process_primary_stack(process,&owned);
    expect_condition("owned-stack-descriptor",succeeded(owned_status));
    if(owned_status.status!=NSO_OK)return NULL;
    fprintf(stderr,"[check-values] owned-stack logical=(0x%" PRIxPTR ",%zu) raw=(0x%" PRIxPTR ",%zu) native=(0x%" PRIxPTR ",%zu) admin=%zu attempts=%u\n",
        owned.address,owned.bytes,owned.allocation_address,owned.allocation_bytes,
        owned.native_address,owned.native_bytes,owned.administrative_bytes,owned.capacity_attempts);
    expect_value("logical-guest-stack-bytes",owned.bytes,65536);
    expect_condition("pthread-capacity-at-least-logical",stack_high-stack_low>=owned.bytes);
    expect_value("pthread-reported-native-base",stack_low,owned.native_address);
    expect_value("pthread-reported-native-bytes",stack_high-stack_low,owned.native_bytes);
    expect_condition("logical-window-in-native",owned.address>=stack_low && owned.address+owned.bytes==stack_high);
    expect_condition("native-in-raw-allocation",stack_low>=owned.allocation_address && stack_high<=owned.allocation_address+owned.allocation_bytes);
    expect_value("raw-logical-admin-accounting",owned.allocation_bytes,owned.bytes+owned.administrative_bytes);
    expect_condition("bounded-admin",owned.administrative_bytes<=4*page && owned.capacity_attempts<=3);
    expect_condition("sp-in-exact-logical-window",sp>=owned.address && sp-owned.address<owned.bytes);
    expect_condition("local-in-exact-logical-window",(uintptr_t)&info>=owned.address && (uintptr_t)&info-owned.address<owned.bytes);
    expect_condition("raw-lower-guard",inaccessible(owned.allocation_address-owned.guard_bytes));
    expect_condition("raw-upper-guard",inaccessible(owned.allocation_address+owned.allocation_bytes));
    expect_condition("sp-in-pthread-reported-stack",sp>=stack_low && sp<stack_high);
    expect_condition("local-in-pthread-reported-stack",(uintptr_t)&info>=stack_low && (uintptr_t)&info<stack_high);
    stack_t alternate;
    memset(&alternate,0,sizeof(alternate));
    int alternate_result=sigaltstack(NULL,&alternate),alternate_error=errno;
    fprintf(stderr,"[check-values] altstack result=%d errno=%d address=%p bytes=%zu flags=0x%x\n",
        alternate_result,alternate_error,alternate.ss_sp,alternate.ss_size,alternate.ss_flags);
    expect_value("altstack-read-result",(uint64_t)alternate_result,0);
    expect_condition("altstack-enabled",!(alternate.ss_flags&SS_DISABLE));
    typedef uint32_t (*allocate_fn)(uintptr_t *,size_t);
    typedef uint32_t (*free_fn)(uintptr_t);
    allocate_fn allocate=(allocate_fn)(uintptr_t)guest_process_resolve(0,"_ZN2nn2os21AllocateAddressRegionEPmm");
    free_fn release=(free_fn)(uintptr_t)guest_process_resolve(0,"_ZN2nn2os17FreeAddressRegionEm");
    uintptr_t first=0,second=0;
    expect_condition("va-allocate-binding",allocate!=NULL);expect_condition("va-free-binding",release!=NULL);
    if(allocate && release) {
        expect_value("va-allocate-first-result",allocate(&first,2*page),0);
        expect_value("va-allocate-second-result",allocate(&second,2*page),0);
    }
    struct guest_memory_info after;
    struct nso_result after_result=guest_process_memory_info(process,&after);
    expect_condition("query-after-va-status",succeeded(after_result));
    if(after_result.status==NSO_OK)expect_condition("query-after-va-unchanged",!memcmp(&info,&after,0x34));
    if(second && release)expect_value("va-free-second-result",release(second),0);
    /* first remains genuinely owned PROT_NONE; process destroy must release it. */
    typedef void (*query_fn)(struct guest_memory_info *);
    query_fn query=(query_fn)(uintptr_t)guest_process_resolve(0,"_ZN2nn2os15QueryMemoryInfoEPNS0_10MemoryInfoE");
    expect_condition("query-guest-binding",query!=NULL);
    if(query) {memset(&after,0x5a,sizeof(after));query(&after);
        expect_condition("query-guest-fields",!memcmp(&after,&info,0x34));
        expect_condition("query-guest-padding",all_bytes((unsigned char *)&after+0x34,sizeof(after)-0x34,0x5a));}
    typedef uint32_t (*pages_fn)(uintptr_t,size_t);
    pages_fn map=(pages_fn)(uintptr_t)guest_process_resolve(0,"_ZN2nn2os19AllocateMemoryPagesEmm");
    pages_fn unmap=(pages_fn)(uintptr_t)guest_process_resolve(0,"_ZN2nn2os15FreeMemoryPagesEmm");
    expect_condition("pages-map-binding",map!=NULL);expect_condition("pages-free-binding",unmap!=NULL);
    if(first && query && map && unmap) {
        size_t mapped=2*page;
        expect_value("pages-map-result",map(first,mapped),0);
        volatile unsigned char *memory=(volatile unsigned char *)first;
        memory[0]=0x37;memory[mapped-1]=0x81;
        expect_condition("pages-pattern-rw",memory[0]==0x37 && memory[mapped-1]==0x81);
        struct guest_memory_info expected=info;expected.used+=mapped;
        memset(&after,0x5a,sizeof(after));query(&after);
        expect_condition("query-adds-real-page-backing",!memcmp(&after,&expected,0x34));
        expect_condition("query-mapped-padding",all_bytes((unsigned char *)&after+0x34,sizeof(after)-0x34,0x5a));
        expect_value("pages-free-exact-result",unmap(first,mapped),0);
        query(&after);expect_condition("query-refunds-decommitted-pages",!memcmp(&after,&info,0x34));
        expect_value("pages-recommit-result",map(first,mapped),0);
        expect_condition("pages-recommit-zero",memory[0]==0 && memory[mapped-1]==0);
        memory[0]=0x6d;memory[mapped-1]=0x29;
        query(&after);expect_condition("query-retains-live-page-charge",!memcmp(&after,&expected,0x34));
        input->live_page_bytes+=mapped;
        /* Keep a real allocation alive: whole owner destroy must unmap it
         * before refund, not just clear a duplicate profile ledger. */
    }
    return &returned_token;
}
#if defined(NSO_LOADER_TESTING)
struct ownership_probe {
    guest_context *context;guest_charge_ticket *ticket;size_t bytes;
    void *image,*source;int committed,unloads,pending_observed;
    int fail_map,fail_protect,fail_image_unmap,fail_source_unmap;
};
static struct ownership_probe *probe;
static struct nso_result owner_status(struct guest_context_result r) {
    return (struct nso_result){r.status==GUEST_CONTEXT_OK?NSO_OK:r.status==GUEST_CONTEXT_LIMIT?NSO_LIMIT:NSO_OWNER_ERROR,r.native_error,r.cleanup_error};
}
static struct nso_result reserve(void *o,size_t n,void **out) {
    struct ownership_probe *p=o;p->bytes=n;
    struct nso_result r=owner_status(guest_charge_reserve(p->context,n,&p->ticket));
    *out=p->ticket;return r;
}
static struct nso_result commit(void *o,void *ticket) {
    struct ownership_probe *p=o;struct nso_result r=owner_status(guest_charge_commit(ticket));
    if(r.status==NSO_OK)p->committed=1;return r;
}
static struct nso_result drop(void *o,void *ticket,int committed) {
    struct ownership_probe *p=o;
    struct nso_result r=owner_status(committed?guest_charge_release(ticket):guest_charge_cancel(ticket));
    if(r.status==NSO_OK) {p->ticket=NULL;p->committed=0;}return r;
}
static struct nso_result cancel(void *o,void *ticket) {return drop(o,ticket,0);}
static struct nso_result release(void *o,void *ticket) {return drop(o,ticket,1);}
static const struct nso_owner_ops ops={reserve,commit,cancel,release};
static void *map_probe(size_t n,int protection,int fd) {
    if(fd<0 && !probe->image) {
        struct guest_context_usage usage;
        if(guest_context_usage(probe->context,&usage).status!=GUEST_CONTEXT_OK ||
           usage.pending_bytes!=n || usage.charged_bytes || usage.tickets!=1)errors++;
        else probe->pending_observed++;
        if(probe->fail_map) {probe->fail_map=0;errno=ENOMEM;return MAP_FAILED;}
    }
    void *mapped=mmap(NULL,n,protection,MAP_PRIVATE|(fd<0?MAP_ANONYMOUS:0),fd,0);
    if(mapped!=MAP_FAILED) {
        if(fd>=0)probe->source=mapped;
        else if(!probe->image)probe->image=mapped;
    }
    return mapped;
}
static int protect_probe(void *p,size_t n,int protection) {
    if(probe->fail_protect) {probe->fail_protect=0;errno=EACCES;return -1;}
    return mprotect(p,n,protection);
}
static int unmap_probe(void *p,size_t n) {
    if((p==probe->image && probe->fail_image_unmap-- >0) ||
       (p==probe->source && probe->fail_source_unmap-- >0)) {errno=EBUSY;return -1;}
    return munmap(p,n);
}
static void observer(void *user) {
    nso_module *m=user;uint64_t value;
    memcpy(&value,(void *)(uintptr_t)nso_addr(m,0x8208),8);
    if(value!=nso_base(m)+0x1234)errors++;
    probe->unloads++;
}
static int empty_accounting(guest_context *context) {
    struct guest_context_usage usage;
    return guest_context_usage(context,&usage).status==GUEST_CONTEXT_OK &&
        !usage.pending_bytes && !usage.charged_bytes && !usage.tickets;
}
static int owner_contract(const char *path) {
    struct guest_context_config config={1024*1024,65536};
    for(int phase=0;phase<5;phase++) {
        struct ownership_probe state={0};probe=&state;
        CHECK(guest_context_create(&config,&state.context).status==GUEST_CONTEXT_OK);
        if(phase==1)state.fail_map=1;
        if(phase==2) {state.fail_protect=1;state.fail_image_unmap=1;}
        if(phase==3)state.fail_source_unmap=2;
        if(phase==4) {
            guest_context_destroy(state.context);config.quota_bytes=page;
            CHECK(guest_context_create(&config,&state.context).status==GUEST_CONTEXT_OK);
        }
        nso_loader_test_vm(map_probe,protect_probe,unmap_probe);
        char err[256]={0};
        struct nso_load_result loaded=nso_load_file_owned(path,NULL,&ops,&state,err,sizeof(err));
        if(phase==0) {
            CHECK(succeeded(loaded.result) && loaded.module && state.pending_observed==1);
            CHECK(state.bytes==nso_image_size(loaded.module) && state.committed);
            nso_set_unload_observer(loaded.module,observer,loaded.module);
            state.fail_image_unmap=1;
            struct nso_result retained=nso_release(&loaded.module);
            CHECK(retained.status==NSO_RETAINED && retained.cleanup_error==EBUSY && loaded.module && state.unloads==1);
            struct guest_context_usage usage;
            CHECK(guest_context_usage(state.context,&usage).status==GUEST_CONTEXT_OK);
            CHECK(usage.charged_bytes==state.bytes && usage.tickets==1);
        } else if(phase==1) {
            CHECK(loaded.result.status==NSO_LOAD_ERROR && loaded.result.native_error==ENOMEM && !loaded.module && state.pending_observed==1);
        } else if(phase==2) {
            CHECK(loaded.result.status==NSO_RETAINED && loaded.result.native_error==EACCES && loaded.result.cleanup_error==EBUSY && loaded.module && state.ticket);
        } else if(phase==3) {
            CHECK(loaded.result.status==NSO_RETAINED && loaded.module && !state.ticket);
        } else CHECK(loaded.result.status==NSO_LIMIT && !loaded.module && !state.pending_observed);
        CHECK(succeeded(nso_release(&loaded.module)) && !loaded.module);
        CHECK(empty_accounting(state.context));
        CHECK(guest_context_destroy(state.context).status==GUEST_CONTEXT_OK);
        if(!phase)CHECK(state.unloads==1);
        nso_loader_test_vm(NULL,NULL,NULL);
    }
    return 0;
}
#endif
static unsigned executed_calls;
#if defined(NSO_PROFILE_BOUNDARY_TESTING)
static unsigned boundary_protect_failures,boundary_unmap_failures,boundary_unmap_attempts,boundary_unmap_successes;
int guest_boundary_test_protect(void *address,size_t bytes,int protection) {
    if(boundary_protect_failures) {boundary_protect_failures--;errno=EACCES;return -1;}
    return mprotect(address,bytes,protection);
}
int guest_boundary_test_unmap(void *address,size_t bytes) {
    boundary_unmap_attempts++;
    if(boundary_unmap_failures) {boundary_unmap_failures--;errno=EBUSY;return -1;}
    int r=munmap(address,bytes);if(!r)boundary_unmap_successes++;return r;
}
static void *retained_boundary_callback(guest_process *process,void *opaque) {
    (void)opaque;const uint64_t arguments[6]={0};uint64_t value=0xa5a5;
    struct nso_result r=guest_process_call(process,0x20,arguments,0,&value);
    expect_value("boundary-failed-bind-retained",r.status,NSO_RETAINED);
    expect_value("boundary-failed-bind-did-not-execute",value,0xa5a5);
    return &returned_token;
}
static int boundary_retained_contract(const char *path) {
    struct guest_process_config config={1024*1024,65536};guest_process *process=NULL;
    CHECK(succeeded(guest_process_create(&config,&process)));
    char err[128]={0};CHECK(succeeded(guest_process_load(process,path,err,sizeof(err))));
    boundary_protect_failures=1;boundary_unmap_failures=2;
    void *value=NULL;CHECK(succeeded(guest_process_run(process,retained_boundary_callback,NULL,&value)));
    CHECK(value==&returned_token && !errors && boundary_unmap_attempts==1 && !boundary_unmap_successes);
    CHECK(guest_process_destroy(&process).status==NSO_RETAINED && process);
    CHECK(boundary_unmap_attempts==2 && !boundary_unmap_successes);
    CHECK(succeeded(guest_process_destroy(&process)) && !process);
    CHECK(boundary_unmap_attempts==3 && boundary_unmap_successes==1);
    CHECK(succeeded(guest_process_create(&config,&process)) && succeeded(guest_process_destroy(&process)));
    puts("{\"boundary_retained_errors\":0,\"unmap_attempts\":3,\"actual_unmaps\":1,\"guest_executed\":false,\"retry_cleanup\":true}");return 0;
}
#endif
static void *execute_boundary(guest_process *process,void *opaque) {
    unsigned phase=*(unsigned *)opaque;
    const uint64_t arguments[6]={0x1234};uint64_t value=UINT64_C(0xa5a5a5a5);
    for(unsigned i=0;i<2;i++) {
        struct nso_result r=guest_process_call(process,0x20,arguments,1,&value);
#if defined(__ANDROID__) && defined(__aarch64__)
        if(!phase) {expect_value("boundary-call-status",r.status,NSO_OK);expect_value("boundary-result-and-guest-x18",value,0x91);executed_calls++;}
        else {expect_value("boundary-ended-session-rejected",r.status,NSO_UNSUPPORTED);expect_value("boundary-rejected-output",value,UINT64_C(0xa5a5a5a5));}
#else
        (void)phase;expect_value("boundary-host-explicit-unsupported",r.status,NSO_UNSUPPORTED);
        expect_value("boundary-unsupported-output",value,UINT64_C(0xa5a5a5a5));
#endif
    }
    return &returned_token;
}
static int boundary_contract(const char *path) {
    struct guest_process_config config={1024*1024,65536};guest_process *process=NULL;
    CHECK(succeeded(guest_process_create(&config,&process)));
    char err[128]={0};CHECK(succeeded(guest_process_load(process,path,err,sizeof(err))));
    const uint64_t arguments[6]={0};uint64_t untouched=0xa5a5;
    CHECK(guest_process_call(process,0x20,arguments,0,&untouched).status==NSO_INVALID && untouched==0xa5a5);
    for(unsigned phase=0;phase<2;phase++) {
        void *value=NULL;struct nso_result run=guest_process_run(process,execute_boundary,&phase,&value);
#if defined(__ANDROID__) && defined(__aarch64__)
        if(phase) {CHECK(run.status==NSO_UNSUPPORTED && !value);continue;}
#endif
        CHECK(succeeded(run));
        CHECK(value==&returned_token && !errors);
    }
    CHECK(succeeded(guest_process_destroy(&process)) && !process);
    printf("{\"boundary_errors\":%u,\"executed_calls\":%u,\"raw_fallback\":false,\"ended_session_rejected\":%s}\n",errors,executed_calls,
#if defined(__ANDROID__) && defined(__aarch64__)
        "true"
#else
        "null"
#endif
    );
    return 0;
}
#if !defined(__ANDROID__)
static void *register_unsupported_destructor(guest_process *process,void *opaque) {
    (void)opaque;typedef uint32_t(*allocate_fn)(uint32_t *,uint64_t);
    allocate_fn allocate=(allocate_fn)(uintptr_t)guest_process_resolve(0,"_ZN2nn2os15AllocateTlsSlotEPNS0_7TlsSlotEPFvmE");
    uint32_t slot=99;uint32_t result=allocate(&slot,nso_addr(guest_process_module(process),0x20));
    expect_value("os-tls-register-before-unsupported-provider",result,0);
    return &returned_token;
}
static int unsupported_tls_contract(const char *path) {
    struct guest_process_config config={1024*1024,65536};guest_process *process=NULL;
    CHECK(succeeded(guest_process_create(&config,&process)));
    char err[128];CHECK(succeeded(guest_process_load(process,path,err,sizeof(err))));
    void *value=NULL;(void)guest_process_run(process,register_unsupported_destructor,NULL,&value);
    fputs("unexpected return after unsupported guest provider\n",stderr);return 1;
}
#endif
static uint64_t retarget_target(uint64_t a) {return a+1;}
static uint64_t replacement_target(uint64_t a) {return a+2;}
static uint64_t retarget_resolver(uint32_t index,const char *name) {
    (void)index;return !strcmp(name,"host_sum")?(uintptr_t)&retarget_target:0;
}
static unsigned init_count,init_fail_at;
static struct nso_result count_initializer(void *user,nso_module *module,uint64_t off,const uint64_t args[6],uint64_t *value) {
    (void)user;(void)module;
    if((off!=0x20 && off!=0x40 && off!=0x60) || args[0] || args[5])return (struct nso_result){NSO_INVALID,EINVAL,0};
    init_count++;*value=0;return (struct nso_result){init_count==init_fail_at?NSO_OWNER_ERROR:NSO_OK,0,0};
}
static int retarget_contract(const char *path) {
    char err[128];nso_module *module=nso_load_file(path,retarget_resolver,err,sizeof(err));CHECK(module);
    CHECK(nso_bound_import_count(module)==1);
    struct nso_import_target target;CHECK(succeeded(nso_bound_import_at(module,0,&target)));
    CHECK(target.index==1 && target.target==(uintptr_t)&retarget_target && !strcmp(target.name,"host_sum"));
    const uint64_t *slot=(const uint64_t *)(uintptr_t)nso_addr(module,0x8200);
    CHECK(*slot==target.target);target.target=(uintptr_t)&replacement_target;
    struct nso_import_target duplicate[2]={target,target};
    CHECK(nso_retarget_imports(module,duplicate,2).status==NSO_INVALID && *slot==(uintptr_t)&retarget_target);
    CHECK(succeeded(nso_retarget_imports(module,&target,1)) && *slot==target.target);
    CHECK(succeeded(nso_retarget_imports(module,&target,1)) && *slot==target.target);
    *(uint64_t *)slot=(uintptr_t)&retarget_target;
    CHECK(nso_retarget_imports(module,&target,1).status==NSO_RETAINED && *slot==(uintptr_t)&retarget_target);
    CHECK(succeeded(nso_run_init_execute(module,NSO_INIT_ELF,count_initializer,NULL)) && init_count==3);
    init_count=0;CHECK(succeeded(nso_run_init_execute(module,NSO_INIT_DT_INIT_OWNS_ARRAY,count_initializer,NULL)) && init_count==1);
    init_count=0;init_fail_at=2;
    CHECK(nso_run_init_execute(module,NSO_INIT_ELF,count_initializer,NULL).status==NSO_OWNER_ERROR && init_count==2);
    init_fail_at=0;
    CHECK(succeeded(nso_release(&module)) && !module);
    puts("{\"retarget_errors\":0,\"recorded_import_only\":true,\"init_elf_calls\":3,\"init_wrapper_calls\":1}");return 0;
}
static int readonly_retarget_contract(const char *path) {
    char err[128];nso_module *module=nso_load_file(path,retarget_resolver,err,sizeof(err));CHECK(module);
    struct nso_import_target target;CHECK(succeeded(nso_bound_import_at(module,0,&target)));
    const uint64_t *slot=(const uint64_t *)(uintptr_t)nso_addr(module,0x4200);
    CHECK(*slot==(uintptr_t)&retarget_target);target.target=(uintptr_t)&replacement_target;
    CHECK(nso_retarget_imports(module,&target,1).status==NSO_UNSUPPORTED && *slot==(uintptr_t)&retarget_target);
    CHECK(succeeded(nso_release(&module)) && !module);
    puts("{\"readonly_retarget_errors\":0,\"unsupported\":true,\"no_write\":true}");return 0;
}
static void *exercise_os_wrappers(guest_process *process,void *opaque) {
    (void)opaque;
    typedef uint32_t(*allocate_fn)(uint32_t *,uint64_t);typedef uint64_t(*get_fn)(uint32_t);
    typedef void(*set_fn)(uint32_t,uint64_t);typedef void(*free_fn)(uint32_t);
    allocate_fn allocate=(allocate_fn)(uintptr_t)guest_process_resolve(0,"_ZN2nn2os15AllocateTlsSlotEPNS0_7TlsSlotEPFvmE");
    get_fn get=(get_fn)(uintptr_t)guest_process_resolve(0,"_ZN2nn2os11GetTlsValueENS0_7TlsSlotE");
    set_fn set=(set_fn)(uintptr_t)guest_process_resolve(0,"_ZN2nn2os11SetTlsValueENS0_7TlsSlotEm");
    free_fn release=(free_fn)(uintptr_t)guest_process_resolve(0,"_ZN2nn2os11FreeTlsSlotENS0_7TlsSlotE");
    uint32_t slots[16];
    for(unsigned i=0;i<16;i++) {expect_value("os-app-allocate",allocate(&slots[i],0),0);expect_value("os-app-search-order",slots[i],i);expect_value("os-initial-zero",get(slots[i]),0);}
    uint32_t untouched=0xabc;expect_value("os-app-exhaust-sdk-result",allocate(&untouched,0),0x1203);expect_value("os-exhaust-output",untouched,0xabc);
    set(slots[0],0x99);release(slots[0]);expect_value("os-free-retains-value-no-hostfree",get(slots[0]),0x99);
    expect_value("os-reallocate",allocate(&slots[0],0),0);expect_value("os-reallocate-clears",get(slots[0]),0);
    for(unsigned i=0;i<16;i++)release(slots[i]);
    struct guest_memory_info info;expect_condition("os-thread-query-still-live",succeeded(guest_process_memory_info(process,&info)) && info.thread_objects==1);
    return &returned_token;
}
static int os_wrapper_contract(const char *path) {
    struct guest_process_config config={1024*1024,65536};guest_process *process=NULL;
    CHECK(succeeded(guest_process_create(&config,&process)));char err[128];
    CHECK(succeeded(guest_process_load(process,path,err,sizeof(err))));
    for(unsigned i=0;i<2;i++) {void *value=NULL;CHECK(succeeded(guest_process_run(process,exercise_os_wrappers,NULL,&value)));CHECK(value==&returned_token && !errors);}
    CHECK(succeeded(guest_process_destroy(&process)) && !process);
    puts("{\"os_wrapper_errors\":0,\"native_primary_records\":2,\"exhaust_result\":4611,\"free_keeps_value\":true,\"no_hostfree\":true}");return 0;
}
struct os_fixture_output {
    uint64_t initial,stale,cleared,calls_a,calls_b,first_a,second_a,value_b,seen_a;
    uint64_t thread_count,stack_bytes,order,errors,returned;
    uint64_t argument_count,argument_vector,argument_byte,expected_argument_vector;
};
_Static_assert(offsetof(struct os_fixture_output,returned)==104 && offsetof(struct os_fixture_output,order)==88,"self-authored fixture output ABI");
static void *execute_os_fixture(guest_process *process,void *opaque) {
    struct os_fixture_output *output=opaque;
    typedef char **(*vector_fn)(void);
    vector_fn get_vector=(vector_fn)(uintptr_t)guest_process_resolve(0,"_ZN2nn2os11GetHostArgvEv");
    output->expected_argument_vector=(uintptr_t)get_vector();
    const uint64_t arguments[6]={(uintptr_t)output};uint64_t value=0;
    struct nso_result r=guest_process_call(process,0x20,arguments,1,&value);
#if defined(__ANDROID__) && defined(__aarch64__)
    expect_value("os-fixture-entry-status",r.status,NSO_OK);expect_value("os-fixture-entry-result",value,0x91);
    expect_value("os-callback-deferred-a",output->calls_a,0);expect_value("os-callback-deferred-b",output->calls_b,0);
    expect_value("os-entry-returned",output->returned,1);
#else
    expect_value("os-fixture-unsupported",r.status,NSO_UNSUPPORTED);
#endif
    return &returned_token;
}
static int os_callback_contract(const char *path) {
    struct guest_process_config config={1024*1024,65536};guest_process *process=NULL;
    const char *arguments[]={"ffrs","callback-lifetime"};
    CHECK(succeeded(guest_process_create_with_arguments(&config,2,arguments,&process)));char err[128];
    CHECK(succeeded(guest_process_load(process,path,err,sizeof(err))));
    struct os_fixture_output output={0};void *value=NULL;
    CHECK(succeeded(guest_process_run(process,execute_os_fixture,&output,&value)) && value==&returned_token && !errors);
#if defined(__ANDROID__) && defined(__aarch64__)
    CHECK(output.initial==0 && output.stale==0x99 && output.cleared==0);
    CHECK(output.calls_a==2 && output.calls_b==1 && output.first_a==0 && output.second_a==5 && output.value_b==9);
    CHECK(output.argument_count==2 && output.argument_vector==output.expected_argument_vector && output.argument_byte=='c');
    CHECK(output.seen_a==0 && output.thread_count==1 && output.stack_bytes==65536 && output.order==0x121 && !output.errors);
#endif
    CHECK(succeeded(guest_process_destroy(&process)) && !process);
    printf("{\"os_callback_errors\":%u,\"calls_a\":%" PRIu64 ",\"calls_b\":%" PRIu64 ",\"first_a\":%" PRIu64 ",\"second_a\":%" PRIu64 ",\"value_b\":%" PRIu64 ",\"order\":%" PRIu64 "}\n",errors,output.calls_a,output.calls_b,output.first_a,output.second_a,output.value_b,output.order);return 0;
}
/* The ordinary storage contract and the self-authored guest fixture exercise
 * the same getters. A borrowed vector never transfers its allocation owner. */
struct argument_input {size_t count,bytes;const char *expected[3];char **borrowed;int native,mutate;};
static void *exercise_arguments(guest_process *process,void *opaque) {
    struct argument_input *input=opaque;
    typedef int (*count_fn)(void);typedef char **(*vector_fn)(void);
    count_fn get_count=(count_fn)(uintptr_t)guest_process_resolve(0,"_ZN2nn2os11GetHostArgcEv");
    vector_fn get_vector=(vector_fn)(uintptr_t)guest_process_resolve(0,"_ZN2nn2os11GetHostArgvEv");
    expect_condition("argv-getters-bound",get_count && get_vector);
    if(!get_count || !get_vector)return NULL;
    expect_value("argv-owned-count",(uint64_t)get_count(),input->count);
    char **vector=get_vector();input->borrowed=vector;
    expect_condition("argv-nonnull-even-empty",vector!=NULL);
    if(!vector)return NULL;
    expect_condition("argv-null-tail",vector[input->count]==NULL);
    expect_condition("argv-stable-vector",get_vector()==vector);
    for(size_t i=0;i<input->count;i++) {
        expect_condition("argv-copied-string",!strcmp(vector[i],input->expected[i]));
        expect_condition("argv-string-in-owned-extent",(uintptr_t)vector[i]>=(uintptr_t)vector+(input->count+1)*sizeof(*vector) &&
            (uintptr_t)vector[i]-(uintptr_t)vector<input->bytes && strlen(vector[i])+1<=input->bytes-((uintptr_t)vector[i]-(uintptr_t)vector));
    }
    struct guest_memory_info info;memset(&info,0xa5,sizeof(info));
    expect_condition("argv-query-status",succeeded(guest_process_memory_info(process,&info)));
    size_t image=nso_image_size(guest_process_module(process));
    expect_value("argv-query-backing-charge",info.used,image+65536+input->bytes);
    expect_value("argv-query-baseline",info.baseline,image+input->bytes);
    expect_condition("argv-query-padding",all_bytes((unsigned char *)&info+0x34,sizeof(info)-0x34,0xa5));
    if(input->native) {
        uint64_t output[3]={UINT64_C(0xa5a5),UINT64_C(0xa5a5),UINT64_C(0xa5a5)};
        const uint64_t arguments[6]={(uintptr_t)output};uint64_t returned=0xa5a5;
        struct nso_result r=guest_process_call(process,0x20,arguments,1,&returned);
#if defined(__ANDROID__) && defined(__aarch64__)
        expect_value("argv-native-call",r.status,NSO_OK);
        expect_value("argv-native-return",returned,0x91);
        expect_value("argv-native-w0-count",output[0],input->count);
        expect_value("argv-native-x0-vector",output[1],(uintptr_t)vector);
        expect_value("argv-native-guest-x18-preserved",output[2],0x55);
        executed_calls++;
#else
        expect_value("argv-native-host-unsupported",r.status,NSO_UNSUPPORTED);
        expect_value("argv-no-raw-fallback",returned,0xa5a5);
        expect_value("argv-no-guest-output",output[0],0xa5a5);
#endif
    }
    if(input->mutate && input->count) {
        vector[0][0]='X';vector[0]=vector[input->count-1];
        expect_value("argv-count-independent-of-mutable-vector",(uint64_t)get_count(),input->count);
        expect_condition("argv-base-independent-of-mutable-vector",get_vector()==vector);
    }
    return &returned_token;
}
static int argument_contract(const char *path,int native) {
    struct guest_process_config config={1024*1024,65536};
    for(unsigned phase=0;phase<3;phase++) {
        char first[]="ffrs",last[]="option=123";const char *source[]={first,"",last};
        char *large=NULL;
        struct argument_input input={.count=phase==0?0:phase==1?3:1,.bytes=page,.native=native,.mutate=phase==1};
        if(phase==1) {input.expected[0]="ffrs";input.expected[1]="";input.expected[2]="option=123";}
        if(phase==2) {
            large=malloc(page+14);CHECK(large);memset(large,'q',page+13);large[page+13]=0;
            source[0]=large;input.expected[0]=large;input.bytes=2*page;
        }
        guest_process *process=NULL;
        CHECK(succeeded(guest_process_create_with_arguments(&config,input.count,input.count?source:NULL,&process)));
        if(phase==1) {first[0]='!';last[0]='!';source[0]="caller vector changed";}
        char err[128]={0};CHECK(succeeded(guest_process_load(process,path,err,sizeof(err))));
        void *value=NULL;CHECK(succeeded(guest_process_run(process,exercise_arguments,&input,&value)));
        CHECK(value==&returned_token && !errors);
        /* Guest mutations above must not change the private munmap base. */
        CHECK(succeeded(guest_process_destroy(&process)) && !process);free(large);
    }
    printf("{\"argument_errors\":%u,\"cases\":3,\"empty_charged_page\":true,\"copied_mutable_vector\":true,\"null_tail\":true,\"multi_page\":true,\"native_calls\":%u,\"raw_fallback\":false}\n",errors,executed_calls);return 0;
}
#if defined(NSO_PROFILE_ARGUMENT_TESTING)
static unsigned argument_maps,argument_unmaps,argument_actual_unmaps;
static int argument_fail_map,argument_fail_unmap;
static void *argument_raw_base;
static void *argument_map_probe(size_t bytes,size_t pending) {
    argument_maps++;expect_value("argv-pending-before-mmap",pending,bytes);
    if(argument_fail_map) {argument_fail_map=0;errno=ENOMEM;return MAP_FAILED;}
    void *result=mmap(NULL,bytes,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if(result!=MAP_FAILED)argument_raw_base=result;
    return result;
}
static int argument_unmap_probe(void *address,size_t bytes) {
    argument_unmaps++;expect_condition("argv-unmap-private-base",address==argument_raw_base);
    expect_value("argv-unmap-rounded-extent",bytes,page);
    if(argument_fail_unmap) {argument_fail_unmap=0;errno=EBUSY;return -1;}
    int r=munmap(address,bytes);if(!r)argument_actual_unmaps++;return r;
}
static int argument_recovery_contract(const char *path) {
    struct guest_process_config config={1024*1024,65536};guest_process *process=NULL,*extra=NULL;
    const char *source[]={"retained-argv"};
    guest_process_test_argument_vm(argument_map_probe,argument_unmap_probe);
    CHECK(guest_process_create_with_arguments(&config,(size_t)INT_MAX+1,NULL,&process).status==NSO_INVALID && !process);
    CHECK(guest_process_create_with_arguments(&config,1,NULL,&process).status==NSO_INVALID && !process);
    const char *bad[]={NULL};
    CHECK(guest_process_create_with_arguments(&config,1,bad,&process).status==NSO_INVALID && !process);
    struct guest_process_config limited={page-1,65536};
    CHECK(guest_process_create(&limited,&process).status==NSO_LIMIT && !process && !argument_maps);
    argument_fail_map=1;
    struct nso_result r=guest_process_create_with_arguments(&config,1,source,&process);
    CHECK(r.status==NSO_OWNER_ERROR && r.native_error==ENOMEM && !process && argument_maps==1 && !argument_unmaps);
    CHECK(succeeded(guest_process_create_with_arguments(&config,1,source,&process)));
    char err[128]={0};CHECK(succeeded(guest_process_load(process,path,err,sizeof(err))));
    struct argument_input input={.count=1,.bytes=page,.expected={"retained-argv"}};
    void *value=NULL;CHECK(succeeded(guest_process_run(process,exercise_arguments,&input,&value)) && value==&returned_token && !errors);
    argument_fail_unmap=1;r=guest_process_destroy(&process);
    CHECK(r.status==NSO_RETAINED && r.cleanup_error==EBUSY && process && argument_unmaps==1 && !argument_actual_unmaps);
    CHECK(input.borrowed==argument_raw_base && !strcmp(input.borrowed[0],"retained-argv") && !input.borrowed[1]);
    CHECK(guest_process_create(&config,&extra).status!=NSO_OK && !extra);
    CHECK(succeeded(guest_process_destroy(&process)) && !process && argument_unmaps==2 && argument_actual_unmaps==1);
    guest_process_test_argument_vm(NULL,NULL);
    CHECK(succeeded(guest_process_create(&config,&process)) && succeeded(guest_process_destroy(&process)));
    CHECK(!errors);
    puts("{\"argument_recovery_errors\":0,\"precharge_observed\":true,\"quota_refusal_before_map\":true,\"map_failure_cancelled\":true,\"retained_readable\":true,\"unmap_attempts\":2,\"actual_unmaps\":1,\"retry_cleanup\":true}");return 0;
}
#endif
int main(int argc,char **argv) {
    page=(size_t)sysconf(_SC_PAGESIZE);
    if(argc==3 && !strcmp(argv[1],"--arguments"))return argument_contract(argv[2],0);
    if(argc==3 && !strcmp(argv[1],"--arguments-native"))return argument_contract(argv[2],1);
#if defined(NSO_PROFILE_ARGUMENT_TESTING)
    if(argc==3 && !strcmp(argv[1],"--arguments-recovery"))return argument_recovery_contract(argv[2]);
#endif
#if defined(NSO_PROFILE_BOUNDARY_TESTING)
    if(argc==3 && !strcmp(argv[1],"--boundary-retained"))return boundary_retained_contract(argv[2]);
#endif
    if(argc==3 && !strcmp(argv[1],"--boundary"))return boundary_contract(argv[2]);
    if(argc==3 && !strcmp(argv[1],"--retarget"))return retarget_contract(argv[2]);
    if(argc==3 && !strcmp(argv[1],"--retarget-readonly"))return readonly_retarget_contract(argv[2]);
    if(argc==3 && !strcmp(argv[1],"--os-tls"))return os_wrapper_contract(argv[2]);
    if(argc==3 && !strcmp(argv[1],"--os-tls-callback"))return os_callback_contract(argv[2]);
#if !defined(__ANDROID__)
    if(argc==3 && !strcmp(argv[1],"--tls-unsupported"))return unsupported_tls_contract(argv[2]);
#endif
    if(argc==2 && !strcmp(argv[1],"--invalid-query")) {
        struct guest_memory_info untouched;memset(&untouched,0xa5,sizeof(untouched));
        typedef void (*query_fn)(struct guest_memory_info *);
        query_fn query=(query_fn)(uintptr_t)guest_process_resolve(0,"_ZN2nn2os15QueryMemoryInfoEPNS0_10MemoryInfoE");
        query(&untouched);return 9;
    }
    CHECK(argc==2 && page>0);
#if defined(NSO_LOADER_TESTING)
    CHECK(!owner_contract(argv[1]));
#endif
    nso_host_set_extension_resolver(extension);
    const char *const rejected[]={"strdup","malloc","calloc","mmap","mprotect","pthread_create",
        "nvnBootstrapLoader","__nnmusl_init_dso","__cxa_finalize","exit",NULL};
    for(size_t i=0;rejected[i];i++)CHECK(!guest_process_resolve(0,rejected[i]));
    CHECK(!extension_calls);
    guest_process *process=NULL,*extra=NULL;
    struct guest_process_config config={1024*1024,65536};
    CHECK(succeeded(guest_process_create(&config,&process)));
    CHECK(guest_process_create(&config,&extra).status!=NSO_OK && !extra);
    struct guest_memory_info untouched;
    memset(&untouched,0xa5,sizeof(untouched));
    CHECK(guest_process_memory_info(process,&untouched).status==NSO_INVALID && all_bytes(&untouched,sizeof(untouched),0xa5));
    char err[256]={0};CHECK(succeeded(guest_process_load(process,argv[1],err,sizeof(err))));
    struct callback_input input={.quota=config.quota_bytes,.image=nso_image_size(guest_process_module(process))};
    struct sigaction before[4],after;const int sigs[4]={SIGSEGV,SIGBUS,SIGILL,SIGTRAP};
    for(int i=0;i<4;i++)CHECK(!sigaction(sigs[i],NULL,&before[i]));
    for(int i=0;i<2;i++) {
        void *value=NULL;CHECK(succeeded(guest_process_run(process,exercise,&input,&value)));
        fprintf(stderr,"[check-values] callback-joined run=%d returned=%p expected=%p errors=%u calls=%u\n",
            i,value,(void *)&returned_token,errors,calls);
        CHECK(value==&returned_token && !errors);
        memset(&untouched,0xa5,sizeof(untouched));
        CHECK(guest_process_memory_info(process,&untouched).status==NSO_INVALID && all_bytes(&untouched,sizeof(untouched),0xa5));
        for(int j=0;j<4;j++) {CHECK(!sigaction(sigs[j],NULL,&after));CHECK(before[j].sa_sigaction==after.sa_sigaction && before[j].sa_flags==after.sa_flags);}
    }
    CHECK(succeeded(guest_process_destroy(&process)) && !process);
    CHECK(succeeded(guest_process_create(&config,&process)) && succeeded(guest_process_destroy(&process)));
    nso_host_set_extension_resolver(NULL);
    CHECK(!extension_calls && !errors && calls==2 && input.live_page_bytes==4*page);
    printf("{\"errors\":%u,\"calls\":%u,\"extension_calls\":%u,\"stack_bytes\":65536,\"query_fields\":7,\"padding_preserved\":true,\"thread_record_joined\":true,\"signal_dispositions_restored\":true,\"pages_accounted\":true,\"recommit_zero\":true,\"whole_owner_release\":true,\"cleanup_failure_cases\":%d}\n",
        errors,calls,extension_calls,
#if defined(NSO_LOADER_TESTING)
        5
#else
        0
#endif
    );return 0;
}
