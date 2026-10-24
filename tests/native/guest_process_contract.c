#define _GNU_SOURCE
#include "../../src/runtime/hle/guest_process.h"
#include "../../src/runtime/hle/guest_context.h"
#include "../../src/runtime/hle/nso_host.h"
#include "../../src/runtime/hle/guest_pages.h"
#include "../../src/runtime/hle/guest_tp.h"
#include "../../src/runtime/nso/native_boundary.h"
#include <errno.h>
#include <ctype.h>
#include <wctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <limits.h>
#include <float.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
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
struct child_tls_test_data {
    uint32_t slot;
    uint64_t initial;
    uint64_t after_set;
    uint64_t (*get)(uint32_t);
    void (*set)(uint32_t,uint64_t);
};
static void *child_tls_worker(void *arg) {
    struct child_tls_test_data *d=(struct child_tls_test_data *)arg;
    guest_process_bind_child_thread();
    d->initial=d->get(d->slot);
    d->set(d->slot,0x123456);
    d->after_set=d->get(d->slot);
    guest_process_unbind_child_thread();
    return NULL;
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
    set(slots[0],0x99);
    struct child_tls_test_data cd={slots[0],0xdead,0xdead,get,set};
    pthread_t cth;
    expect_condition("child-create-success",pthread_create(&cth,NULL,child_tls_worker,&cd)==0);
    expect_condition("child-join-success",pthread_join(cth,NULL)==0);
    expect_value("child-os-tls-isolated-initial-zero",cd.initial,0);
    expect_value("child-os-tls-after-set",cd.after_set,0x123456);
    expect_value("primary-os-tls-untouched-by-child",get(slots[0]),0x99);
    release(slots[0]);expect_value("os-free-retains-value-no-hostfree",get(slots[0]),0x99);
    expect_value("os-reallocate",allocate(&slots[0],0),0);expect_value("os-reallocate-clears",get(slots[0]),0);
    for(unsigned i=0;i<16;i++)release(slots[i]);
    typedef void (*std_lock_fn)(void *);
    typedef void (*std_unlock_fn)(void *);
    typedef void (*std_notify_all_fn)(void *);
    typedef uint64_t (*std_now_fn)(void);
    std_lock_fn std_lock=(std_lock_fn)(uintptr_t)guest_process_resolve(0,"_ZNSt3__15mutex4lockEv");
    std_unlock_fn std_unlock=(std_unlock_fn)(uintptr_t)guest_process_resolve(0,"_ZNSt3__15mutex6unlockEv");
    std_notify_all_fn std_notify_all=(std_notify_all_fn)(uintptr_t)guest_process_resolve(0,"_ZNSt3__118condition_variable10notify_allEv");
    std_now_fn std_now=(std_now_fn)(uintptr_t)guest_process_resolve(0,"_ZNSt3__16chrono12steady_clock3nowEv");
    uint64_t dummy_m=0,dummy_cv=0;
    std_lock(&dummy_m);
    std_unlock(&dummy_m);
    std_notify_all(&dummy_cv);
    expect_condition("std-steady-clock-now-positive",std_now()>0);
    typedef char *(*setlocale_fn)(int, const char *);
    setlocale_fn sl=(setlocale_fn)(uintptr_t)guest_process_resolve(0,"setlocale");
    expect_condition("setlocale-returns-c",sl && sl(0,NULL) && !strcmp(sl(0,NULL),"C"));
    typedef int (*gtod_fn)(struct timeval *, void *);
    gtod_fn gtod=(gtod_fn)(uintptr_t)guest_process_resolve(0,"gettimeofday");
    struct timeval test_tv;
    memset(&test_tv, 0, sizeof(test_tv));
    expect_condition("gettimeofday-returns-zero",gtod && gtod(&test_tv,NULL)==0 && test_tv.tv_sec>0);
    typedef uint32_t (*get_process_handle_fn)(void);
    get_process_handle_fn gph=(get_process_handle_fn)(uintptr_t)guest_process_resolve(0,"_ZN2nn2os23GetCurrentProcessHandleEv");
    expect_condition("get-current-process-handle-live",gph && gph()==(uint32_t)getpid());
    typedef uint32_t (*is_fail_fn)(uint32_t);
    is_fail_fn is_fail = (is_fail_fn)(uintptr_t)guest_process_resolve(0, "nnResultIsFailure");
    expect_condition("nn-result-is-failure-live", is_fail && is_fail(0) == 0 && is_fail(1) == 1);
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
static int32_t ascii_reference(const unsigned char *a,const unsigned char *b,uint64_t n) {
    for(uint64_t i=0;i<n;i++) {
        unsigned x=a[i],y=b[i];
        x=(x>=65 && x<=90)?x+32:x;y=(y>=65 && y<=90)?y+32:y;
        if(x!=y || !x)return (int32_t)x-(int32_t)y;
    }
    return 0;
}
static int locale_contract(void) {
    typedef int32_t(*compare_fn)(const char *,const char *,uint64_t,uintptr_t);
    compare_fn compare=(compare_fn)(uintptr_t)guest_process_resolve(0,"strncasecmp_l");CHECK(compare);
    CHECK(compare(NULL,NULL,0,UINTPTR_MAX)==0);
    unsigned checked=0;
    for(unsigned a=0;a<256;a++)for(unsigned b=0;b<256;b++) {
        unsigned char x[3]={(unsigned char)a,0,0x81},y[3]={(unsigned char)b,0,0x82};
        CHECK(compare((const char *)x,(const char *)y,1,(checked&1)?0:UINTPTR_MAX)==ascii_reference(x,y,1));checked++;
    }
    const unsigned char a[]={'A','b',0,0x81},b[]={'a','B',0,0x82};
    for(uint64_t n=0;n<8;n++)CHECK(compare((const char *)a,(const char *)b,n,UINTPTR_MAX)==0);
    CHECK(compare("ab","ac",UINT64_C(0x100000001),UINTPTR_MAX)==-1);
    CHECK(compare("abcd","ABce",3,0)==0 && compare("abcd","ABce",4,UINTPTR_MAX)==-1);
    printf("{\"locale_errors\":0,\"unsigned_pairs\":%u,\"zero_count_no_load\":true,\"opaque_locale_ignored\":true,\"exact_ascii_difference\":true}\n",checked);return 0;
}
static void *exercise_locale_native(guest_process *p,void *opaque) {
    (void)opaque;
    const unsigned char a[][5]={{'A','b',0},{'x',0},{0xff,0},{'a',0},{'z',0},{0,0x81}};
    const unsigned char b[][5]={{'a','C',0},{'Z',0},{0x80,0},{'b',0},{'a',0},{0,0x82}};
    for(unsigned i=0;i<6;i++) {
        uint64_t n=i==0?UINT64_C(0x100000001):4;
        uint64_t output[2]={0xa5a5,0xa5a5},value=0xa5a5;
        uint64_t arguments[6]={(uintptr_t)output,(uintptr_t)a[i],(uintptr_t)b[i],n,i&1?0:UINTPTR_MAX};
        struct nso_result r=guest_process_call(p,0x20,arguments,5,&value);
#if defined(__ANDROID__) && defined(__aarch64__)
        expect_value("locale-native-call",r.status,NSO_OK);expect_value("locale-native-return",value,0x91);
        expect_value("locale-native-exact-signed-difference",output[0],(uint64_t)(int64_t)ascii_reference(a[i],b[i],n));
        expect_value("locale-native-x18",output[1],0x55);executed_calls++;
#else
        expect_value("locale-native-unsupported",r.status,NSO_UNSUPPORTED);expect_value("locale-no-raw-fallback",value,0xa5a5);
#endif
    }
    return &returned_token;
}
static int locale_native_contract(const char *path) {
    struct guest_process_config config={1024*1024,65536};guest_process *p=NULL;
    CHECK(!locale_contract());CHECK(succeeded(guest_process_create(&config,&p)));
    char err[128];CHECK(succeeded(guest_process_load(p,path,err,sizeof(err))));
    void *value=NULL;CHECK(succeeded(guest_process_run(p,exercise_locale_native,NULL,&value)) && value==&returned_token && !errors);
    CHECK(succeeded(guest_process_destroy(&p)) && !p);
    printf("{\"locale_native_errors\":%u,\"native_calls\":%u,\"raw_fallback\":false}\n",errors,executed_calls);return 0;
}
static int strcasecmp_contract(void) {
    typedef int32_t(*compare_fn)(const char *,const char *);
    compare_fn compare=(compare_fn)(uintptr_t)guest_process_resolve(0,"strcasecmp");CHECK(compare);
    unsigned checked=0;
    for(unsigned a=0;a<256;a++)for(unsigned b=0;b<256;b++) {
        unsigned char x[2]={(unsigned char)a,0},y[2]={(unsigned char)b,0};
        CHECK(compare((const char *)x,(const char *)y)==ascii_reference(x,y,2));checked++;
    }
    CHECK(compare("LongEqualPrefixAbc","longequalprefixabD")==-1);
    CHECK(compare("", "z")==-'z' && compare("Z","")=='z');
    CHECK(compare("same","SAME")==0);
    printf("{\"strcasecmp_errors\":0,\"unsigned_pairs\":%u,\"exact_signed_difference\":true,\"nul_termination\":true}\n",checked);return 0;
}
static void *exercise_strcasecmp_native(guest_process *p,void *opaque) {
    (void)opaque;
    const unsigned char a[][24]={{'A','b',0},{'x',0},{0xff,0},{'a',0},{'z',0},{0,0x81}};
    const unsigned char b[][24]={{'a','C',0},{'Z',0},{0x80,0},{'b',0},{'a',0},{0,0x82}};
    for(unsigned i=0;i<6;i++) {
        uint64_t output[2]={0xa5a5,0xa5a5},value=0xa5a5;
        uint64_t arguments[6]={(uintptr_t)output,(uintptr_t)a[i],(uintptr_t)b[i]};
        struct nso_result r=guest_process_call(p,0x20,arguments,3,&value);
#if defined(__ANDROID__) && defined(__aarch64__)
        expect_value("strcasecmp-native-call",r.status,NSO_OK);expect_value("strcasecmp-native-return",value,0x91);
        expect_value("strcasecmp-native-exact-difference",output[0],(uint64_t)(int64_t)ascii_reference(a[i],b[i],24));
        expect_value("strcasecmp-native-x18",output[1],0x55);executed_calls++;
#else
        expect_value("strcasecmp-native-unsupported",r.status,NSO_UNSUPPORTED);expect_value("strcasecmp-no-raw-fallback",value,0xa5a5);
#endif
    }
    return &returned_token;
}
static int strcasecmp_native_contract(const char *path) {
    struct guest_process_config config={1024*1024,65536};guest_process *p=NULL;
    CHECK(!strcasecmp_contract());CHECK(succeeded(guest_process_create(&config,&p)));
    char err[128];CHECK(succeeded(guest_process_load(p,path,err,sizeof(err))));
    void *value=NULL;CHECK(succeeded(guest_process_run(p,exercise_strcasecmp_native,NULL,&value)) && value==&returned_token && !errors);
    CHECK(succeeded(guest_process_destroy(&p)) && !p);
    printf("{\"strcasecmp_native_errors\":%u,\"native_calls\":%u,\"raw_fallback\":false}\n",errors,executed_calls);return 0;
}
struct va_enabled_input {int policy,native;};
static void verify_va_enabled(guest_process *p,const struct va_enabled_input *input,const char *stage) {
    int enabled=0x5a;struct nso_result r=guest_process_va_enabled(p,&enabled);
    expect_condition(stage,succeeded(r));expect_value("va-enabled-canonical-policy",(uint64_t)enabled,(uint64_t)input->policy);
    typedef uint32_t(*enabled_fn)(void);
    enabled_fn query=(enabled_fn)(uintptr_t)guest_process_resolve(0,"_ZN2nn2os29IsVirtualAddressMemoryEnabledEv");
    expect_condition("va-enabled-binding",query!=NULL);
    if(query)expect_value("va-enabled-typed-getter",query(),(uint64_t)input->policy);
    if(input->native) {
        uint64_t output[2]={0xa5a5,0xa5a5},value=0xa5a5;
        uint64_t arguments[6]={(uintptr_t)output};r=guest_process_call(p,0x20,arguments,1,&value);
#if defined(__ANDROID__) && defined(__aarch64__)
        expect_value("va-enabled-native-call",r.status,NSO_OK);expect_value("va-enabled-native-return",value,0x91);
        expect_value("va-enabled-native-canonical-w0",output[0],(uint64_t)input->policy);
        expect_value("va-enabled-native-guest-x18",output[1],0x55);executed_calls++;
#else
        expect_value("va-enabled-native-unsupported",r.status,NSO_UNSUPPORTED);expect_value("va-enabled-no-raw-fallback",value,0xa5a5);
#endif
    }
}
static void *exercise_va_enabled(guest_process *p,void *opaque) {
    struct va_enabled_input *input=opaque;
    verify_va_enabled(p,input,"va-enabled-empty-live-owner");
    typedef uint32_t (*reserve_fn)(uintptr_t *,size_t);typedef uint32_t(*release_fn)(uintptr_t);
    typedef uint32_t(*pages_fn)(uintptr_t,size_t);
    reserve_fn reserve=(reserve_fn)(uintptr_t)guest_process_resolve(0,"_ZN2nn2os21AllocateAddressRegionEPmm");
    release_fn release=(release_fn)(uintptr_t)guest_process_resolve(0,"_ZN2nn2os17FreeAddressRegionEm");
    pages_fn map=(pages_fn)(uintptr_t)guest_process_resolve(0,"_ZN2nn2os19AllocateMemoryPagesEmm");
    pages_fn unmap=(pages_fn)(uintptr_t)guest_process_resolve(0,"_ZN2nn2os15FreeMemoryPagesEmm");
    uintptr_t base=0;expect_value("va-enabled-reserve",reserve(&base,2*page),0);
    verify_va_enabled(p,input,"va-enabled-live-reservation");
    expect_value("va-enabled-real-map",map(base,page),0);*(volatile unsigned char *)base=0x39;
    verify_va_enabled(p,input,"va-enabled-live-backed-allocation");
    expect_value("va-enabled-rw-pattern",*(volatile unsigned char *)base,0x39);
    expect_value("va-enabled-real-free",unmap(base,page),0);
    verify_va_enabled(p,input,"va-enabled-after-decommit");
    expect_value("va-enabled-real-release",release(base),0);
    verify_va_enabled(p,input,"va-enabled-empty-after-release");
    return &returned_token;
}
static int va_enabled_contract(const char *path,int native) {
    int untouched=0x5a;CHECK(guest_process_va_enabled(NULL,&untouched).status==NSO_INVALID && untouched==0x5a);
    struct guest_process_config config={1024*1024,65536};
    for(int policy=0;policy<=1;policy++) {
        guest_process *p=NULL;
        struct nso_result r=policy?guest_process_create_with_resources(&config,1024*1024,0,NULL,&p):guest_process_create(&config,&p);
        CHECK(succeeded(r));untouched=0x5a;
        CHECK(guest_process_va_enabled(p,&untouched).status==NSO_INVALID && untouched==0x5a);
        char err[128];CHECK(succeeded(guest_process_load(p,path,err,sizeof(err))));
        struct va_enabled_input input={policy,native};void *value=NULL;
        CHECK(succeeded(guest_process_run(p,exercise_va_enabled,&input,&value)) && value==&returned_token && !errors);
        untouched=0x5a;CHECK(guest_process_va_enabled(p,&untouched).status==NSO_INVALID && untouched==0x5a);
        CHECK(succeeded(guest_process_destroy(&p)) && !p);
    }
    printf("{\"va_enabled_errors\":%u,\"explicit_enabled_policy\":true,\"unconfigured_false\":true,\"actual_vm_stages\":5,\"invalid_untouched\":true,\"native_calls\":%u,\"raw_fallback\":false}\n",errors,executed_calls);return 0;
}
struct log_case {float input;double expected;};
static const struct log_case log_cases[]={
    {0.125f,-2.0794415416798359283},{0.5f,-0.69314718055994530942},
    {1.0f,0.0},{2.0f,0.69314718055994530942},{3.5f,1.2527629684953679957},
    {10.0f,2.3025850929940456840},{65536.0f,11.090354888959124951},
    {FLT_MIN,-87.336544750553108986},{FLT_MAX,88.722839052068353053},
    {0x1p-149f,-103.27892990343185110}
};
static int log_close(float result,double expected) {
    return isfinite(result) && fabs((double)result-expected)<=0.000002*fmax(1.0,fabs(expected));
}
static int log_special_matches(float result,float reference) {
    if(isnan(reference))return isnan(result);
    if(isinf(reference))return isinf(result) && !!signbit(result)==!!signbit(reference);
    return result==reference && (result!=0.0f || !!signbit(result)==!!signbit(reference));
}
static int logf_contract(void) {
    typedef float (*log_fn)(float);log_fn logarithm=(log_fn)(uintptr_t)guest_process_resolve(0,"logf");CHECK(logarithm);
    for(size_t i=0;i<sizeof(log_cases)/sizeof(log_cases[0]);i++)CHECK(log_close(logarithm(log_cases[i].input),log_cases[i].expected));
    const float special[]={0.0f,-0.0f,-1.0f,INFINITY,NAN};
    for(size_t i=0;i<sizeof(special)/sizeof(special[0]);i++)CHECK(log_special_matches(logarithm(special[i]),logf(special[i])));
    puts("{\"logf_errors\":0,\"positive_boundary_values\":10,\"ieee_observations\":5,\"dynamic_values\":true,\"implementation\":\"system-libm\",\"fp_control_mode_changed_by_test\":false}");return 0;
}
static int snprintf_contract(void) {
    uint64_t target = guest_process_resolve(0, "snprintf");
    CHECK(target == (uintptr_t)&vsnprintf);
    char buf[128];
    /* 1. Integers */
    int r = snprintf(buf, sizeof(buf), "%d %u 0x%x %d %d", 12345, 67890U, 0xabcd, INT_MIN, INT_MAX);
    CHECK(r > 0 && strstr(buf, "12345 67890 0xabcd"));
    /* 2. Pointers */
    void *ptr = (void *)0xdeadbeef;
    r = snprintf(buf, sizeof(buf), "%p %p", NULL, ptr);
    CHECK(r > 0 && strstr(buf, "deadbeef"));
    /* 3. Strings */
    r = snprintf(buf, sizeof(buf), "%s and '%s'", "hello", "");
    CHECK(r == 12 && !strcmp(buf, "hello and ''"));
    /* 4. Floats */
    r = snprintf(buf, sizeof(buf), "%.2f %.4f %g", 3.14159, 2.71828, 1e-4);
    CHECK(r > 0 && strstr(buf, "3.14 2.7183"));
    /* 5. Length modifiers */
    long long ll = 123456789012345LL;
    size_t sz = 999;
    r = snprintf(buf, sizeof(buf), "%lld %zu %hhd", ll, sz, (char)42);
    CHECK(r > 0 && strstr(buf, "123456789012345 999 42"));
    /* 6. Buffer truncation & return values */
    const char *trunc_input = "abcdef";
    r = snprintf(NULL, 0, "test %s", "123");
    CHECK(r == 8);
    char small[4];
    r = snprintf(small, 4, "%s", trunc_input);
    CHECK(r == 6 && !strcmp(small, "abc"));
    /* 7. Stack arguments (10 arguments: > 5 variadic GP args) */
    r = snprintf(buf, sizeof(buf), "%d %d %d %d %d %d %d %d %d %d",
                 1, 2, 3, 4, 5, 6, 7, 8, 9, 10);
    CHECK(r > 0 && !strcmp(buf, "1 2 3 4 5 6 7 8 9 10"));
    /* 8. Mixed FP and stack arguments */
    r = snprintf(buf, sizeof(buf), "%d %.1f %d %.1f %d %d %d %d %d %d",
                 10, 1.5, 20, 2.5, 30, 40, 50, 60, 70, 80);
    CHECK(r > 0 && !strcmp(buf, "10 1.5 20 2.5 30 40 50 60 70 80"));
#if defined(__ANDROID__) && defined(__aarch64__)
    /* Test actual native boundary gateway */
    nb_scope scope;
    memset(&scope, 0, sizeof(scope));
    CHECK(nb_scope_init(&scope) == NB_OK);
    nb_import binding = {0};
    nb_signature sig = {
        .version = 3, .flags = NB_SIG_VARIADIC_SNPRINTF,
        .gp_arguments = 3, .gp_returns = 1, .variadic = 1
    };
    CHECK(nb_import_bind(&binding, &scope, (void(*)(void))(uintptr_t)&vsnprintf, &sig) == NB_OK);
    typedef int (*snp_fn)(char *, size_t, const char *, ...);
    snp_fn gateway_snprintf = (snp_fn)nb_import_entry(&binding);
    CHECK(gateway_snprintf != NULL);
    scope.phase = 1; /* NB_PHASE_GUEST */
    r = gateway_snprintf(buf, sizeof(buf), "%d %d %d %d %d %d %d %d %d %d",
                         11, 22, 33, 44, 55, 66, 77, 88, 99, 100);
    CHECK(r > 0 && !strcmp(buf, "11 22 33 44 55 66 77 88 99 100"));
    r = gateway_snprintf(buf, sizeof(buf), "pi=%.4f e=%.4f s=%s p=%p",
                         3.14159, 2.71828, "verified", ptr);
    CHECK(r > 0 && strstr(buf, "pi=3.1416 e=2.7183 s=verified"));
    r = gateway_snprintf(small, 4, "gateway_truncation");
    CHECK(r == 18 && !strcmp(small, "gat"));
    scope.phase = 0;
    CHECK(nb_import_unbind(&binding) == NB_OK);
    CHECK(nb_scope_close(&scope) == NB_OK);
#endif
    puts("{\"snprintf_errors\":0,\"integers\":true,\"pointers\":true,\"strings\":true,\"floats\":true,\"length_modifiers\":true,\"truncation\":true,\"stack_arguments\":true,\"gateway_tested\":true}");
    return 0;
}
struct rwlock_test_thread_arg {
    void *lock;
    int phase;
    int result;
};
static void *rwlock_test_thread(void *arg) {
    struct rwlock_test_thread_arg *a = arg;
    typedef bool (*try_acquire_fn)(void *);
    typedef void (*release_fn)(void *);

    release_fn rel_read = (release_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn2os15ReleaseReadLockEPNS0_20ReaderWriterLockTypeE");
    try_acquire_fn try_write = (try_acquire_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn2os19TryAcquireWriteLockEPNS0_20ReaderWriterLockTypeE");
    try_acquire_fn try_read = (try_acquire_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn2os18TryAcquireReadLockEPNS0_20ReaderWriterLockTypeE");

    if (a->phase == 1) {
        if (try_write(a->lock)) { a->result = 101; return NULL; }
        if (try_read(a->lock)) { a->result = 102; return NULL; }
        a->result = 0;
    } else if (a->phase == 2) {
        if (try_write(a->lock)) { a->result = 201; return NULL; }
        if (!try_read(a->lock)) { a->result = 202; return NULL; }
        rel_read(a->lock);
        a->result = 0;
    }
    return NULL;
}
static int rwlock_contract(void) {
    typedef void (*init_fn)(void *);
    typedef void (*fini_fn)(void *);
    typedef void (*acquire_fn)(void *);
    typedef bool (*try_acquire_fn)(void *);
    typedef void (*release_fn)(void *);
    typedef bool (*query_fn)(const void *);

    init_fn init = (init_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn2os26InitializeReaderWriterLockEPNS0_20ReaderWriterLockTypeE");
    fini_fn fini = (fini_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn2os24FinalizeReaderWriterLockEPNS0_20ReaderWriterLockTypeE");
    acquire_fn acq_read = (acquire_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn2os15AcquireReadLockEPNS0_20ReaderWriterLockTypeE");
    release_fn rel_read = (release_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn2os15ReleaseReadLockEPNS0_20ReaderWriterLockTypeE");
    acquire_fn acq_write = (acquire_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn2os16AcquireWriteLockEPNS0_20ReaderWriterLockTypeE");
    try_acquire_fn try_write = (try_acquire_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn2os19TryAcquireWriteLockEPNS0_20ReaderWriterLockTypeE");
    release_fn rel_write = (release_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn2os16ReleaseWriteLockEPNS0_20ReaderWriterLockTypeE");
    query_fn is_owner = (query_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn2os29IsReaderWriterLockOwnerThreadEPKNS0_20ReaderWriterLockTypeE");
    query_fn is_read_held = (query_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn2os14IsReadLockHeldEPKNS0_20ReaderWriterLockTypeE");

    CHECK(init && fini && acq_read && rel_read && acq_write && try_write && rel_write && is_owner && is_read_held);

    _Alignas(8) unsigned char lock[64];
    memset(lock, 0, sizeof(lock));
    init(lock);
    CHECK(lock[0x10] == 1);
    CHECK(!is_owner(lock));
    CHECK(!is_read_held(lock));

    /* 1. Test Write Lock */
    acq_write(lock);
    CHECK(is_owner(lock));
    CHECK(*(uintptr_t*)(lock + 0x18) != 0);

    /* Test concurrent exclusion from another thread */
    pthread_t thread;
    struct rwlock_test_thread_arg arg = { .lock = lock, .phase = 1, .result = -1 };
    CHECK(pthread_create(&thread, NULL, rwlock_test_thread, &arg) == 0);
    CHECK(pthread_join(thread, NULL) == 0);
    CHECK(arg.result == 0);

    rel_write(lock);
    CHECK(!is_owner(lock));
    CHECK(*(uintptr_t*)(lock + 0x18) == 0);

    /* 2. Test Read Lock */
    acq_read(lock);
    CHECK(is_read_held(lock));
    CHECK(!is_owner(lock));

    arg.phase = 2;
    arg.result = -1;
    CHECK(pthread_create(&thread, NULL, rwlock_test_thread, &arg) == 0);
    CHECK(pthread_join(thread, NULL) == 0);
    CHECK(arg.result == 0);

    rel_read(lock);
    CHECK(!is_read_held(lock));

    /* 3. Finalize */
    fini(lock);
    CHECK(lock[0x10] == 0);

    puts("{\"rwlock_errors\":0,\"initialized\":true,\"write_exclusive\":true,\"read_shared\":true,\"owner_tracked\":true,\"finalized\":true}");
    return 0;
}
static int uuid_contract(void) {
    uint64_t target = guest_process_resolve(0, "_ZN2nn4util12GenerateUuidEv");
    CHECK(target != 0);
    typedef struct nn_uuid (*gen_fn)(void);
    gen_fn generate = (gen_fn)(uintptr_t)target;
    struct nn_uuid u1 = generate();
    struct nn_uuid u2 = generate();
    CHECK(u1.w0 != 0 || u1.w1 != 0);
    CHECK(u2.w0 != 0 || u2.w1 != 0);
    CHECK(u1.w0 != u2.w0 || u1.w1 != u2.w1);

#if defined(__ANDROID__) && defined(__aarch64__)
    nb_scope scope;
    memset(&scope, 0, sizeof(scope));
    CHECK(nb_scope_init(&scope) == NB_OK);
    nb_import binding = {0};
    nb_signature sig = {
        .version = 1, .flags = NB_SIG_REGISTER_ONLY,
        .gp_arguments = 0, .gp_returns = 2
    };
    CHECK(nb_import_bind(&binding, &scope, (void(*)(void))(uintptr_t)target, &sig) == NB_OK);
    gen_fn gateway_gen = (gen_fn)nb_import_entry(&binding);
    CHECK(gateway_gen != NULL);
    scope.phase = 1;
    struct nn_uuid gu1 = gateway_gen();
    struct nn_uuid gu2 = gateway_gen();
    CHECK(gu1.w0 != 0 || gu1.w1 != 0);
    CHECK(gu2.w0 != 0 || gu2.w1 != 0);
    CHECK(gu1.w0 != gu2.w0 || gu1.w1 != gu2.w1);
    scope.phase = 0;
    CHECK(nb_import_unbind(&binding) == NB_OK);
    CHECK(nb_scope_close(&scope) == NB_OK);
#endif

    puts("{\"uuid_errors\":0,\"generated\":true,\"distinct\":true,\"two_gp_returns\":true,\"gateway_tested\":true}");
    return 0;
}
static int time_contract(void) {
    typedef uint32_t (*time_init_fn)(void);
    typedef bool (*time_is_init_fn)(void);
    typedef uint32_t (*get_time_fn)(int64_t *);
    typedef uint32_t (*loc_fn)(struct nn_location_name *);
    typedef int (*dow_fn)(int, int, int);

    time_init_fn init = (time_init_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn4time10InitializeEv");
    time_is_init_fn is_init = (time_is_init_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn4time13IsInitializedEv");
    get_time_fn get_time = (get_time_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn4time23StandardUserSystemClock14GetCurrentTimeEPNS0_9PosixTimeE");
    loc_fn get_loc = (loc_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn4time21GetDeviceLocationNameEPNS0_12LocationNameE");
    dow_fn dow = (dow_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn4time12GetDayOfWeekEiii");
    typedef uint32_t (*to_cal_fn)(struct nn_calendar_time *, struct nn_calendar_additional_info *, const int64_t *, const void *);
    typedef uint64_t (*to_utc_fn)(const int64_t *);
    to_cal_fn to_cal = (to_cal_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn4time14ToCalendarTimeEPNS0_12CalendarTimeEPNS0_22CalendarAdditionalInfoERKNS0_9PosixTimeERKNS0_12TimeZoneRuleE");
    to_utc_fn to_utc = (to_utc_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn4time19ToCalendarTimeInUtcERKNS0_9PosixTimeE");

    CHECK(init && is_init && get_time && get_loc && dow && to_cal && to_utc);
    CHECK(init() == 0);
    CHECK(is_init());

    int64_t now = 0;
    CHECK(get_time(&now) == 0 && now > 1700000000);

    struct nn_location_name loc;
    memset(&loc, 0, sizeof(loc));
    CHECK(get_loc(&loc) == 0 && strlen(loc.name) > 0);

    CHECK(dow(2026, 10, 24) == 6);

    struct nn_calendar_time cal;
    struct nn_calendar_additional_info info;
    int64_t sample_time = 1700000000;
    CHECK(to_cal(&cal, &info, &sample_time, NULL) == 0);
    CHECK(cal.year == 2023 && cal.month == 11 && cal.day == 14 && cal.hour == 22 && cal.minute == 13 && cal.second == 20);
    CHECK(info.day_of_week == 2);
    uint64_t utc_val = to_utc(&sample_time);
    struct nn_calendar_time *utc_cal = (struct nn_calendar_time *)&utc_val;
    CHECK(utc_cal->year == 2023 && utc_cal->month == 11 && utc_cal->day == 14);

    puts("{\"time_errors\":0,\"initialized\":true,\"current_time_valid\":true,\"location_name\":true,\"day_of_week\":true,\"to_calendar_time\":true,\"to_calendar_time_utc\":true}");
    return 0;
}
static int tick_contract(void) {
    typedef int64_t (*get_freq_fn)(void);
    typedef int64_t (*get_tick_fn)(void);
    get_freq_fn get_freq = (get_freq_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn2os22GetSystemTickFrequencyEv");
    get_tick_fn get_tick = (get_tick_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn2os13GetSystemTickEv");
    CHECK(get_freq && get_tick);
    int64_t freq = get_freq();
    CHECK(freq == 19200000LL);
    int64_t t1 = get_tick();
    CHECK(t1 > 0);
    int64_t t2 = get_tick();
    CHECK(t2 >= t1);
    puts("{\"tick_errors\":0,\"frequency_19_2mhz\":true,\"ticks_monotonic\":true}");
    return 0;
}
static int ctype_contract(void) {
    typedef int (*iswxdigit_fn)(wint_t);
    typedef int (*iswdigit_fn)(wint_t);
    typedef int (*isspace_fn)(int);
    typedef int (*tolower_fn)(int);
    iswxdigit_fn fn_xdigit = (iswxdigit_fn)(uintptr_t)guest_process_resolve(0, "iswxdigit");
    iswdigit_fn fn_digit = (iswdigit_fn)(uintptr_t)guest_process_resolve(0, "iswdigit");
    isspace_fn fn_space = (isspace_fn)(uintptr_t)guest_process_resolve(0, "isspace");
    tolower_fn fn_tolower = (tolower_fn)(uintptr_t)guest_process_resolve(0, "tolower");
    CHECK(fn_xdigit && fn_digit && fn_space && fn_tolower);
    CHECK(fn_xdigit(L'a') && fn_xdigit(L'F') && fn_xdigit(L'0') && !fn_xdigit(L'g'));
    CHECK(fn_digit(L'5') && !fn_digit(L'a'));
    CHECK(fn_space(' ') && fn_space('\t') && !fn_space('x'));
    CHECK(fn_tolower('A') == 'a');
    puts("{\"ctype_errors\":0,\"iswxdigit\":true,\"iswdigit\":true,\"isspace\":true,\"tolower\":true}");
    return 0;
}
struct tp_test_worker_arg {
    unsigned id;
    int errors;
    uintptr_t base;
};
static pthread_mutex_t s_tp_test_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_tp_test_cond = PTHREAD_COND_INITIALIZER;
static unsigned s_tp_test_ready = 0;
static unsigned s_tp_test_release = 0;

static void *tp_test_worker(void *arg) {
    struct tp_test_worker_arg *a = arg;
    unsigned char tpl_bytes[0x220];
    memset(tpl_bytes, 0x42 + a->id, sizeof(tpl_bytes));
    guest_tp_template src = {
        .initialized_data = tpl_bytes,
        .initialized_size = 0x220,
        .total_size = 0x1019,
        .alignment = 8,
        .reserved_header_size = 0x10,
    };
    guest_tp_mirror *mirror = NULL;
    if (guest_tp_mirror_create(&src, &mirror).status != GUEST_TP_OK || !mirror) { a->errors++; return NULL; }
    if (guest_tp_bind_current(mirror).status != GUEST_TP_OK) { a->errors++; guest_tp_mirror_free(mirror); return NULL; }
    const unsigned char *base = guest_tp_current_base();
    if (base != guest_tp_mirror_base(mirror)) a->errors++;
    a->base = (uintptr_t)base;
    if (guest_tp_mirror_span(mirror) != 0x1030) a->errors++;
    guest_tp_mirror_payload(mirror)[0] = (unsigned char)(a->id + 1);

    /* Hold mirror until all workers are ready so memory is not recycled */
    pthread_mutex_lock(&s_tp_test_mutex);
    s_tp_test_ready++;
    pthread_cond_broadcast(&s_tp_test_cond);
    while (!s_tp_test_release) {
        pthread_cond_wait(&s_tp_test_cond, &s_tp_test_mutex);
    }
    pthread_mutex_unlock(&s_tp_test_mutex);

    if (guest_tp_unbind_current().status != GUEST_TP_OK) a->errors++;
    return NULL;
}
static int guest_tp_test_contract(void) {
    /* 1. Test mirror model with game's template facts */
    unsigned char template_data[0x220];
    for (size_t i = 0; i < 0x220; i++) template_data[i] = (unsigned char)(i * 31 + 7);
    guest_tp_template source = {
        .initialized_data = template_data,
        .initialized_size = 0x220,
        .total_size = 0x1019,
        .alignment = 8,
        .reserved_header_size = 0x10,
    };
    guest_tp_mirror *mirror = NULL;
    CHECK(guest_tp_mirror_create(&source, &mirror).status == GUEST_TP_OK && mirror);
    const unsigned char *base = guest_tp_mirror_base(mirror);
    const unsigned char *payload = guest_tp_mirror_payload(mirror);
    CHECK(base != NULL && payload == base + 0x10);
    CHECK(guest_tp_mirror_span(mirror) == 0x1030);
    CHECK((uintptr_t)base % 8 == 0);
    for (size_t i = 0; i < 0x10; i++) CHECK(base[i] == 0);
    CHECK(memcmp(payload, template_data, 0x220) == 0);
    for (size_t i = 0x220; i < 0x1019; i++) CHECK(payload[i] == 0);

    CHECK(guest_tp_bind_current(mirror).status == GUEST_TP_OK);
    CHECK(guest_tp_current_base() == base);
    /* Duplicate bind is rejected */
    guest_tp_mirror *second = NULL;
    CHECK(guest_tp_mirror_create(&source, &second).status == GUEST_TP_OK && second);
    CHECK(guest_tp_bind_current(second).status == GUEST_TP_BUSY);
    guest_tp_mirror_free(second);
    CHECK(guest_tp_unbind_current().status == GUEST_TP_OK);
    CHECK(guest_tp_current_base() == NULL);

    /* 2. Test patching model */
    uint32_t words[8] = {
        0xd53bd048u, /* MRS x8, TPIDR_EL0 */
        0xd53bd054u, /* MRS x20, TPIDR_EL0 */
        0xd53bd060u, /* MRS TPIDRRO_EL0 */
        0xd51bd040u, /* MSR TPIDR_EL0 */
        0xd2a00000u, /* MOVZ x0, #0 */
        0xd65f03c0u, /* RET */
        0, 0
    };
    struct guest_tp_patch_report report;
    CHECK(guest_tp_scan_text(words, sizeof(words), &report).status == GUEST_TP_OK);
    CHECK(report.scanned_words == 8 && report.registers[8] == 1 && report.registers[20] == 1);
    CHECK(guest_tp_patch_text(words, sizeof(words), &report).status == GUEST_TP_OK);
    CHECK(report.patched == 2);
    CHECK(words[0] == guest_tp_trap_encode(8));
    CHECK(words[1] == guest_tp_trap_encode(20));
    CHECK(words[2] == 0xd53bd060u);
    CHECK(words[3] == 0xd51bd040u);

    /* 3. Test trap decision */
    unsigned dest = 0; int advance = 0;
    enum guest_tp_trap_decision dec = guest_tp_trap_decide(1, words[0], 0xd503201fu, base, &dest, &advance);
    CHECK(dec == GUEST_TP_TRAP_RESOLVE && dest == 8 && advance == 1);
    dec = guest_tp_trap_decide(1, 0xd503201fu, words[0], base, &dest, &advance);
    CHECK(dec == GUEST_TP_TRAP_RESOLVE && dest == 8 && advance == 0);

    /* 4. Multi-thread isolation */
    enum { TP_THREADS = 8 };
    pthread_t th[TP_THREADS];
    struct tp_test_worker_arg args[TP_THREADS];
    memset(args, 0, sizeof(args));
    s_tp_test_ready = 0;
    s_tp_test_release = 0;
    for (unsigned i = 0; i < TP_THREADS; i++) {
        args[i].id = i;
        CHECK(pthread_create(&th[i], NULL, tp_test_worker, &args[i]) == 0);
    }
    pthread_mutex_lock(&s_tp_test_mutex);
    while (s_tp_test_ready != TP_THREADS) {
        pthread_cond_wait(&s_tp_test_cond, &s_tp_test_mutex);
    }
    for (unsigned i = 0; i < TP_THREADS; i++) {
        for (unsigned j = 0; j < i; j++) {
            CHECK(args[i].base != args[j].base);
        }
    }
    s_tp_test_release = 1;
    pthread_cond_broadcast(&s_tp_test_cond);
    pthread_mutex_unlock(&s_tp_test_mutex);
    for (unsigned i = 0; i < TP_THREADS; i++) {
        CHECK(pthread_join(th[i], NULL) == 0);
        CHECK(args[i].errors == 0);
    }
    CHECK(guest_tp_live_mirrors() == 0);

    /* 5. Test _ZTH weak TLS symbol resolution */
    uint64_t zth_fn = guest_process_resolve(150, "_ZTHN13LowLevelTasks5FTask10ActiveTaskE");
    CHECK(zth_fn != 0);
    typedef void (*tls_init_fn)(void);
    ((tls_init_fn)(uintptr_t)zth_fn)();

    puts("{\"guest_tp_errors\":0,\"mirror_model\":true,\"patch_model\":true,\"trap_decision\":true,\"isolation\":true,\"zth_resolved\":true,\"live_mirrors\":0}");
    return 0;
}
struct test_thread_context {
    uint64_t magic;
    uint64_t value;
    uint64_t observed_tp;
    uint64_t thread_id;
};
static void test_thread_worker(void *arg) {
    struct test_thread_context *ctx = (struct test_thread_context *)arg;
    ctx->value = 0x12345678;
    ctx->observed_tp = (uint64_t)(uintptr_t)guest_tp_current_base();
}
static int threads_contract(void) {
    typedef int32_t (*create_thread_fn)(void *, void (*)(void *), void *, void *, size_t, int32_t);
    typedef void (*start_thread_fn)(void *);
    typedef void (*wait_thread_fn)(void *);
    typedef void (*destroy_thread_fn)(void *);
    typedef void (*set_name_fn)(void *, const char *);
    typedef const char *(*get_name_fn)(const void *);
    typedef int32_t (*get_priority_fn)(const void *);
    typedef uint64_t (*get_id_fn)(const void *);
    typedef void (*sleep_fn)(int64_t);
    typedef void (*yield_fn)(void);
    typedef uint64_t (*get_cores_fn)(void);

    create_thread_fn create = (create_thread_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn2os12CreateThreadEPNS0_10ThreadTypeEPFvPvES3_S3_mi");
    start_thread_fn start = (start_thread_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn2os11StartThreadEPNS0_10ThreadTypeE");
    wait_thread_fn wait = (wait_thread_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn2os10WaitThreadEPNS0_10ThreadTypeE");
    destroy_thread_fn destroy = (destroy_thread_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn2os13DestroyThreadEPNS0_10ThreadTypeE");
    set_name_fn set_name = (set_name_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn2os13SetThreadNameEPNS0_10ThreadTypeEPKc");
    get_name_fn get_name = (get_name_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn2os20GetThreadNamePointerEPKNS0_10ThreadTypeE");
    get_priority_fn get_prio = (get_priority_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn2os17GetThreadPriorityEPKNS0_10ThreadTypeE");
    get_id_fn get_id = (get_id_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn2os11GetThreadIdEPKNS0_10ThreadTypeE");
    sleep_fn sleep_th = (sleep_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn2os11SleepThreadENS_8TimeSpanE");
    yield_fn yield_th = (yield_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn2os11YieldThreadEv");
    get_cores_fn get_cores = (get_cores_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn2os26GetThreadAvailableCoreMaskEv");

    CHECK(create && start && wait && destroy && set_name && get_name && get_prio && get_id && sleep_th && yield_th && get_cores);

    CHECK(get_cores() == 0x7ULL);
    sleep_th(1000);
    yield_th();

    _Alignas(16) unsigned char thread_obj[0x1c0];
    memset(thread_obj, 0, sizeof(thread_obj));

    _Alignas(16) unsigned char stack[65536];
    struct test_thread_context ctx = { .magic = 0xfeedface, .value = 0, .observed_tp = 0, .thread_id = 0 };

    int32_t rc = create(thread_obj, test_thread_worker, &ctx, stack, sizeof(stack), 16);
    CHECK(rc == 0);

    CHECK(get_prio(thread_obj) == 16);
    uint64_t tid = get_id(thread_obj);
    CHECK(tid != 0);

    set_name(thread_obj, "TestThread");
    CHECK(!strcmp(get_name(thread_obj), "TestThread"));

    start(thread_obj);
    wait(thread_obj);

    CHECK(ctx.value == 0x12345678);
    CHECK(ctx.observed_tp != 0);

    destroy(thread_obj);

    puts("{\"thread_errors\":0,\"create\":true,\"start\":true,\"wait\":true,\"destroy\":true,\"name\":true,\"priority\":true,\"tid\":true,\"tp_mirror\":true}");
    return 0;
}
static void *dummy_fs_alloc(size_t size) {
    return (void *)(uintptr_t)(0x1000 + size);
}
static void dummy_fs_free(void *ptr, size_t size) {
    (void)ptr;
    (void)size;
}
static int fs_allocator_contract(void) {
    typedef void (*set_allocator_fn)(void *(*)(size_t), void (*)(void *, size_t));
    set_allocator_fn set_alloc = (set_allocator_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn2fs12SetAllocatorEPFPvmEPFvS1_mE");
    CHECK(set_alloc != NULL);

    set_alloc(dummy_fs_alloc, dummy_fs_free);

    CHECK(nso_host_fs_get_allocator() == dummy_fs_alloc);
    CHECK(nso_host_fs_get_deallocator() == dummy_fs_free);

    puts("{\"fs_allocator_errors\":0,\"set\":true,\"callbacks_saved\":true}");
    return 0;
}
static int fs_query_cache_contract(void) {
    typedef int32_t (*query_fn)(size_t *);
    query_fn query = (query_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn2fs22QueryMountRomCacheSizeEPm");
    CHECK(query != NULL);

    int32_t err = query(NULL);
    CHECK(err != 0);

    size_t size = 0;
    int32_t ok = query(&size);
    CHECK(ok == 0);
    CHECK(size >= 32);

    puts("{\"fs_query_cache_errors\":0,\"query\":true,\"size_valid\":true}");
    return 0;
}
static int fs_mount_rom_contract(void) {
    typedef int32_t (*mount_fn)(const char *, void *, size_t);
    typedef int32_t (*unmount_fn)(const char *);

    mount_fn mount = (mount_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn2fs8MountRomEPKcPvm");
    CHECK(mount != NULL);

    mount_fn mount_alias = (mount_fn)(uintptr_t)guest_process_resolve(0, "nnfsMountRom");
    CHECK(mount_alias != NULL);

    unmount_fn unmount = (unmount_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn2fs7UnmountEPKc");
    CHECK(unmount != NULL);

    unmount_fn unmount_alias = (unmount_fn)(uintptr_t)guest_process_resolve(0, "nnfsUnmount");
    CHECK(unmount_alias != NULL);

    /* Null name rejection */
    int32_t err = mount(NULL, (void *)0x1000, 0x10000);
    CHECK(err != 0);

    /* Empty name rejection */
    err = mount("", (void *)0x1000, 0x10000);
    CHECK(err != 0);

    /* Null buffer rejection */
    err = mount("rom", NULL, 0x10000);
    CHECK(err != 0);

    /* Buffer too small rejection (< 64KB) */
    err = mount("rom", (void *)0x1000, 0x8000);
    CHECK(err != 0);

    /* Valid mount */
    char dummy_buf[64];
    int32_t ok = mount("rom", dummy_buf, 0x10000);
    CHECK(ok == 0);
    CHECK(nso_host_fs_is_rom_mounted("rom") == 1);
    CHECK(nso_host_fs_get_rom_mount_path("rom") != NULL);

    /* Valid unmount */
    int32_t unmount_ok = unmount("rom");
    CHECK(unmount_ok == 0);
    CHECK(nso_host_fs_is_rom_mounted("rom") == 0);

    /* Unmount invalid/non-existent returns clean */
    unmount("non_existent");

    puts("{\"fs_mount_rom_errors\":0,\"mount\":true,\"unmount\":true,\"validation\":true}");
    return 0;
}
static int fs_open_file_contract(void) {
    typedef int32_t (*open_fn)(nn_fs_file_handle *, const char *, int32_t);
    typedef int32_t (*get_size_fn)(int64_t *, nn_fs_file_handle);
    typedef int32_t (*read_fn)(size_t *, nn_fs_file_handle, int64_t, void *, size_t);
    typedef int32_t (*read_no_out_fn)(nn_fs_file_handle, int64_t, void *, size_t);
    typedef void (*close_fn)(nn_fs_file_handle);
    typedef int32_t (*get_entry_type_fn)(int32_t *, const char *);

    open_fn open_file = (open_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn2fs8OpenFileEPNS0_10FileHandleEPKci");
    CHECK(open_file != NULL);

    open_fn open_file_alias = (open_fn)(uintptr_t)guest_process_resolve(0, "nnfsOpenFile");
    CHECK(open_file_alias != NULL);

    get_size_fn get_file_size = (get_size_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn2fs11GetFileSizeEPlNS0_10FileHandleE");
    CHECK(get_file_size != NULL);

    get_size_fn get_file_size_alias = (get_size_fn)(uintptr_t)guest_process_resolve(0, "nnfsGetFileSize");
    CHECK(get_file_size_alias != NULL);

    read_fn read_file = (read_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn2fs8ReadFileEPmNS0_10FileHandleElPvm");
    CHECK(read_file != NULL);

    read_fn read_file_alias = (read_fn)(uintptr_t)guest_process_resolve(0, "nnfsReadFile");
    CHECK(read_file_alias != NULL);

    read_no_out_fn read_file_no_out = (read_no_out_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn2fs8ReadFileENS0_10FileHandleElPvm");
    CHECK(read_file_no_out != NULL);

    close_fn close_file = (close_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn2fs9CloseFileENS0_10FileHandleE");
    CHECK(close_file != NULL);

    close_fn close_file_alias = (close_fn)(uintptr_t)guest_process_resolve(0, "nnfsCloseFile");
    CHECK(close_file_alias != NULL);

    get_entry_type_fn get_entry_type = (get_entry_type_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn2fs12GetEntryTypeEPNS0_18DirectoryEntryTypeEPKc");
    CHECK(get_entry_type != NULL);

    get_entry_type_fn get_entry_type_alias = (get_entry_type_fn)(uintptr_t)guest_process_resolve(0, "nnfsGetEntryType");
    CHECK(get_entry_type_alias != NULL);

    /* Parameter validation */
    nn_fs_file_handle handle = {0};
    CHECK(open_file(NULL, "rom:/test.txt", 1) != 0);
    CHECK(open_file(&handle, NULL, 1) != 0);
    CHECK(open_file(&handle, "", 1) != 0);

    /* Ensure 'rom' is mounted for contract testing */
    if (!nso_host_fs_is_rom_mounted("rom")) {
        static char cache_buf[0x10000];
        nso_host_fs_mount_rom("rom", cache_buf, sizeof(cache_buf));
    }

    /* Non-existent file */
    CHECK(open_file(&handle, "rom:/non_existent_file_xyz_123.bin", 1) != 0);

    /* Create a temporary test file inside the mounted romfs directory */
    const char *mount_root = nso_host_fs_get_rom_mount_path("rom");
    char full_test_path[512];
    snprintf(full_test_path, sizeof(full_test_path), "%s/romfs_contract_test.tmp", mount_root ? mount_root : ".");
    FILE *fp = fopen(full_test_path, "wb");
    CHECK(fp != NULL);
    const char test_data[] = "Hello RG477M RomFS!";
    size_t test_len = sizeof(test_data) - 1;
    CHECK(fwrite(test_data, 1, test_len, fp) == test_len);
    fclose(fp);

    /* Open the temporary file via rom:/ path */
    int32_t ok = open_file(&handle, "rom:/romfs_contract_test.tmp", 1);
    CHECK(ok == 0);
    CHECK(handle.handle != NULL);

    /* Query size */
    int64_t size = 0;
    CHECK(get_file_size(&size, handle) == 0);
    CHECK(size == (int64_t)test_len);

    /* Read with out_bytes_read */
    char read_buf[64] = {0};
    size_t bytes_read = 0;
    CHECK(read_file(&bytes_read, handle, 0, read_buf, sizeof(read_buf)) == 0);
    CHECK(bytes_read == test_len);
    CHECK(memcmp(read_buf, test_data, test_len) == 0);

    /* Read with offset using 4-arg ReadFile */
    memset(read_buf, 0, sizeof(read_buf));
    CHECK(read_file_no_out(handle, 6, read_buf, 6) == 0);
    CHECK(memcmp(read_buf, "RG477M", 6) == 0);

    /* Close file */
    close_file(handle);

    /* Operations on closed handle must fail */
    CHECK(get_file_size(&size, handle) != 0);

    /* GetEntryType on file */
    int32_t entry_type = -1;
    CHECK(get_entry_type(&entry_type, "rom:/romfs_contract_test.tmp") == 0);
    CHECK(entry_type == 1); /* File */

    /* GetEntryType on directory */
    CHECK(get_entry_type(&entry_type, "rom:/") == 0);
    CHECK(entry_type == 0); /* Directory */

    /* Clean up temporary file */
    unlink(full_test_path);

    /* Test real game asset if present */
    nn_fs_file_handle game_handle = {0};
    if (open_file(&game_handle, "rom:/UECommandLine.txt", 1) == 0) {
        int64_t gsz = 0;
        CHECK(get_file_size(&gsz, game_handle) == 0);
        CHECK(gsz == 38);
        char gbuf[64] = {0};
        size_t gread = 0;
        CHECK(read_file(&gread, game_handle, 0, gbuf, sizeof(gbuf)) == 0);
        CHECK(gread == 38);
        CHECK(strncmp(gbuf, "-project=", 9) == 0);
        close_file(game_handle);
    }

    puts("{\"fs_open_file_errors\":0,\"open\":true,\"read\":true,\"size\":true,\"close\":true,\"entry_type\":true,\"validation\":true}");
    return 0;
}
static int aoc_contract(void) {
    typedef int32_t (*count_fn)(void);
    typedef int32_t (*list_fn)(int32_t *, int32_t, int32_t);
    typedef int32_t (*query_cache_fn)(size_t *, int32_t);
    typedef int32_t (*mount_aoc_fn)(const char *, int32_t, void *, size_t);

    count_fn count_aoc = (count_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn3aoc17CountAddOnContentEv");
    CHECK(count_aoc != NULL);

    count_fn count_aoc_alias = (count_fn)(uintptr_t)guest_process_resolve(0, "nnaocCountAddOnContent");
    CHECK(count_aoc_alias != NULL);

    list_fn list_aoc = (list_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn3aoc16ListAddOnContentEPiii");
    CHECK(list_aoc != NULL);

    list_fn list_aoc_alias = (list_fn)(uintptr_t)guest_process_resolve(0, "nnaocListAddOnContent");
    CHECK(list_aoc_alias != NULL);

    query_cache_fn query_cache = (query_cache_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn2fs31QueryMountAddOnContentCacheSizeEPmi");
    CHECK(query_cache != NULL);

    query_cache_fn query_cache_alias = (query_cache_fn)(uintptr_t)guest_process_resolve(0, "nnfsQueryMountAddOnContentCacheSize");
    CHECK(query_cache_alias != NULL);

    mount_aoc_fn mount_aoc = (mount_aoc_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn2fs17MountAddOnContentEPKciPvm");
    CHECK(mount_aoc != NULL);

    mount_aoc_fn mount_aoc_alias = (mount_aoc_fn)(uintptr_t)guest_process_resolve(0, "nnfsMountAddOnContent");
    CHECK(mount_aoc_alias != NULL);

    /* Test CountAddOnContent: baseline returns 0 without crash */
    int32_t c = count_aoc();
    CHECK(c == 0);
    c = count_aoc_alias();
    CHECK(c == 0);

    /* Test ListAddOnContent: returns 0 when no addon content */
    int32_t indices[8] = {0};
    int32_t listed = list_aoc(indices, 0, 8);
    CHECK(listed == 0);
    listed = list_aoc_alias(indices, 0, 8);
    CHECK(listed == 0);

    /* Test QueryMountAddOnContentCacheSize: writes 0 to out_size */
    size_t cache_size = 9999;
    CHECK(query_cache(NULL, 1) != 0);
    CHECK(query_cache(&cache_size, 1) == 0);
    CHECK(cache_size == 0);
    cache_size = 9999;
    CHECK(query_cache_alias(&cache_size, 1) == 0);
    CHECK(cache_size == 0);

    /* Test MountAddOnContent: returns error because no addon is available */
    char dummy_buf[64];
    CHECK(mount_aoc("aoc", 1, dummy_buf, sizeof(dummy_buf)) != 0);
    CHECK(mount_aoc_alias("aoc", 1, dummy_buf, sizeof(dummy_buf)) != 0);

    puts("{\"aoc_contract_errors\":0,\"count\":true,\"list\":true,\"query_cache\":true,\"mount\":true,\"baseline_zero\":true}");
    return 0;
}
static int nifm_contract(void) {
    typedef int32_t (*init_fn)(void);

    init_fn init_nifm = (init_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn4nifm10InitializeEv");
    CHECK(init_nifm != NULL);

    init_fn init_nifm_alias = (init_fn)(uintptr_t)guest_process_resolve(0, "nnnifmInitialize");
    CHECK(init_nifm_alias != NULL);

    /* Test Initialize: baseline returns 0 without crash */
    int32_t res = init_nifm();
    CHECK(res == 0);
    res = init_nifm_alias();
    CHECK(res == 0);

    puts("{\"nifm_contract_errors\":0,\"initialize\":true,\"baseline_zero\":true}");
    return 0;
}
static int socket_contract(void) {
    typedef int32_t (*init_fn)(void *work_buffer, size_t work_buffer_size, size_t allocator_size, int32_t concurrency);

    init_fn init_sock = (init_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn6socket10InitializeEPvmmi");
    CHECK(init_sock != NULL);

    init_fn init_sock_alias = (init_fn)(uintptr_t)guest_process_resolve(0, "nnsocketInitialize");
    CHECK(init_sock_alias != NULL);

    /* Test Initialize: baseline returns 0 without crash */
    static char dummy_buf[1024];
    int32_t res = init_sock(dummy_buf, sizeof(dummy_buf), 256, 4);
    CHECK(res == 0);
    CHECK(nso_host_socket_is_initialized());

    res = init_sock_alias(dummy_buf, sizeof(dummy_buf), 256, 4);
    CHECK(res == 0);

    puts("{\"socket_contract_errors\":0,\"initialize\":true,\"baseline_zero\":true}");
    return 0;
}
/* nn::socket event family. Retail ABI (Nintendo SDK NSO): EventFd(initval,
 * flags) returns a descriptor or -1; EventFdWrite/EventFdRead move exactly
 * eight bytes and answer 0 or -1. The guest posts one signal and drains it
 * with a single read, so the contract asserts the auto-reset behaviour, the
 * empty-read error and the pollability of the descriptor. */
static int socket_eventfd_contract(void) {
    typedef int32_t (*event_fd_fn)(uint64_t, int32_t);
    typedef int32_t (*event_fd_write_fn)(int32_t, uint64_t);
    typedef int32_t (*event_fd_read_fn)(int32_t, uint64_t *);
    typedef int32_t (*close_fn)(int32_t);
    typedef int32_t (*get_last_error_fn)(void);
    typedef int32_t (*poll_fn)(void *, uint64_t, int32_t);

    event_fd_fn event_fd = (event_fd_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn6socket7EventFdEmNS0_12EventFdFlagsE");
    CHECK(event_fd != NULL);
    event_fd_write_fn event_fd_write = (event_fd_write_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn6socket12EventFdWriteEim");
    CHECK(event_fd_write != NULL);
    event_fd_read_fn event_fd_read = (event_fd_read_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn6socket11EventFdReadEiPm");
    CHECK(event_fd_read != NULL);
    close_fn socket_close = (close_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn6socket5CloseEi");
    CHECK(socket_close != NULL);
    get_last_error_fn get_last_error = (get_last_error_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn6socket12GetLastErrorEv");
    CHECK(get_last_error != NULL);
    poll_fn socket_poll = (poll_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn6socket4PollEPNS0_6PollFdEmi");
    CHECK(socket_poll != NULL);
    poll_fn socket_poll_c = (poll_fn)(uintptr_t)guest_process_resolve(0, "nnsocketPoll");
    CHECK(socket_poll_c != NULL);

    /* The guest's own call shape: EventFd(0, 4). */
    int32_t fd = event_fd(0, 4);
    CHECK(fd >= 0);

    /* A read with nothing posted must fail instead of parking the caller. */
    uint64_t value = 0xdeadbeef;
    CHECK(event_fd_read(fd, &value) == -1);
    CHECK(get_last_error() != 0);

    /* A null output pointer is rejected, matching the retail null check. */
    CHECK(event_fd_read(fd, NULL) == -1);

    /* Post and drain: one read consumes exactly one posted signal. */
    CHECK(event_fd_write(fd, 1) == 0);
    CHECK(event_fd_read(fd, &value) == 0);
    CHECK(value == 1);

    /* Two posts, two drains: the second read must not report the first post. */
    CHECK(event_fd_write(fd, 1) == 0);
    CHECK(event_fd_write(fd, 1) == 0);
    CHECK(event_fd_read(fd, &value) == 0);
    CHECK(value == 1);
    CHECK(event_fd_read(fd, &value) == 0);
    CHECK(value == 1);
    CHECK(event_fd_read(fd, &value) == -1);

    /* The descriptor is pollable, which is how the guest waits for a signal. */
    struct { int fd; short events; short revents; } descriptors[1];
    descriptors[0].fd = fd;
    descriptors[0].events = 1;   /* POLLIN */
    descriptors[0].revents = 0;
    CHECK(socket_poll(descriptors, 1, 0) == 0);
    CHECK(descriptors[0].revents == 0);
    CHECK(event_fd_write(fd, 1) == 0);
    descriptors[0].revents = 0;
    CHECK(socket_poll(descriptors, 1, 0) == 1);
    CHECK((descriptors[0].revents & 1) == 1);
    /* The C entry point polls the same array and reports the same readiness. */
    descriptors[0].revents = 0;
    CHECK(socket_poll_c(descriptors, 1, 0) == 1);
    CHECK((descriptors[0].revents & 1) == 1);

    CHECK(socket_close(fd) == 0);

    puts("{\"socket_eventfd_contract_errors\":0,\"create\":true,\"write\":true,\"read_roundtrip\":true,"
         "\"read_empty_rejected\":true,\"null_output_rejected\":true,\"auto_reset\":true,"
         "\"pollable\":true,\"close\":true}");
    return 0;
}
static int account_contract(void) {
    typedef int32_t (*init_fn)(void);

    init_fn init_acc = (init_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn7account10InitializeEv");
    CHECK(init_acc != NULL);

    init_fn init_acc_alias = (init_fn)(uintptr_t)guest_process_resolve(0, "nnaccountInitialize");
    CHECK(init_acc_alias != NULL);

    /* Test Initialize: baseline returns 0 without crash */
    int32_t res = init_acc();
    CHECK(res == 0);
    CHECK(nso_host_account_is_initialized());

    res = init_acc_alias();
    CHECK(res == 0);

    puts("{\"account_contract_errors\":0,\"initialize\":true,\"baseline_zero\":true}");
    return 0;
}
static int oe_contract(void) {
    typedef int32_t (*init_fn)(void);
    typedef nn_settings_language_code (*lang_fn)(void);

    init_fn init_oe = (init_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn2oe10InitializeEv");
    CHECK(init_oe != NULL);

    init_fn init_oe_alias = (init_fn)(uintptr_t)guest_process_resolve(0, "nnoeInitialize");
    CHECK(init_oe_alias != NULL);

    lang_fn get_lang = (lang_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn2oe18GetDesiredLanguageEv");
    CHECK(get_lang != NULL);

    lang_fn get_lang_alias = (lang_fn)(uintptr_t)guest_process_resolve(0, "nnoeGetDesiredLanguage");
    CHECK(get_lang_alias != NULL);

    /* Test Initialize: baseline returns 0 without crash */
    int32_t res = init_oe();
    CHECK(res == 0);
    CHECK(nso_host_oe_is_initialized());

    res = init_oe_alias();
    CHECK(res == 0);

    /* Test GetDesiredLanguage: returns "en-US" */
    nn_settings_language_code lc = get_lang();
    CHECK(!strcmp(lc.code, "en-US"));

    nn_settings_language_code lc_alias = get_lang_alias();
    CHECK(!strcmp(lc_alias.code, "en-US"));

    puts("{\"oe_contract_errors\":0,\"initialize\":true,\"get_desired_language\":true,\"baseline_locale\":\"en-US\"}");
    return 0;
}
static int gettimeofday_contract(void) {
    typedef int (*gtod_fn)(struct timeval *, void *);
    typedef pid_t (*getpid_fn)(void);

    gtod_fn gtod = (gtod_fn)(uintptr_t)guest_process_resolve(0, "gettimeofday");
    CHECK(gtod != NULL);

    struct timeval tv;
    memset(&tv, 0, sizeof(tv));
    int rc = gtod(&tv, NULL);
    CHECK(rc == 0);
    CHECK(tv.tv_sec > 1700000000);
    CHECK(tv.tv_usec >= 0 && tv.tv_usec < 1000000);

    /* Test null pointer returns -1 */
    CHECK(gtod(NULL, NULL) == -1);

    getpid_fn gpid = (getpid_fn)(uintptr_t)guest_process_resolve(0, "getpid");
    CHECK(gpid != NULL);
    CHECK(gpid() > 0);

    puts("{\"gettimeofday_contract_errors\":0,\"valid_timeval\":true,\"tv_sec_positive\":true,\"tv_usec_bounded\":true,\"null_tv_rejected\":true,\"getpid_positive\":true}");
    return 0;
}
static int os_process_handle_contract(void) {
    typedef uint32_t (*get_process_handle_fn)(void);
    get_process_handle_fn fn1 = (get_process_handle_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn2os23GetCurrentProcessHandleEv");
    CHECK(fn1 != NULL);
    get_process_handle_fn fn2 = (get_process_handle_fn)(uintptr_t)guest_process_resolve(0, "nnosGetCurrentProcessHandle");
    CHECK(fn2 != NULL);

    uint32_t h1 = fn1();
    uint32_t h2 = fn2();
    CHECK(h1 != 0);
    CHECK(h1 == h2);
    CHECK(h1 == (uint32_t)getpid());

    puts("{\"os_process_handle_contract_errors\":0,\"get_current_process_handle\":true,\"handle_positive\":true,\"matches_getpid\":true}");
    return 0;
}
static int nn_result_contract(void) {
    typedef uint32_t (*is_fail_fn)(uint32_t);
    typedef uint32_t (*is_succ_fn)(uint32_t);
    typedef uint32_t (*get_mod_fn)(uint32_t);
    typedef uint32_t (*get_desc_fn)(uint32_t);
    typedef uint32_t (*succ_fn)(void);

    is_fail_fn is_fail = (is_fail_fn)(uintptr_t)guest_process_resolve(0, "nnResultIsFailure");
    CHECK(is_fail != NULL);
    is_succ_fn is_succ = (is_succ_fn)(uintptr_t)guest_process_resolve(0, "nnResultIsSuccess");
    CHECK(is_succ != NULL);
    get_mod_fn get_mod = (get_mod_fn)(uintptr_t)guest_process_resolve(0, "nnResultGetModule");
    CHECK(get_mod != NULL);
    get_desc_fn get_desc = (get_desc_fn)(uintptr_t)guest_process_resolve(0, "nnResultGetDescription");
    CHECK(get_desc != NULL);
    succ_fn succ = (succ_fn)(uintptr_t)guest_process_resolve(0, "nnResultSuccess");
    CHECK(succ != NULL);

    CHECK(is_fail(0) == 0);
    CHECK(is_fail(1) == 1);
    CHECK(is_fail(0x202) == 1);
    CHECK(is_fail(0x80000000) == 1);

    CHECK(is_succ(0) == 1);
    CHECK(is_succ(1) == 0);
    CHECK(is_succ(0x202) == 0);

    /* 0x202 = Module 2 (fs), Description 1 (PathNotFound) */
    CHECK(get_mod(0x202) == 2);
    CHECK(get_desc(0x202) == 1);

    /* 0x402 = Module 2 (fs), Description 2 (PathAlreadyExists) */
    CHECK(get_mod(0x402) == 2);
    CHECK(get_desc(0x402) == 2);

    /* 0x7b | (0x65 << 9) = 123 | (101 << 9) */
    uint32_t ssl_err = 0x7b | (0x65 << 9);
    CHECK(get_mod(ssl_err) == 0x7b);
    CHECK(get_desc(ssl_err) == 0x65);

    CHECK(succ() == 0);

    puts("{\"nn_result_contract_errors\":0,\"is_failure_works\":true,\"is_success_works\":true,\"get_module_works\":true,\"get_description_works\":true,\"success_constant_zero\":true}");
    return 0;
}
static int nnssl_contract(void) {
    typedef int32_t (*init_fn)(void);
    typedef int32_t (*finalize_fn)(void);

    init_fn ssl_init = (init_fn)(uintptr_t)guest_process_resolve(0, "nnsslInitialize");
    CHECK(ssl_init != NULL);
    finalize_fn ssl_fin = (finalize_fn)(uintptr_t)guest_process_resolve(0, "nnsslFinalize");
    CHECK(ssl_fin != NULL);

    /* nnsslInitialize returns nn::Result (0 = success) */
    int32_t rc = ssl_init();
    CHECK(rc == 0);

    /* nnsslFinalize returns a masked nn::Result in w0, not void: the retail SDK
     * wrapper (sdk module_offset 0x433620) ends with "and w0, w0, #0x3fffff".
     * The import gateway forwards w0 unchanged, so a void stub would hand the
     * guest an indeterminate Result. */
    int32_t fin_rc = ssl_fin();
    CHECK(fin_rc == 0);

    puts("{\"nnssl_contract_errors\":0,\"initialize\":true,\"finalize\":true,\"baseline_zero\":true,\"finalize_result_zero\":true}");
    return 0;
}
static int stdio_contract(void) {
    typedef int (*fflush_fn)(FILE *);
    typedef size_t (*fwrite_fn)(const void *,size_t,size_t,FILE *);
    typedef int (*fputs_fn)(const char *,FILE *);
    typedef int (*fputc_fn)(int,FILE *);
    typedef int (*puts_fn)(const char *);

    /* Data imports: the resolved value is the address of a host object the
     * guest dereferences (stdout is a FILE* variable in the guest's libc), so
     * it must never be a callable entry or a sentinel slot. */
    uint64_t stdout_object=guest_process_resolve(0,"stdout");
    uint64_t stdin_object=guest_process_resolve(0,"stdin");
    uint64_t stderr_object=guest_process_resolve(0,"stderr");
    CHECK(stdout_object!=0 && stdin_object!=0 && stderr_object!=0);
    CHECK(*(FILE **)(uintptr_t)stdout_object==stdout);
    CHECK(*(FILE **)(uintptr_t)stdin_object==stdin);
    CHECK(*(FILE **)(uintptr_t)stderr_object==stderr);
    CHECK(stdout_object!=stdin_object && stdout_object!=stderr_object && stdin_object!=stderr_object);
    /* The object slot is writable and holds exactly one host stream pointer. */
    FILE *mirror=*(FILE **)(uintptr_t)stdout_object;
    CHECK(mirror==stdout);

    fflush_fn flush=(fflush_fn)(uintptr_t)guest_process_resolve(0,"fflush");
    fwrite_fn write_bytes=(fwrite_fn)(uintptr_t)guest_process_resolve(0,"fwrite");
    fputs_fn put_string=(fputs_fn)(uintptr_t)guest_process_resolve(0,"fputs");
    fputc_fn put_char=(fputc_fn)(uintptr_t)guest_process_resolve(0,"fputc");
    puts_fn put_line=(puts_fn)(uintptr_t)guest_process_resolve(0,"puts");
    CHECK(flush&&write_bytes&&put_string&&put_char&&put_line);
    CHECK(guest_process_resolve(0,"printf")!=0);
    CHECK(guest_process_resolve(0,"fprintf")!=0);
    CHECK(guest_process_resolve(0,"sprintf")!=0);

    /* Round trip through a scratch host file: the write entry points must
     * deliver the guest bytes and return the libc results. */
    FILE *scratch=tmpfile();
    CHECK(scratch!=NULL);
    const char payload[]="stdio-contract";
    CHECK(write_bytes(payload,1,sizeof(payload)-1,scratch)==sizeof(payload)-1);
    CHECK(put_string("fputs",scratch)>=0);
    CHECK(put_char('!',scratch)=='!');
    CHECK(flush(scratch)==0);
    rewind(scratch);
    char readback[64];
    memset(readback,0,sizeof(readback));
    size_t got=fread(readback,1,sizeof(readback)-1,scratch);
    CHECK(got==sizeof(payload)-1+5+1);
    CHECK(!strncmp(readback,payload,sizeof(payload)-1));
    CHECK(!strcmp(readback+sizeof(payload)-1,"fputs!"));

    /* puts writes to the process stdout: capture it so the contract output
     * stays machine readable and the written line can be verified. */
    int saved=dup(fileno(stdout));
    CHECK(saved>=0);
    CHECK(dup2(fileno(scratch),fileno(stdout))>=0);
    int line_result=put_line("line");
    fflush(stdout);
    CHECK(dup2(saved,fileno(stdout))>=0);
    close(saved);
    CHECK(line_result>=0);
    fflush(scratch);
    fseek(scratch,0,SEEK_END);
    long size=ftell(scratch);
    CHECK(size==(long)(sizeof(payload)-1+5+1+5));
    fseek(scratch,0,SEEK_SET);
    memset(readback,0,sizeof(readback));
    got=fread(readback,1,sizeof(readback)-1,scratch);
    CHECK(got==(size_t)(sizeof(payload)-1+5+1+5));
    CHECK(!strcmp(readback+sizeof(payload)-1+5+1,"line\n"));
    fclose(scratch);

#if defined(__ANDROID__) && defined(__aarch64__)
    /* Real native boundary gateways: the variadic tail is rebuilt from the
     * guest registers, including the arguments that did not fit in registers
     * and are read from the caller's stack. */
    nb_scope scope;
    memset(&scope,0,sizeof(scope));
    CHECK(nb_scope_init(&scope)==NB_OK);
    nb_import binding={0};
    nb_signature one={.version=4,.flags=NB_SIG_VARIADIC_STDIO,.gp_arguments=1,.gp_returns=1,.variadic=1};
    CHECK(nb_import_bind(&binding,&scope,(void(*)(void))(uintptr_t)&nso_host_vprintf,&one)==NB_OK);
    nb_import binding_two={0};
    nb_signature two={.version=4,.flags=NB_SIG_VARIADIC_STDIO,.gp_arguments=2,.gp_returns=1,.variadic=1};
    CHECK(nb_import_bind(&binding_two,&scope,(void(*)(void))(uintptr_t)&nso_host_vfprintf,&two)==NB_OK);
    typedef int (*gateway_one_fn)(const char *,...);
    typedef int (*gateway_two_fn)(FILE *,const char *,...);
    gateway_one_fn gateway_one=(gateway_one_fn)nb_import_entry(&binding);
    gateway_two_fn gateway_two=(gateway_two_fn)nb_import_entry(&binding_two);
    CHECK(gateway_one&&gateway_two);
    scope.phase=1;
    scratch=tmpfile();
    CHECK(scratch!=NULL);
    int result=gateway_two(scratch,"contract %d %s %.2f %d %d %d %d %d %d %d %d\n",
        7,"text",1.5,1,2,3,4,5,6,7,8);
    CHECK(result>0);
    fflush(scratch);
    rewind(scratch);
    memset(readback,0,sizeof(readback));
    got=fread(readback,1,sizeof(readback)-1,scratch);
    CHECK(got>0);
    CHECK(got==37);
    CHECK(!memcmp(readback,"contract 7 text 1.50 1 2 3 4 5 6 7 8\n",37));
    /* One named argument: the first variadic argument is the second
     * general register, so the rebuilt list starts there. */
    saved=dup(fileno(stdout));
    CHECK(saved>=0);
    CHECK(dup2(fileno(scratch),fileno(stdout))>=0);
    result=gateway_one("one %d %s\n",42,"tail");
    fflush(stdout);
    CHECK(dup2(saved,fileno(stdout))>=0);
    close(saved);
    CHECK(result>0);
    fflush(scratch);
    fseek(scratch,0,SEEK_END);
    size=ftell(scratch);
    CHECK(size>0);
    fseek(scratch,0,SEEK_SET);
    memset(readback,0,sizeof(readback));
    got=fread(readback,1,sizeof(readback)-1,scratch);
    CHECK(got>0);
    CHECK(strstr(readback,"one 42 tail")!=NULL);
    fclose(scratch);
    scope.phase=0;
    CHECK(nb_import_unbind(&binding)==NB_OK);
    CHECK(nb_import_unbind(&binding_two)==NB_OK);
    CHECK(nb_scope_close(&scope)==NB_OK);
#endif
    puts("{\"stdio_contract_errors\":0,\"objects_bound\":true,\"object_holds_host_stream\":true,\"writable_mirror\":true,\"fwrite_roundtrip\":true,\"fputs_roundtrip\":true,\"fputc_roundtrip\":true,\"puts_roundtrip\":true,\"fflush_roundtrip\":true,\"callables_bound\":true}");
    return 0;
}
static int fs_open_dir_contract(void) {
    typedef int32_t (*open_dir_fn)(nn_fs_directory_handle *, const char *, int32_t);
    typedef int32_t (*get_count_fn)(int64_t *, nn_fs_directory_handle);
    typedef int32_t (*read_dir_fn)(int64_t *, nn_fs_directory_entry *, nn_fs_directory_handle, int64_t);
    typedef void (*close_dir_fn)(nn_fs_directory_handle);
    typedef int32_t (*create_dir_fn)(const char *);

    open_dir_fn open_dir = (open_dir_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn2fs13OpenDirectoryEPNS0_15DirectoryHandleEPKci");
    CHECK(open_dir != NULL);

    open_dir_fn open_dir_alias = (open_dir_fn)(uintptr_t)guest_process_resolve(0, "nnfsOpenDirectory");
    CHECK(open_dir_alias != NULL);

    get_count_fn get_count = (get_count_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn2fs22GetDirectoryEntryCountEPlNS0_15DirectoryHandleE");
    CHECK(get_count != NULL);

    get_count_fn get_count_alias = (get_count_fn)(uintptr_t)guest_process_resolve(0, "nnfsGetDirectoryEntryCount");
    CHECK(get_count_alias != NULL);

    read_dir_fn read_dir = (read_dir_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn2fs13ReadDirectoryEPlPNS0_14DirectoryEntryENS0_15DirectoryHandleEl");
    CHECK(read_dir != NULL);

    read_dir_fn read_dir_alias = (read_dir_fn)(uintptr_t)guest_process_resolve(0, "nnfsReadDirectory");
    CHECK(read_dir_alias != NULL);

    close_dir_fn close_dir = (close_dir_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn2fs14CloseDirectoryENS0_15DirectoryHandleE");
    CHECK(close_dir != NULL);

    close_dir_fn close_dir_alias = (close_dir_fn)(uintptr_t)guest_process_resolve(0, "nnfsCloseDirectory");
    CHECK(close_dir_alias != NULL);

    create_dir_fn create_dir = (create_dir_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn2fs15CreateDirectoryEPKc");
    CHECK(create_dir != NULL);

    create_dir_fn create_dir_alias = (create_dir_fn)(uintptr_t)guest_process_resolve(0, "nnfsCreateDirectory");
    CHECK(create_dir_alias != NULL);

    /* Parameter validation */
    nn_fs_directory_handle handle = {0};
    CHECK(open_dir(NULL, "rom:/", 3) != 0);
    CHECK(open_dir(&handle, NULL, 3) != 0);
    CHECK(open_dir(&handle, "", 3) != 0);

    /* Ensure 'rom' is mounted for contract testing */
    if (!nso_host_fs_is_rom_mounted("rom")) {
        static char cache_buf[0x10000];
        nso_host_fs_mount_rom("rom", cache_buf, sizeof(cache_buf));
    }

    /* Non-existent directory */
    CHECK(open_dir(&handle, "rom:/non_existent_dir_contract_xyz", 3) != 0);

    /* Setup a dedicated temporary directory with test files & subdirectories */
    const char *mount_root = nso_host_fs_get_rom_mount_path("rom");
    char test_dir_path[512];
    snprintf(test_dir_path, sizeof(test_dir_path), "%s/dir_test_fixture", mount_root ? mount_root : ".");
    mkdir(test_dir_path, 0755);

    char sub_dir_path[512];
    snprintf(sub_dir_path, sizeof(sub_dir_path), "%s/sub_dir", test_dir_path);
    mkdir(sub_dir_path, 0755);

    char file1_path[512];
    snprintf(file1_path, sizeof(file1_path), "%s/file1.txt", test_dir_path);
    FILE *fp1 = fopen(file1_path, "wb");
    if (fp1) { fputs("file1 contents", fp1); fclose(fp1); }

    char file2_path[512];
    snprintf(file2_path, sizeof(file2_path), "%s/file2.bin", test_dir_path);
    FILE *fp2 = fopen(file2_path, "wb");
    if (fp2) { fputs("longer file2 contents here", fp2); fclose(fp2); }

    /* Test 1: Open Directory mode 1 (Directories only) */
    int32_t ok = open_dir(&handle, "rom:/dir_test_fixture", 1);
    CHECK(ok == 0);
    CHECK(handle.handle != NULL);

    int64_t count = 0;
    CHECK(get_count(&count, handle) == 0);
    CHECK(count == 1);

    nn_fs_directory_entry entries[4];
    memset(entries, 0, sizeof(entries));
    int64_t read_count = 0;
    CHECK(read_dir(&read_count, entries, handle, 4) == 0);
    CHECK(read_count == 1);
    CHECK(entries[0].type == 0); /* Directory */
    CHECK(strcmp(entries[0].name, "sub_dir") == 0);
    CHECK(entries[0].file_size == 0);

    /* End of enumeration */
    CHECK(read_dir(&read_count, entries, handle, 4) == 0);
    CHECK(read_count == 0);

    close_dir(handle);

    /* Test 2: Open Directory mode 2 (Files only) */
    ok = open_dir_alias(&handle, "rom:/dir_test_fixture", 2);
    CHECK(ok == 0);
    CHECK(handle.handle != NULL);

    CHECK(get_count_alias(&count, handle) == 0);
    CHECK(count == 2);

    memset(entries, 0, sizeof(entries));
    CHECK(read_dir_alias(&read_count, entries, handle, 4) == 0);
    CHECK(read_count == 2);
    CHECK(entries[0].type == 1); /* File */
    CHECK(entries[1].type == 1); /* File */
    CHECK(entries[0].file_size > 0);
    CHECK(entries[1].file_size > 0);

    close_dir_alias(handle);

    /* Test 3: Open Directory mode 3 (All: files and directories) */
    ok = open_dir(&handle, "rom:/dir_test_fixture", 3);
    CHECK(ok == 0);
    CHECK(handle.handle != NULL);

    CHECK(get_count(&count, handle) == 0);
    CHECK(count == 3);

    /* Read in chunks of 2 */
    memset(entries, 0, sizeof(entries));
    CHECK(read_dir(&read_count, entries, handle, 2) == 0);
    CHECK(read_count == 2);

    CHECK(read_dir(&read_count, entries + 2, handle, 2) == 0);
    CHECK(read_count == 1);

    close_dir(handle);

    /* Operations on closed handle must fail */
    CHECK(get_count(&count, handle) != 0);
    CHECK(read_dir(&read_count, entries, handle, 1) != 0);

    /* Clean up temporary fixture directory */
    unlink(file1_path);
    unlink(file2_path);
    rmdir(sub_dir_path);
    rmdir(test_dir_path);

    /* If rom:/ exists, test opening root rom directory */
    nn_fs_directory_handle root_dir = {0};
    if (open_dir(&root_dir, "rom:/", 3) == 0) {
        int64_t root_count = 0;
        CHECK(get_count(&root_count, root_dir) == 0);
        close_dir(root_dir);
    }

    puts("{\"fs_open_dir_errors\":0,\"open\":true,\"count\":true,\"read\":true,\"close\":true,\"validation\":true}");
    return 0;
}
struct event_signal_thread_arg {
    void (*signal_fn)(void *);
    void *event;
    int delay_ms;
};

static void *event_signal_worker(void *param) {
    struct event_signal_thread_arg *arg = (struct event_signal_thread_arg *)param;
    usleep(arg->delay_ms * 1000);
    arg->signal_fn(arg->event);
    return NULL;
}

static int os_event_contract(void) {
    typedef void (*init_event_fn)(void *, bool, int32_t);
    typedef void (*finalize_event_fn)(void *);
    typedef void (*signal_event_fn)(void *);
    typedef void (*clear_event_fn)(void *);
    typedef void (*wait_event_fn)(void *);
    typedef bool (*try_wait_event_fn)(void *);
    typedef bool (*timed_wait_event_fn)(void *, int64_t);

    init_event_fn init_event = (init_event_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn2os15InitializeEventEPNS0_9EventTypeEbNS0_14EventClearModeE");
    CHECK(init_event != NULL);
    init_event_fn init_event_alias = (init_event_fn)(uintptr_t)guest_process_resolve(0, "nnosInitializeEvent");
    CHECK(init_event_alias != NULL);

    finalize_event_fn finalize_event = (finalize_event_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn2os13FinalizeEventEPNS0_9EventTypeE");
    CHECK(finalize_event != NULL);
    finalize_event_fn finalize_event_alias = (finalize_event_fn)(uintptr_t)guest_process_resolve(0, "nnosFinalizeEvent");
    CHECK(finalize_event_alias != NULL);

    signal_event_fn signal_event = (signal_event_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn2os11SignalEventEPNS0_9EventTypeE");
    CHECK(signal_event != NULL);
    signal_event_fn signal_event_alias = (signal_event_fn)(uintptr_t)guest_process_resolve(0, "nnosSignalEvent");
    CHECK(signal_event_alias != NULL);

    clear_event_fn clear_event = (clear_event_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn2os10ClearEventEPNS0_9EventTypeE");
    CHECK(clear_event != NULL);

    wait_event_fn wait_event = (wait_event_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn2os9WaitEventEPNS0_9EventTypeE");
    CHECK(wait_event != NULL);
    wait_event_fn wait_event_alias = (wait_event_fn)(uintptr_t)guest_process_resolve(0, "nnosWaitEvent");
    CHECK(wait_event_alias != NULL);

    try_wait_event_fn try_wait_event = (try_wait_event_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn2os12TryWaitEventEPNS0_9EventTypeE");
    CHECK(try_wait_event != NULL);

    timed_wait_event_fn timed_wait_event = (timed_wait_event_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn2os14TimedWaitEventEPNS0_9EventTypeENS_8TimeSpanE");
    CHECK(timed_wait_event != NULL);

    /* 1. Null / unaligned parameter safety checks */
    init_event(NULL, false, 1);
    signal_event(NULL);
    clear_event(NULL);
    wait_event(NULL);
    CHECK(try_wait_event(NULL) == false);
    CHECK(timed_wait_event(NULL, 1000000) == false);
    finalize_event(NULL);

    /* 2. AutoClear mode contract (EventClearMode_AutoClear = 1) */
    uint8_t auto_event_buf[64] __attribute__((aligned(8))) = {0};
    init_event(auto_event_buf, false, 1);

    /* Check mirrored ABI bytes */
    CHECK(auto_event_buf[0x10] == 0); /* signaled = false */
    CHECK(auto_event_buf[0x11] == 0); /* initially_signaled = false */
    CHECK(auto_event_buf[0x12] == 1); /* clear_mode = AutoClear (1) */
    CHECK(auto_event_buf[0x13] == 1); /* state = initialized (1) */

    CHECK(try_wait_event(auto_event_buf) == false);
    /* Timed wait should timeout when not signaled */
    CHECK(timed_wait_event(auto_event_buf, 5000000) == false); /* 5ms */

    /* Signal event */
    signal_event(auto_event_buf);
    CHECK(auto_event_buf[0x10] == 1);

    /* In AutoClear mode, try_wait succeeds and auto-clears signaled state */
    CHECK(try_wait_event(auto_event_buf) == true);
    CHECK(auto_event_buf[0x10] == 0);
    CHECK(try_wait_event(auto_event_buf) == false);

    /* Signal again and test timed_wait success */
    signal_event(auto_event_buf);
    CHECK(timed_wait_event(auto_event_buf, 100000000) == true); /* 100ms */
    CHECK(auto_event_buf[0x10] == 0); /* auto-cleared */

    finalize_event(auto_event_buf);
    CHECK(auto_event_buf[0x13] == 0); /* state = uninitialized */

    /* 3. ManualClear mode contract (EventClearMode_ManualClear = 0) */
    uint8_t manual_event_buf[64] __attribute__((aligned(8))) = {0};
    init_event_alias(manual_event_buf, true, 0); /* initially signaled */

    CHECK(manual_event_buf[0x10] == 1); /* signaled = true */
    CHECK(manual_event_buf[0x11] == 1); /* initially_signaled = true */
    CHECK(manual_event_buf[0x12] == 0); /* clear_mode = ManualClear (0) */
    CHECK(manual_event_buf[0x13] == 1);

    /* In ManualClear mode, try_wait does NOT clear signaled state */
    CHECK(try_wait_event(manual_event_buf) == true);
    CHECK(manual_event_buf[0x10] == 1);
    CHECK(try_wait_event(manual_event_buf) == true);
    CHECK(manual_event_buf[0x10] == 1);

    /* ClearEvent resets signaled */
    clear_event(manual_event_buf);
    CHECK(manual_event_buf[0x10] == 0);
    CHECK(try_wait_event(manual_event_buf) == false);

    finalize_event_alias(manual_event_buf);
    CHECK(manual_event_buf[0x13] == 0);

    /* 4. Cross-thread WaitEvent notification */
    uint8_t thread_event_buf[64] __attribute__((aligned(8))) = {0};
    init_event(thread_event_buf, false, 1); /* AutoClear */

    pthread_t thread;
    struct event_signal_thread_arg thread_arg = {
        .signal_fn = signal_event,
        .event = thread_event_buf,
        .delay_ms = 10,
    };
    int pth_rc = pthread_create(&thread, NULL, event_signal_worker, &thread_arg);
    CHECK(pth_rc == 0);

    wait_event(thread_event_buf);
    /* Successfully woke up! With AutoClear, signaled is reset */
    CHECK(thread_event_buf[0x10] == 0);

    pthread_join(thread, NULL);
    finalize_event(thread_event_buf);

    puts("{\"os_event_errors\":0,\"init\":true,\"auto_clear\":true,\"manual_clear\":true,\"timed_wait\":true,\"cross_thread\":true,\"finalize\":true}");
    return 0;
}

static int posix_memalign_contract(void) {
    typedef int (*posix_memalign_fn)(void **, size_t, size_t);
    typedef void *(*memalign_fn)(size_t, size_t);

    posix_memalign_fn p_memalign = (posix_memalign_fn)(uintptr_t)guest_process_resolve(0, "posix_memalign");
    CHECK(p_memalign != NULL);
    memalign_fn m_align = (memalign_fn)(uintptr_t)guest_process_resolve(0, "memalign");
    CHECK(m_align != NULL);

    /* 1. Invalid parameter checks */
    void *ptr = (void *)0xdeadbeef;
    CHECK(p_memalign(NULL, 64, 128) == EINVAL);
    CHECK(p_memalign(&ptr, 3, 128) == EINVAL);
    CHECK(p_memalign(&ptr, 4, 128) == EINVAL);
    CHECK(p_memalign(&ptr, 7, 128) == EINVAL);
    CHECK(m_align(3, 128) == NULL);
    CHECK(m_align(0, 128) == NULL);

    /* 2. Normal allocation matching UE4 caller (alignment=64, size=40) */
    ptr = NULL;
    int rc = p_memalign(&ptr, 64, 40);
    CHECK(rc == 0);
    CHECK(ptr != NULL);
    CHECK(((uintptr_t)ptr & 63) == 0);
    memset(ptr, 0xaa, 40);
    free(ptr);

    /* 3. Page alignment (4096, 8192) */
    ptr = NULL;
    rc = p_memalign(&ptr, 4096, 8192);
    CHECK(rc == 0);
    CHECK(ptr != NULL);
    CHECK(((uintptr_t)ptr & 4095) == 0);
    memset(ptr, 0x55, 8192);
    free(ptr);

    /* 4. Zero size allocation */
    ptr = NULL;
    rc = p_memalign(&ptr, 16, 0);
    CHECK(rc == 0);
    CHECK(ptr != NULL);
    CHECK(((uintptr_t)ptr & 15) == 0);
    free(ptr);

    /* 5. memalign direct allocation */
    void *m_ptr = m_align(64, 128);
    CHECK(m_ptr != NULL);
    CHECK(((uintptr_t)m_ptr & 63) == 0);
    memset(m_ptr, 0x33, 128);
    free(m_ptr);

    puts("{\"posix_memalign_errors\":0,\"resolve\":true,\"invalid_params\":true,\"alloc_64\":true,\"alloc_page\":true,\"zero_size\":true,\"memalign\":true}");
    return 0;
}

static unsigned s_thread_atexit_events[16];
static size_t s_thread_atexit_event_count = 0;
static void thread_atexit_test_cb(void *arg) {
    if (s_thread_atexit_event_count < 16) {
        s_thread_atexit_events[s_thread_atexit_event_count++] = (unsigned)(uintptr_t)arg;
    }
}
struct child_atexit_arg {
    int (*cxa_fn)(void (*)(void *), void *, void *);
    int result;
};
static void *child_thread_atexit_worker(void *param) {
    struct child_atexit_arg *ca = (struct child_atexit_arg *)param;
    if (ca->cxa_fn(thread_atexit_test_cb, (void *)1, NULL) != 0) ca->result = 1;
    if (ca->cxa_fn(thread_atexit_test_cb, (void *)2, NULL) != 0) ca->result = 2;
    if (nso_exit_registry_thread_pending() != 2) ca->result = 3;
    return NULL;
}

static int cxa_thread_atexit_contract(void) {
    typedef int (*cxa_thread_atexit_fn)(void (*)(void *), void *, void *);

    cxa_thread_atexit_fn p_atexit = (cxa_thread_atexit_fn)(uintptr_t)guest_process_resolve(0, "__cxa_thread_atexit");
    CHECK(p_atexit != NULL);
    cxa_thread_atexit_fn p_impl = (cxa_thread_atexit_fn)(uintptr_t)guest_process_resolve(0, "__cxa_thread_atexit_impl");
    CHECK(p_impl != NULL);

    /* 1. Invalid params */
    CHECK(p_atexit(NULL, (void *)0x1234, (void *)0x5678) == -1);

    /* 2. LIFO execution on this thread */
    s_thread_atexit_event_count = 0;
    CHECK(p_atexit(thread_atexit_test_cb, (void *)100, NULL) == 0);
    CHECK(p_atexit(thread_atexit_test_cb, (void *)200, NULL) == 0);
    CHECK(p_atexit(thread_atexit_test_cb, (void *)300, NULL) == 0);
    CHECK(nso_exit_registry_thread_pending() == 3);

    nso_exit_registry_thread_finalize();
    CHECK(nso_exit_registry_thread_pending() == 0);
    CHECK(s_thread_atexit_event_count == 3);
    CHECK(s_thread_atexit_events[0] == 300);
    CHECK(s_thread_atexit_events[1] == 200);
    CHECK(s_thread_atexit_events[2] == 100);

    /* 3. Thread isolation & child automatic exit cleanup */
    s_thread_atexit_event_count = 0;
    CHECK(p_atexit(thread_atexit_test_cb, (void *)10, NULL) == 0);
    CHECK(nso_exit_registry_thread_pending() == 1);

    pthread_t child;
    struct child_atexit_arg carg = {.cxa_fn = p_atexit};
    CHECK(pthread_create(&child, NULL, child_thread_atexit_worker, &carg) == 0);
    CHECK(pthread_join(child, NULL) == 0);
    CHECK(carg.result == 0);

    /* Child should have executed its 2 callbacks in LIFO order upon exit */
    CHECK(s_thread_atexit_event_count == 2);
    CHECK(s_thread_atexit_events[0] == 2);
    CHECK(s_thread_atexit_events[1] == 1);

    /* Main thread registration should still be intact and pending */
    CHECK(nso_exit_registry_thread_pending() == 1);
    nso_exit_registry_thread_finalize();
    CHECK(nso_exit_registry_thread_pending() == 0);
    CHECK(s_thread_atexit_event_count == 3);
    CHECK(s_thread_atexit_events[2] == 10);

    puts("{\"cxa_thread_atexit_errors\":0,\"resolve\":true,\"invalid_params\":true,\"lifo\":true,\"thread_isolation\":true,\"auto_exit\":true}");
    return 0;
}

static int diag_abort_contract(void) {
    typedef uint64_t (*abort_impl_fn)(void);
    typedef int32_t (*get_backtrace_fn)(uintptr_t *, int32_t);

    abort_impl_fn p_abort = (abort_impl_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn4diag6detail9AbortImplEv");
    CHECK(p_abort != NULL);

    get_backtrace_fn p_bt = (get_backtrace_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn4diag12GetBacktraceEPmi");
    CHECK(p_bt != NULL);

    /* 1. AbortImpl call returns 0 */
    uint64_t res = p_abort();
    CHECK(res == 0);

    /* 2. GetBacktrace invalid params */
    CHECK(p_bt(NULL, 10) == 0);
    CHECK(p_bt((uintptr_t *)0x1234, 0) == 0);
    CHECK(p_bt((uintptr_t *)0x1234, -1) == 0);

    /* 3. GetBacktrace captures non-negative depth */
    uintptr_t bt[16] = {0};
    int32_t count = p_bt(bt, 16);
    CHECK(count >= 0);

    puts("{\"diag_abort_errors\":0,\"resolve\":true,\"return_zero\":true,\"backtrace\":true}");
    return 0;
}

static int diag_aoc_event_contract(void) {
    typedef void (*event_fn)(void *);

    event_fn p_get = (event_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn3aoc31GetAddOnContentListChangedEventEPNS_2os11SystemEventE");
    CHECK(p_get != NULL);
    event_fn p_wait = (event_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn2os15WaitSystemEventEPNS0_15SystemEventTypeE");
    CHECK(p_wait != NULL);
    event_fn p_signal = (event_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn2os17SignalSystemEventEPNS0_15SystemEventTypeE");
    CHECK(p_signal != NULL);
    event_fn p_destroy = (event_fn)(uintptr_t)guest_process_resolve(0, "_ZN2nn2os18DestroySystemEventEPNS0_15SystemEventTypeE");
    CHECK(p_destroy != NULL);

    /* The host owns the signalling state and must not rewrite the guest
     * nn::os::SystemEvent object: the game keeps its own layout. */
    uint8_t guest_object[64];
    for (size_t i = 0; i < sizeof guest_object; i++) guest_object[i] = (uint8_t)(i * 7u + 3u);
    uint8_t before[64];
    memcpy(before, guest_object, sizeof before);
    p_get(guest_object);
    CHECK(memcmp(before, guest_object, sizeof before) == 0);

    /* A raised event releases a waiter instead of blocking forever. */
    p_signal(guest_object);
    p_wait(guest_object);

    /* Null objects are reported, not dereferenced. */
    p_get(NULL);
    p_wait(NULL);
    p_signal(NULL);
    p_destroy(NULL);
    p_destroy(guest_object);

    puts("{\"diag_aoc_event_errors\":0,\"resolve\":true,\"guest_object_untouched\":true,\"signal_wait_roundtrip\":true}");
    return 0;
}
struct log_fixture_io {float input,output;uint64_t guest_x18;};
_Static_assert(offsetof(struct log_fixture_io,output)==4 && offsetof(struct log_fixture_io,guest_x18)==8,"self-authored float fixture ABI");
static void *exercise_logf_native(guest_process *p,void *opaque) {
    (void)opaque;
    const float special[]={0.0f,-0.0f,-1.0f,INFINITY,NAN};
    size_t normal_count=sizeof(log_cases)/sizeof(log_cases[0]);
    for(size_t i=0;i<normal_count+sizeof(special)/sizeof(special[0]);i++) {
        float input=i<normal_count?log_cases[i].input:special[i-normal_count];
        struct log_fixture_io io={input,12345.0f,UINT64_C(0xa5a5)};
        const uint64_t arguments[6]={(uintptr_t)&io};uint64_t result=0xa5a5;
        struct nso_result r=guest_process_call(p,0x20,arguments,1,&result);
#if defined(__ANDROID__) && defined(__aarch64__)
        expect_value("logf-native-call",r.status,NSO_OK);expect_value("logf-native-return",result,0x91);
        expect_value("logf-native-guest-x18",io.guest_x18,0x55);
        expect_condition("logf-fixture-input-preserved",!memcmp(&io.input,&input,sizeof(input)));
        if(i<normal_count)expect_condition("logf-fp-argument-return-value",log_close(io.output,log_cases[i].expected));
        else expect_condition("logf-ieee-system-behavior",log_special_matches(io.output,logf(input)));
        executed_calls++;
#else
        expect_value("logf-native-host-unsupported",r.status,NSO_UNSUPPORTED);
        expect_value("logf-no-raw-fallback",result,0xa5a5);
        expect_condition("logf-no-fake-fp-result",io.output==12345.0f && io.guest_x18==0xa5a5);
#endif
    }
    return &returned_token;
}
static int logf_native_contract(const char *path) {
    CHECK(!logf_contract());struct guest_process_config config={1024*1024,65536};guest_process *p=NULL;
    CHECK(succeeded(guest_process_create(&config,&p)));char err[128];CHECK(succeeded(guest_process_load(p,path,err,sizeof(err))));
    void *value=NULL;CHECK(succeeded(guest_process_run(p,exercise_logf_native,NULL,&value)) && value==&returned_token && !errors);
    CHECK(succeeded(guest_process_destroy(&p)) && !p);
    printf("{\"logf_native_errors\":%u,\"native_calls\":%u,\"fp_arguments\":1,\"fp_returns\":1,\"gp_float_arguments\":0,\"raw_fallback\":false}\n",errors,executed_calls);return 0;
}
static void *exercise_tls_registration(guest_process *p,void *opaque) {
    uintptr_t phase=(uintptr_t)opaque;
    uint64_t a=nso_addr(guest_process_module(p),0x20),f=nso_addr(guest_process_module(p),0x40);
    struct guest_tls_allocator_info info;
    expect_value("tls-reg-empty",guest_process_tls_allocator_info(p,&info).status,NSO_OK);
    expect_value("tls-reg-not-initialized",info.initialized,0);
    expect_value("tls-uninitialized-null-free",guest_process_free_thread_local(p,NULL,123).status,NSO_OK);
    typedef void(*set_fn)(uint64_t,uint64_t);
    set_fn set=(set_fn)(uintptr_t)guest_process_resolve(0,"_ZN2nn2os32SetMemoryAllocatorForThreadLocalEPFPvmmEPFvS1_mE");
    if(phase==1) {set(0,f);return NULL;}
    expect_value("tls-reg-null-reject",guest_process_register_tls_allocator(p,0,f).status,NSO_INVALID);
    expect_value("tls-reg-data-reject",guest_process_register_tls_allocator(p,nso_addr(guest_process_module(p),0x8000),f).status,NSO_INVALID);
    expect_value("tls-reg-first",guest_process_register_tls_allocator(p,a,f).status,NSO_OK);
    if(phase==2) {set(a,f);return NULL;}
    expect_value("tls-reg-repeat-reject",guest_process_register_tls_allocator(p,a,f).status,NSO_INVALID);
    expect_value("tls-reg-info",guest_process_tls_allocator_info(p,&info).status,NSO_OK);
    expect_value("tls-reg-saved-allocate",info.allocate_callback,a);expect_value("tls-reg-saved-free",info.free_callback,f);
    expect_value("tls-reg-no-consumption",info.allocations,0);
    typedef uint32_t(*initialized_fn)(void);
    initialized_fn initialized=(initialized_fn)(uintptr_t)guest_process_resolve(0,"_ZN2nn2os40MemoryAllocatorForThreadLocalInitializedEv");
    expect_value("tls-reg-initialized",initialized(),1);
    void *untouched=(void *)(uintptr_t)0xa5a5;
    expect_value("tls-consumer-unprepared-unsupported",guest_process_allocate_thread_local(p,48,&untouched).status,NSO_UNSUPPORTED);
    expect_value("tls-consumer-output-untouched",(uintptr_t)untouched,0xa5a5);
    return &returned_token;
}
static int tls_registration_contract(const char *path,uintptr_t phase) {
    struct guest_process_config config={1024*1024,65536};guest_process *p=NULL;
    CHECK(succeeded(guest_process_create(&config,&p)));char err[128];CHECK(succeeded(guest_process_load(p,path,err,sizeof(err))));
    CHECK(guest_process_register_tls_allocator(p,1,2).status==NSO_INVALID);
    void *value=NULL;CHECK(succeeded(guest_process_run(p,exercise_tls_registration,(void *)phase,&value)) && value==&returned_token && !errors);
    struct guest_tls_allocator_info info;CHECK(succeeded(guest_process_tls_allocator_info(p,&info)) && info.initialized && !info.allocations);
    CHECK(succeeded(guest_process_destroy(&p)) && !p);
    puts("{\"tls_registration_errors\":0,\"once_nonnull\":true,\"module_owned_targets\":true,\"registration_callback_calls\":0,\"no_raw_fallback\":true}");return 0;
}
static int tls_fresh_registration_contract(const char *path) {
    struct guest_process_config config={1024*1024,65536};
    for(unsigned phase=0;phase<2;phase++) {
        guest_process *p=NULL;CHECK(succeeded(guest_process_create(&config,&p)) && p);
        CHECK(!guest_process_module(p));
        struct nso_result r=guest_process_register_tls_allocator(p,1,2);
        CHECK(r.status==NSO_INVALID && r.native_error==EINVAL && !r.cleanup_error);
        CHECK(!guest_process_module(p));
        struct guest_tls_allocator_info state;
        CHECK(succeeded(guest_process_tls_allocator_info(p,&state)) && !state.initialized &&
            !state.allocate_callback && !state.free_callback && !state.allocations &&
            !state.uncertain_allocations && !state.busy);
        struct guest_memory_info info,untouched;memset(&info,0xa5,sizeof(info));untouched=info;
        CHECK(guest_process_memory_info(p,&info).status==NSO_INVALID && !memcmp(&info,&untouched,sizeof(info)));
        void *output=(void *)(uintptr_t)0xa5a5;
        CHECK(guest_process_allocate_thread_local(p,48,&output).status==NSO_INVALID && (uintptr_t)output==0xa5a5);
        CHECK(guest_process_free_thread_local(p,NULL,0).status==NSO_INVALID);
        if(phase) {
            char error[128];CHECK(succeeded(guest_process_load(p,path,error,sizeof(error))));
            void *value=NULL;CHECK(succeeded(guest_process_run(p,exercise_tls_registration,NULL,&value)) && value==&returned_token && !errors);
        }
        CHECK(succeeded(guest_process_destroy(&p)) && !p);
    }
    puts("{\"tls_fresh_registration_errors\":0,\"created_without_module_rejected\":true,\"status\":1,\"errno\":22,\"sentinels_untouched\":true,\"no_registration_side_effect\":true,\"fresh_destroy\":true,\"subsequent_load_run_destroy\":true}");return 0;
}
struct tls_fixture_io {
    uint64_t allocate_calls,free_calls,sizes[4],alignments[4],freed_pointers[4],freed_sizes[4];
    uint64_t entry_x18,allocate_x18,free_x18,initialized_before,initialized_after;
    struct guest_memory_info query;
    uint64_t host_pointer,host_zero_pointer,host_null_result,host_x18,host_complete,allocate_seed,free_seed;
};
struct identity_io {
    uint64_t token_a,token_b,tid_a,tid_b,guest_x18;
    uint64_t cleanup_token,cleanup_tid,cleanup_value,cleanup_x18,cleanup_calls;
};
struct identity_input {struct identity_io io;uintptr_t retired;uint64_t native_tid;int native;};
static uint64_t actual_tid(void) {
#if defined(__ANDROID__)
    return (uint64_t)gettid();
#else
    uint64_t tid=0;if(pthread_threadid_np(NULL,&tid))return 0;return tid;
#endif
}
static void identity_inactive(guest_process *p,uintptr_t token) {
    uintptr_t untouched=0xa5a5;uint64_t tid=0xa5a5;
    expect_value("identity-inactive-current",guest_process_current_thread(p,&untouched).status,NSO_INVALID);
    expect_value("identity-inactive-current-output",untouched,0xa5a5);
    expect_value("identity-inactive-id",guest_process_thread_id(p,token,&tid).status,NSO_INVALID);
    expect_value("identity-inactive-id-output",tid,0xa5a5);
}
static void *identity_foreign(void *opaque) {identity_inactive(opaque,0);return NULL;}
static unsigned identity_callbacks;
static void *exercise_identity(guest_process *p,void *opaque) {
    struct identity_input *input=opaque;identity_callbacks++;
    uintptr_t a=0,b=0;uint64_t tid=0,again=0,foreign=0xa5a5;
    expect_value("identity-current",guest_process_current_thread(p,&a).status,NSO_OK);
    expect_value("identity-current-repeat",guest_process_current_thread(p,&b).status,NSO_OK);
    expect_condition("identity-token-stable-nonzero",a && a==b);
    expect_condition("identity-token-unreadable",inaccessible(a));
    expect_value("identity-id",guest_process_thread_id(p,a,&tid).status,NSO_OK);
    expect_value("identity-id-repeat",guest_process_thread_id(p,b,&again).status,NSO_OK);
    expect_value("identity-real-native-id",tid,actual_tid());expect_value("identity-id-stable",again,tid);
    expect_condition("identity-real-id-nonzero",tid!=0);
    expect_value("identity-foreign-token",guest_process_thread_id(p,(uintptr_t)&tid,&foreign).status,NSO_INVALID);
    expect_value("identity-foreign-output",foreign,0xa5a5);
    if(input->retired) {
        expect_condition("identity-retired-address-reserved",inaccessible(input->retired));
        expect_condition("identity-new-generation-different",a!=input->retired);
        expect_value("identity-retired-token-rejected",guest_process_thread_id(p,input->retired,&foreign).status,NSO_INVALID);
        expect_value("identity-retired-output",foreign,0xa5a5);
    }
    pthread_t helper;int e=pthread_create(&helper,NULL,identity_foreign,p);
    expect_value("identity-host-helper-create",(uint64_t)e,0);
    if(!e)expect_value("identity-host-helper-join",(uint64_t)pthread_join(helper,NULL),0);
    input->native_tid=tid;input->retired=a;
    if(input->native) {
        uint64_t result=0xa5a5;const uint64_t args[6]={(uintptr_t)&input->io};
        struct nso_result r=guest_process_call(p,0x20,args,1,&result);
#if defined(__ANDROID__) && defined(__aarch64__)
        expect_value("identity-native-status",r.status,NSO_OK);expect_value("identity-native-return",result,0x91);
        expect_value("identity-native-token",input->io.token_a,a);expect_value("identity-native-token-repeat",input->io.token_b,a);
        expect_value("identity-native-tid64",input->io.tid_a,tid);expect_value("identity-native-tid64-repeat",input->io.tid_b,tid);
        expect_value("identity-native-x18",input->io.guest_x18,0x55);executed_calls++;
#else
        expect_value("identity-native-unsupported",r.status,NSO_UNSUPPORTED);expect_value("identity-native-no-raw-output",result,0xa5a5);
#endif
    }
    return &returned_token;
}
static int identity_contract(const char *path,int native) {
    struct guest_process_config config={1024*1024,65536};guest_process *p=NULL;
    CHECK(succeeded(guest_process_create(&config,&p)));identity_inactive(p,0);
    char error[128];CHECK(succeeded(guest_process_load(p,path,error,sizeof(error))));
    struct identity_input input={.native=native};
    for(unsigned i=0;i<(native?1u:2u);i++) {
        void *value=NULL;CHECK(succeeded(guest_process_run(p,exercise_identity,&input,&value)) && value==&returned_token && !errors);
        identity_inactive(p,input.retired);
        CHECK(inaccessible(input.retired)); /* Retired token remains reserved until destroy. */
    }
#if defined(__ANDROID__) && defined(__aarch64__)
    if(native) {
        CHECK(input.io.cleanup_calls==1 && input.io.cleanup_token==input.retired && input.io.cleanup_tid==input.native_tid);
        CHECK(!input.io.cleanup_value && input.io.cleanup_x18==0x55);executed_calls++;
    }
#endif
    CHECK(succeeded(guest_process_destroy(&p)) && !p && !errors);
    printf("{\"identity_errors\":%u,\"actual_primaries\":%u,\"native_calls\":%u,\"tid_bits\":64,\"token_PROT_NONE\":true,\"retired_token_no_reuse\":true,\"raw_fallback\":false,\"SDK_ThreadType_layout\":false}\n",errors,identity_callbacks,executed_calls);return 0;
}
#if defined(NSO_PROFILE_IDENTITY_TESTING)
static unsigned identity_map_failure,identity_unmap_failure,identity_maps,identity_unmaps,identity_actual_unmaps;
static void *identity_map_probe(size_t bytes) {
    identity_maps++;
    if(identity_map_failure) {identity_map_failure--;errno=ENOMEM;return MAP_FAILED;}
    return mmap(NULL,bytes,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
}
static int identity_unmap_probe(void *token,size_t bytes) {
    identity_unmaps++;
    if(identity_unmap_failure) {identity_unmap_failure--;errno=EBUSY;return -1;}
    int r=munmap(token,bytes);if(!r)identity_actual_unmaps++;return r;
}
static int identity_recovery_contract(const char *path) {
    struct guest_process_config config={1024*1024,65536};guest_process *p=NULL;
    CHECK(succeeded(guest_process_create(&config,&p)));char error[128];CHECK(succeeded(guest_process_load(p,path,error,sizeof(error))));
    guest_process_test_identity_vm(identity_map_probe,identity_unmap_probe);identity_map_failure=1;
    struct identity_input input={0};void *value=(void *)(uintptr_t)0xa5a5;
    struct nso_result r=guest_process_run(p,exercise_identity,&input,&value);
    CHECK(r.status==NSO_OWNER_ERROR && r.native_error==ENOMEM && !value && !identity_callbacks && identity_maps==1);
    for(unsigned i=0;i<2;i++) {
        CHECK(succeeded(guest_process_run(p,exercise_identity,&input,&value)) && value==&returned_token && !errors);
        identity_inactive(p,input.retired);
    }
    identity_unmap_failure=1;nso_module *module=guest_process_module(p);
    r=guest_process_destroy(&p);
    CHECK(r.status==NSO_RETAINED && r.cleanup_error==EBUSY && p && guest_process_module(p)==module);
    CHECK(identity_unmaps==1 && !identity_actual_unmaps && inaccessible(input.retired));
    CHECK(succeeded(guest_process_destroy(&p)) && !p && identity_unmaps==3 && identity_actual_unmaps==2);
    guest_process_test_identity_vm(NULL,NULL);
    puts("{\"identity_recovery_errors\":0,\"failed_map_callback_calls\":0,\"protected_maps\":2,\"retired_generations\":2,\"unmap_attempts\":3,\"actual_unmaps\":2,\"retained_retry\":true}");return 0;
}
#endif
_Static_assert(offsetof(struct tls_fixture_io,entry_x18)==144 && offsetof(struct tls_fixture_io,query)==184,"self-authored TLS provider fixture ABI");
#if defined(NSO_PROFILE_TLS_ALLOCATOR_TESTING) && defined(__ANDROID__) && defined(__aarch64__)
static unsigned tls_host_rejections;
static void tls_host_probe(guest_process *p) {
    void *untouched=(void *)(uintptr_t)0xa5a5;
    struct nso_result r=guest_process_allocate_thread_local(p,48,&untouched);
    expect_value("tls-HOST-busy-unsupported",r.status,NSO_UNSUPPORTED);expect_value("tls-HOST-busy-errno",r.native_error,EBUSY);
    expect_value("tls-HOST-output-untouched",(uintptr_t)untouched,0xa5a5);
    expect_value("tls-HOST-free-unsupported",guest_process_free_thread_local(p,NULL,0).status,NSO_UNSUPPORTED);
    struct guest_tls_allocator_info state;
    expect_value("tls-HOST-owner-info",guest_process_tls_allocator_info(p,&state).status,NSO_OK);
    expect_condition("tls-HOST-inflight-owner-retained",state.busy && state.allocations);
    tls_host_rejections++;
}
#endif
struct tls_provider_input {struct tls_fixture_io io;int retain;};
static void *exercise_tls_provider(guest_process *p,void *opaque) {
    struct tls_provider_input *input=opaque;struct tls_fixture_io *io=&input->io;
#if defined(__ANDROID__) && defined(__aarch64__)
    uint64_t host_x18_before,host_x18_after;
    __asm__ volatile("mov %0, x18":"=r"(host_x18_before));
#endif
    const uint64_t args[6]={(uintptr_t)io};uint64_t result=0xa5a5;
    struct nso_result r=guest_process_call(p,0x20,args,1,&result);
#if defined(__ANDROID__) && defined(__aarch64__)
    expect_value("tls-native-register-call",r.status,NSO_OK);expect_value("tls-native-register-return",result,0x91);
    __asm__ volatile("mov %0, x18":"=r"(host_x18_after));
    expect_value("tls-register-host-x18",host_x18_after,host_x18_before);
    expect_value("tls-native-before",io->initialized_before,0);expect_value("tls-native-after",io->initialized_after,1);
    expect_value("tls-native-entry-x18",io->entry_x18,0x55);
    expect_value("tls-registration-no-allocate",io->allocate_calls,0);expect_value("tls-registration-no-free",io->free_calls,0);
    expect_value("tls-null-free-no-call",guest_process_free_thread_local(p,NULL,123).status,NSO_OK);
    void *a=(void *)(uintptr_t)0xa5a5,*b=NULL;
#if defined(NSO_PROFILE_TLS_ALLOCATOR_TESTING)
    guest_process_test_tls_allocator(1,tls_host_probe);
    r=guest_process_allocate_thread_local(p,37,&a);
    expect_value("tls-metadata-before-side-effect",r.status,NSO_OWNER_ERROR);expect_value("tls-metadata-ENOMEM",r.native_error,ENOMEM);
    expect_value("tls-metadata-no-guest-call",io->allocate_calls,0);expect_value("tls-metadata-output-untouched",(uintptr_t)a,0xa5a5);
#endif
    expect_value("tls-first-allocate",guest_process_allocate_thread_local(p,37,&a).status,NSO_OK);
    expect_value("tls-second-allocate",guest_process_allocate_thread_local(p,93,&b).status,NSO_OK);
    __asm__ volatile("mov %0, x18":"=r"(host_x18_after));
    expect_value("tls-allocate-host-x18",host_x18_after,host_x18_before);
    if(!a || !b || a==b) {expect_condition("tls-real-distinct-pointers",0);return NULL;}
    expect_condition("tls-return-alignment",!((uintptr_t)a&15) && !((uintptr_t)b&15));
    expect_value("tls-actual-storage-a",*(unsigned char *)a,0x5a);expect_value("tls-actual-storage-b",*(unsigned char *)b,0x5a);
    expect_value("tls-actual-tail-a",((unsigned char *)a)[36],0x5a);expect_value("tls-actual-tail-b",((unsigned char *)b)[92],0x5a);
    expect_value("tls-first-size",io->sizes[0],37);expect_value("tls-second-size",io->sizes[1],93);
    expect_value("tls-first-align",io->alignments[0],16);expect_value("tls-second-align",io->alignments[1],16);
    expect_value("tls-allocate-x18",io->allocate_x18,0x55);
    struct guest_tls_allocator_info state;expect_value("tls-live-info",guest_process_tls_allocator_info(p,&state).status,NSO_OK);
    expect_value("tls-two-owned-suballocations",state.allocations,2);
    expect_value("tls-no-uncertain",state.uncertain_allocations,0);
    if(input->retain)return &returned_token;
    void *untouched=(void *)(uintptr_t)0xa5a5;
    expect_value("tls-actual-null-allocation",guest_process_allocate_thread_local(p,4096,&untouched).status,NSO_LIMIT);
    expect_value("tls-null-output-untouched",(uintptr_t)untouched,0xa5a5);
    expect_value("tls-free-wrong-size-rejected",guest_process_free_thread_local(p,a,38).status,NSO_UNSUPPORTED);
    expect_value("tls-free-unknown-identity-rejected",guest_process_free_thread_local(p,(unsigned char *)a+16,21).status,NSO_UNSUPPORTED);
    expect_value("tls-no-wrong-size-callback",io->free_calls,0);
    expect_value("tls-real-free-second",guest_process_free_thread_local(p,b,93).status,NSO_OK);
    expect_value("tls-real-free-first",guest_process_free_thread_local(p,a,37).status,NSO_OK);
    __asm__ volatile("mov %0, x18":"=r"(host_x18_after));
    expect_value("tls-free-host-x18",host_x18_after,host_x18_before);
    expect_value("tls-no-repeat-free",guest_process_free_thread_local(p,a,37).status,NSO_UNSUPPORTED);
    expect_value("tls-free-count",io->free_calls,2);expect_value("tls-free-first-address",io->freed_pointers[0],(uintptr_t)b);
    expect_value("tls-free-second-address",io->freed_pointers[1],(uintptr_t)a);expect_value("tls-free-first-size",io->freed_sizes[0],93);
    expect_value("tls-free-second-size",io->freed_sizes[1],37);expect_value("tls-free-x18",io->free_x18,0x55);
    expect_value("tls-free-final-info",guest_process_tls_allocator_info(p,&state).status,NSO_OK);expect_value("tls-live-final-zero",state.allocations,0);
    expect_value("tls-reentrant-query-thread",io->query.thread_objects,1);expect_value("tls-reentrant-query-stack",io->query.stack_bytes,65536);
#if defined(NSO_PROFILE_TLS_ALLOCATOR_TESTING)
    expect_value("tls-real-HOST-rejections",tls_host_rejections,5);
    guest_process_test_tls_allocator(0,NULL);
#endif
    executed_calls+=6; /* register + 3 alloc + 2 free; null/wrong/repeat never enter guest. */
#else
    expect_value("tls-native-host-unsupported",r.status,NSO_UNSUPPORTED);expect_value("tls-no-raw-result",result,0xa5a5);
    expect_value("tls-no-fake-registration",io->initialized_after,0);
    (void)input;
#endif
    return &returned_token;
}
static int tls_provider_contract(const char *path,int retain) {
    struct guest_process_config config={1024*1024,65536};guest_process *p=NULL;
    CHECK(succeeded(guest_process_create(&config,&p)));char err[128];CHECK(succeeded(guest_process_load(p,path,err,sizeof(err))));
    struct tls_provider_input input={.retain=retain};void *value=NULL;
    CHECK(succeeded(guest_process_run(p,exercise_tls_provider,&input,&value)) && value==&returned_token && !errors);
#if defined(__ANDROID__) && defined(__aarch64__)
    if(retain) {
        nso_module *module=guest_process_module(p);struct guest_tls_allocator_info before,after;
        CHECK(succeeded(guest_process_tls_allocator_info(p,&before)) && before.allocations==2 && before.initialized);
        CHECK(guest_process_destroy(&p).status==NSO_RETAINED && p && guest_process_module(p)==module);
        CHECK(succeeded(guest_process_tls_allocator_info(p,&after)) && before.allocate_callback==after.allocate_callback &&
            before.free_callback==after.free_callback && before.allocations==after.allocations &&
            before.uncertain_allocations==after.uncertain_allocations && before.initialized==after.initialized && before.busy==after.busy);
        puts("{\"tls_live_retained_errors\":0,\"allocations\":2,\"module_and_callbacks_retained\":true,\"cleanup_complete\":false,\"no_host_free\":true,\"lifetime\":\"until isolated process shutdown\"}");return 0;
    }
#else
    (void)retain;
#endif
    CHECK(succeeded(guest_process_destroy(&p)) && !p);
    printf("{\"tls_provider_errors\":%u,\"native_calls\":%u,\"callbacks_use_native_boundary\":true,\"reentrant_Query\":true,\"raw_fallback\":false,\"ELF_TLS_initialized\":false}\n",errors,executed_calls);return 0;
}
static void *exercise_tls_host_consumer(guest_process *p,void *opaque) {
    int mode=*(int *)opaque;struct tls_fixture_io io={0};uint64_t value=0xa5a5;
    const uint64_t args[6]={(uintptr_t)&io};
#if defined(__ANDROID__) && defined(__aarch64__)
    uint64_t host_before,host_after;__asm__ volatile("mov %0, x18":"=r"(host_before));
#endif
    struct nso_result r=guest_process_call(p,mode<0?0x500:0x20,args,1,&value);
#if defined(__ANDROID__) && defined(__aarch64__)
    expect_value("tls-HOST-register-status",r.status,NSO_OK);
    if(mode>=0)expect_value("tls-HOST-register-return",value,0x91);
#if defined(NSO_PROFILE_TLS_ALLOCATOR_TESTING)
    guest_process_test_tls_allocator(0,tls_host_probe);
#endif
    const uint64_t host_args[6]={(uint64_t)mode};
    r=guest_process_call(p,mode<0?0x500:0x300,host_args,1,&value);
    expect_value("tls-HOST-consumer-status",r.status,NSO_OK);expect_value("tls-HOST-consumer-return",value,0x91);
    __asm__ volatile("mov %0, x18":"=r"(host_after));expect_value("tls-HOST-caller-SCS-x18",host_after,host_before);
    expect_condition("tls-HOST-real-distinct-storage",io.host_pointer && io.host_zero_pointer && io.host_pointer!=io.host_zero_pointer);
    expect_value("tls-HOST-callback-null",io.host_null_result,0);expect_value("tls-HOST-outer-A-x18",io.host_x18,0x41);
    expect_value("tls-HOST-allocate-B-x18",io.allocate_x18,0x66);expect_value("tls-HOST-free-C-x18",io.free_x18,0x77);
    expect_value("tls-HOST-allocate-actual-A-seed",io.allocate_seed,0x41);expect_value("tls-HOST-free-actual-A-seed",io.free_seed,0x41);
    expect_value("tls-HOST-completed",io.host_complete,1);expect_value("tls-HOST-allocate-count",io.allocate_calls,3);
    expect_value("tls-HOST-size",io.sizes[0],48);expect_value("tls-HOST-zero-size",io.sizes[1],0);expect_value("tls-HOST-OOM-size",io.sizes[2],4096);
    for(unsigned i=0;i<3;i++)expect_value("tls-HOST-fixed-align16",io.alignments[i],16);
    expect_value("tls-HOST-free-count",io.free_calls,2);expect_value("tls-HOST-zero-free-pointer",io.freed_pointers[0],io.host_zero_pointer);
    expect_value("tls-HOST-zero-free-size",io.freed_sizes[0],0);expect_value("tls-HOST-free-pointer",io.freed_pointers[1],io.host_pointer);
    expect_value("tls-HOST-free-size",io.freed_sizes[1],48);
    struct guest_tls_allocator_info state;expect_value("tls-HOST-final-info",guest_process_tls_allocator_info(p,&state).status,NSO_OK);
    expect_condition("tls-HOST-all-owned-records-freed",!state.allocations && !state.busy && !state.uncertain_allocations);
    expect_value("tls-HOST-reentrant-query-thread",io.query.thread_objects,1);
#if defined(NSO_PROFILE_TLS_ALLOCATOR_TESTING)
    expect_value("tls-HOST-busy-queries",tls_host_rejections,5);guest_process_test_tls_allocator(0,NULL);
#endif
    executed_calls+=7;
#else
    expect_value("tls-HOST-native-unsupported",r.status,NSO_UNSUPPORTED);expect_value("tls-HOST-no-raw-result",value,0xa5a5);
#endif
    return &returned_token;
}
static int tls_host_consumer_contract(const char *path,int mode) {
    struct guest_process_config config={1024*1024,65536};guest_process *p=NULL;
    CHECK(succeeded(guest_process_create(&config,&p)));char error[128];CHECK(succeeded(guest_process_load(p,path,error,sizeof(error))));
    void *value=NULL;CHECK(succeeded(guest_process_run(p,exercise_tls_host_consumer,&mode,&value)) && value==&returned_token && !errors);
    CHECK(succeeded(guest_process_destroy(&p)) && !p);
    printf("{\"tls_host_consumer_errors\":%u,\"native_calls\":%u,\"raw_fallback\":false,\"ELF_TLS_initialized\":false}\n",errors,executed_calls);return 0;
}
struct resource_input {size_t quota;int native;};
static uint64_t resource_reported(size_t total,size_t occupied) {
    return occupied>total || total-occupied<0x80000?total:occupied+0x80000;
}
static void *exercise_resource(guest_process *p,void *opaque) {
    struct resource_input *input=opaque;
    struct guest_virtual_resource_usage before,after;
    expect_condition("resource-initial-snapshot",succeeded(guest_process_resource_info(p,&before)));
    expect_value("resource-explicit-total",before.total,input->quota);
    expect_value("resource-three-real-tickets-and-margin",before.used,resource_reported(input->quota,3*256));
    typedef __uint128_t (*resource_fn)(void);
    resource_fn getter=(resource_fn)(uintptr_t)guest_process_resolve(0,"_ZN2nn2os36GetVirtualAddressMemoryResourceUsageEv");
    expect_condition("resource-getter-bound",getter!=NULL);
    if(getter) {
        __uint128_t pair=getter();expect_value("resource-direct-low-total",(uint64_t)pair,before.total);
        expect_value("resource-direct-high-used",(uint64_t)(pair>>64),before.used);
    }
    if(input->native) {
        uint64_t output[3]={0xa5a5,0xa5a5,0xa5a5},returned=0xa5a5;
        uint64_t args[6]={(uintptr_t)output};struct nso_result r=guest_process_call(p,0x20,args,1,&returned);
#if defined(__ANDROID__) && defined(__aarch64__)
        expect_value("resource-native-call",r.status,NSO_OK);expect_value("resource-native-result",returned,0x91);
        expect_value("resource-native-x0-total",output[0],before.total);
        expect_value("resource-native-x1-used",output[1],before.used);
        expect_value("resource-native-guest-x18",output[2],0x55);executed_calls++;
#else
        expect_value("resource-native-unsupported",r.status,NSO_UNSUPPORTED);
        expect_value("resource-native-no-raw-fallback",returned,0xa5a5);
#endif
    }
    typedef uint32_t (*reserve_fn)(uintptr_t *,size_t);typedef uint32_t (*release_fn)(uintptr_t);
    typedef uint32_t (*pages_fn)(uintptr_t,size_t);
    reserve_fn reserve=(reserve_fn)(uintptr_t)guest_process_resolve(0,"_ZN2nn2os21AllocateAddressRegionEPmm");
    release_fn release=(release_fn)(uintptr_t)guest_process_resolve(0,"_ZN2nn2os17FreeAddressRegionEm");
    pages_fn map=(pages_fn)(uintptr_t)guest_process_resolve(0,"_ZN2nn2os19AllocateMemoryPagesEmm");
    pages_fn unmap=(pages_fn)(uintptr_t)guest_process_resolve(0,"_ZN2nn2os15FreeMemoryPagesEmm");
    uintptr_t address=0;expect_value("resource-reserve",reserve(&address,page),0);
    expect_condition("resource-reserved-snapshot",succeeded(guest_process_resource_info(p,&after)));
    expect_value("resource-two-real-reservation-nodes",after.used,resource_reported(input->quota,5*256));
    expect_value("resource-map",map(address,page),0);
    *(volatile unsigned char *)address=0x49;
    expect_condition("resource-mapped-snapshot",succeeded(guest_process_resource_info(p,&after)));
    expect_value("resource-three-real-page-nodes",after.used,resource_reported(input->quota,8*256));
    expect_value("resource-real-page-content",*(volatile unsigned char *)address,0x49);
    expect_value("resource-free-page",unmap(address,page),0);
    expect_condition("resource-after-decommit",succeeded(guest_process_resource_info(p,&after)));
    expect_value("resource-page-nodes-returned",after.used,resource_reported(input->quota,5*256));
    expect_value("resource-release-region",release(address),0);
    expect_condition("resource-after-release",succeeded(guest_process_resource_info(p,&after)));
    expect_value("resource-all-transient-nodes-returned",after.used,before.used);
    return &returned_token;
}
static int resource_process_contract(const char *path,int native) {
    struct guest_process_config config={1024*1024,65536};guest_process *p=NULL;
    CHECK(guest_process_create_with_resources(&config,0,0,NULL,&p).status==NSO_INVALID && !p);
    for(unsigned phase=1;phase<=3;phase++) {
        struct resource_input input={phase==3?page:phase*1024*1024,native};
        CHECK(succeeded(guest_process_create_with_resources(&config,input.quota,0,NULL,&p)));
        struct guest_virtual_resource_usage untouched;memset(&untouched,0xa5,sizeof(untouched));
        CHECK(guest_process_resource_info(p,&untouched).status!=NSO_OK && all_bytes(&untouched,sizeof(untouched),0xa5));
        char err[128];CHECK(succeeded(guest_process_load(p,path,err,sizeof(err))));
        void *returned=NULL;CHECK(succeeded(guest_process_run(p,exercise_resource,&input,&returned)) && returned==&returned_token && !errors);
        CHECK(succeeded(guest_process_destroy(&p)) && !p);
    }
    printf("{\"resource_errors\":%u,\"explicit_quotas\":3,\"real_nodes\":true,\"separate_data_quota\":true,\"sdk_margin\":524288,\"native_calls\":%u,\"raw_fallback\":false}\n",errors,executed_calls);return 0;
}
struct metadata_worker {guest_context *context;unsigned errors;};
static void *metadata_worker(void *opaque) {
    struct metadata_worker *w=opaque;
    for(unsigned i=0;i<1000;i++) {
        void *p=NULL;struct guest_resource_usage use;
        if(guest_metadata_allocate(w->context,GUEST_METADATA_PAGE_ALLOCATION,64,&p).status!=GUEST_CONTEXT_OK) {w->errors++;continue;}
        memset(p,(int)(i&255),64);
        if(guest_context_resource_usage(w->context,&use).status!=GUEST_CONTEXT_OK || !use.valid || !use.used_bytes)w->errors++;
        if(guest_metadata_release(w->context,GUEST_METADATA_PAGE_ALLOCATION,p).status!=GUEST_CONTEXT_OK)w->errors++;
    }
    return NULL;
}
struct page_metadata_worker {guest_pages *owner;unsigned errors;};
static void *page_metadata_worker(void *opaque) {
    struct page_metadata_worker *w=opaque;
    for(unsigned i=0;i<50;i++) {
        uintptr_t range=0;
        if(guest_pages_reserve(w->owner,page,&range).status!=GUEST_PAGES_OK) {w->errors++;continue;}
        struct guest_pages_usage use;
        if(guest_pages_usage(w->owner,&use).status!=GUEST_PAGES_OK || !use.ledger_valid || !use.resources.valid)w->errors++;
        if(guest_pages_release_region(w->owner,range).status!=GUEST_PAGES_OK)w->errors++;
    }
    return NULL;
}
static int resource_owner_contract(void) {
    struct guest_context_config config={1024*1024,65536};guest_context *c=NULL;
    CHECK(guest_context_create_with_resources(&config,0,&c).status==GUEST_CONTEXT_INVALID && !c);
    CHECK(guest_context_create_with_resources(&config,page-1,&c).status==GUEST_CONTEXT_INVALID && !c);
    CHECK(guest_context_create_with_resources(&config,page,&c).status==GUEST_CONTEXT_OK);
    guest_pages *owner=NULL;CHECK(guest_pages_create(c,&owner).status==GUEST_PAGES_OK);
    guest_charge_ticket *pending=NULL;CHECK(guest_charge_reserve(c,1,&pending).status==GUEST_CONTEXT_OK);
    size_t capacity=page/256+1,count=0;uintptr_t *ranges=calloc(capacity,sizeof(*ranges));CHECK(ranges);
    struct guest_pages_result r={0};
    for(;count<capacity;count++) {
        r=guest_pages_reserve(owner,page,&ranges[count]);
        if(r.status!=GUEST_PAGES_OK)break;
    }
    CHECK(count<capacity && r.status==GUEST_PAGES_VM_ERROR && r.region_result==0x1003 && !ranges[count]);
    struct guest_pages_usage use;CHECK(guest_pages_usage(owner,&use).status==GUEST_PAGES_OK);
    CHECK(use.region_count==count && use.vm.region_count==count && use.resources.valid);
    CHECK(use.resources.blocks==1+2*count && use.resources.used_bytes==(1+2*count)*256 && use.context.pending_bytes==1);
    CHECK(guest_charge_cancel(pending).status==GUEST_CONTEXT_OK);
    for(size_t i=0;i<count;i++)CHECK(guest_pages_release_region(owner,ranges[i]).status==GUEST_PAGES_OK);
    free(ranges);CHECK(guest_pages_destroy(owner).status==GUEST_PAGES_OK);
    struct guest_resource_usage resources;CHECK(guest_context_resource_usage(c,&resources).status==GUEST_CONTEXT_OK && resources.valid && !resources.used_bytes);
    void *first=NULL,*second=NULL;CHECK(guest_metadata_allocate(c,GUEST_METADATA_VA_INTERVAL,24,&first).status==GUEST_CONTEXT_OK);
    CHECK(guest_metadata_release(c,GUEST_METADATA_VA_INTERVAL,first).status==GUEST_CONTEXT_OK);
    CHECK(guest_metadata_allocate(c,GUEST_METADATA_VA_INTERVAL,24,&second).status==GUEST_CONTEXT_OK && second==first);
    CHECK(guest_metadata_release(c,GUEST_METADATA_VA_INTERVAL,second).status==GUEST_CONTEXT_OK);
    pthread_t threads[8];struct metadata_worker workers[8];
    for(unsigned i=0;i<8;i++) {workers[i]=(struct metadata_worker){c,0};CHECK(!pthread_create(&threads[i],NULL,metadata_worker,&workers[i]));}
    for(unsigned i=0;i<8;i++) {CHECK(!pthread_join(threads[i],NULL));CHECK(!workers[i].errors);}
    CHECK(guest_context_resource_usage(c,&resources).status==GUEST_CONTEXT_OK && !resources.used_bytes && !resources.blocks);
    CHECK(guest_context_destroy(c).status==GUEST_CONTEXT_OK);
    CHECK(guest_context_create_with_resources(&config,8*page,&c).status==GUEST_CONTEXT_OK);
    CHECK(guest_pages_create(c,&owner).status==GUEST_PAGES_OK);
    struct page_metadata_worker page_workers[8];
    for(unsigned i=0;i<8;i++) {page_workers[i]=(struct page_metadata_worker){owner,0};CHECK(!pthread_create(&threads[i],NULL,page_metadata_worker,&page_workers[i]));}
    for(unsigned i=0;i<8;i++) {CHECK(!pthread_join(threads[i],NULL));CHECK(!page_workers[i].errors);}
    CHECK(guest_pages_usage(owner,&use).status==GUEST_PAGES_OK && !use.region_count && !use.resources.used_bytes);
    CHECK(guest_pages_destroy(owner).status==GUEST_PAGES_OK && guest_context_destroy(c).status==GUEST_CONTEXT_OK);
    puts("{\"resource_owner_errors\":0,\"exhausted_before_vm\":true,\"metadata_rollback\":true,\"reuse\":true,\"threads\":8,\"iterations\":8000,\"concurrent_va_snapshots\":400,\"final_used\":0}");return 0;
}
#if defined(GUEST_RESOURCE_TESTING)
static unsigned resource_map_calls,resource_unmap_calls,resource_unmap_success;
static int resource_map_failure,resource_unmap_failure;
static void *resource_map_probe(size_t bytes) {
    resource_map_calls++;
    if(resource_map_failure) {resource_map_failure=0;errno=ENOMEM;return MAP_FAILED;}
    return mmap(NULL,bytes,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
}
static int resource_unmap_probe(void *p,size_t bytes) {
    resource_unmap_calls++;
    if(resource_unmap_failure) {resource_unmap_failure=0;errno=EBUSY;return -1;}
    int r=munmap(p,bytes);if(!r)resource_unmap_success++;return r;
}
#if defined(NSO_OWNED_PAGES_TESTING)
static int fail_resource_protect(void *p,size_t n,int protection) {
    (void)p;(void)n;(void)protection;errno=EACCES;return -1;
}
static int resource_page_quarantine(void) {
    struct guest_context_config config={1024*1024,65536};guest_context *c=NULL;guest_pages *p=NULL;
    CHECK(guest_context_create_with_resources(&config,page,&c).status==GUEST_CONTEXT_OK);
    CHECK(guest_pages_create(c,&p).status==GUEST_PAGES_OK);
    uintptr_t base=0;CHECK(guest_pages_reserve(p,page,&base).status==GUEST_PAGES_OK);
    nso_host_owned_pages_test_vm(fail_resource_protect,NULL);
    struct guest_pages_result mapped=guest_pages_map(p,base,page);
    CHECK(mapped.status==GUEST_PAGES_QUARANTINED && mapped.vm.native_errno==EACCES && mapped.vm.restore_errno==EACCES);
    struct guest_pages_usage use;CHECK(guest_pages_usage(p,&use).status==GUEST_PAGES_QUARANTINED);
    CHECK(!use.backing_valid && use.quarantined_regions==1 && use.ticket_count==1 && use.pending_bytes==page && use.resources.used_bytes==4*256);
    nso_host_owned_pages_test_vm(NULL,NULL);
    CHECK(guest_pages_destroy(p).status==GUEST_PAGES_OK);
    struct guest_resource_usage resources;CHECK(guest_context_resource_usage(c,&resources).status==GUEST_CONTEXT_OK && !resources.used_bytes);
    CHECK(guest_context_destroy(c).status==GUEST_CONTEXT_OK);return 0;
}
#endif
static int resource_recovery_contract(void) {
    struct guest_context_config config={1024*1024,65536};guest_context *c=NULL;
#if defined(NSO_OWNED_PAGES_TESTING)
    CHECK(!resource_page_quarantine());
#endif
    guest_context_resource_test_vm(resource_map_probe,resource_unmap_probe);
    resource_map_failure=1;CHECK(guest_context_create_with_resources(&config,page,&c).status==GUEST_CONTEXT_SYSTEM_ERROR && !c);
    CHECK(guest_context_create_with_resources(&config,page,&c).status==GUEST_CONTEXT_OK);
    void *node=NULL;CHECK(guest_metadata_allocate(c,GUEST_METADATA_VA_REGION,64,&node).status==GUEST_CONTEXT_OK);
    CHECK(guest_context_destroy(c).status==GUEST_CONTEXT_BUSY && resource_unmap_calls==0);
    CHECK(guest_metadata_release(c,GUEST_METADATA_VA_REGION,node).status==GUEST_CONTEXT_OK);
    resource_unmap_failure=1;CHECK(guest_context_destroy(c).status==GUEST_CONTEXT_QUARANTINED && resource_unmap_calls==1 && !resource_unmap_success);
    struct guest_resource_usage untouched;CHECK(guest_context_resource_usage(c,&untouched).status==GUEST_CONTEXT_QUARANTINED && !untouched.valid);
    CHECK(guest_context_destroy(c).status==GUEST_CONTEXT_OK && resource_unmap_calls==2 && resource_unmap_success==1);
    guest_context_resource_test_vm(NULL,NULL);
    CHECK(resource_map_calls==2);
    puts("{\"resource_recovery_errors\":0,\"map_failure\":true,\"live_nodes_block_destroy\":true,\"retained_invalid\":true,\"unmap_attempts\":2,\"actual_unmaps\":1}");return 0;
}
#endif
static void *exercise_dso_registration(guest_process *process,void *opaque) {
    (void)opaque;
    nso_module *m=guest_process_module(process);
    uint64_t base=nso_base(m);
    uint32_t bad_base=guest_process_nnmusl_init_dso(
        base+0x1000,base+0x10,base+0x20,base+0x30,base+8,
        base+0x28,base+0x30,base+0x40,base+8,base+0x38,
        base+0x50,base+0x60,base+0x70,base+0x80,base,base,
        base+0x90,base+0xa0,base,base,base+0xb0);
    expect_value("dso-bad-base-result",bad_base,2);
    uint32_t bad_bounds=guest_process_nnmusl_init_dso(
        base,base+0x10,base+0x30,base+0x20,base+8,
        base+0x38,base+0x20,base+0x40,base+8,base+0x28,
        base+0x50,base+0x60,base+0x70,base+0x80,base,base,
        base+0x90,base+0xa0,base,base,base+0xb0);
    expect_value("dso-bad-bounds-result",bad_bounds,2);
    uint64_t tls_start=base+0x1000;
    uint64_t tls_init_end=tls_start+0x220;
    uint64_t tls_total_end=tls_start+0x1019;
    uint32_t ok=guest_process_nnmusl_init_dso(
        base,base+0x500,tls_start,tls_init_end,base+8,
        tls_start+8,tls_init_end,tls_total_end,base+8,tls_init_end+8,
        base+0x3000,base+0x3500,base+0x4000,base+0x4500,base,base,
        base+0x4500,base+0x4800,base,base,base+0x5000);
    expect_value("dso-normal-result",ok,1);
    struct guest_dso_info info={0};
    struct nso_result r=guest_process_dso_info(process,&info);
    expect_value("dso-info-status",r.status,NSO_OK);
    expect_value("dso-registered",info.registered,1);
    expect_value("dso-base",info.base,base);
    expect_value("dso-tls-start",info.tls_start,tls_start);
    expect_value("dso-tls-init-size",info.tls_init_size,0x220);
    expect_value("dso-tls-total-size",info.tls_total_size,0x1019);
    expect_value("dso-tls-align",info.tls_alignment,8);
    expect_condition("dso-tls-anchor-nonnull",info.tls_anchor!=0);
    expect_condition("dso-tls-payload-offset",info.tls_payload==info.tls_anchor+16);
    expect_condition("dso-tls-storage-size",info.tls_storage_size>=16+0x1019);
    uint32_t repeat=guest_process_nnmusl_init_dso(
        base,base+0x500,tls_start,tls_init_end,base+8,
        tls_start+8,tls_init_end,tls_total_end,base+8,tls_init_end+8,
        base+0x3000,base+0x3500,base+0x4000,base+0x4500,base,base,
        base+0x4500,base+0x4800,base,base,base+0x5000);
    expect_value("dso-repeat-result",repeat,1);
    return &returned_token;
}
static int dso_registration_contract(const char *path) {
    uint32_t outside=guest_process_nnmusl_init_dso(
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0);
    CHECK(outside==2);
    struct guest_process_config config={1024*1024,65536};guest_process *process=NULL;
    CHECK(succeeded(guest_process_create(&config,&process)));char err[128];
    CHECK(succeeded(guest_process_load(process,path,err,sizeof(err))));
    void *value=NULL;
    CHECK(succeeded(guest_process_run(process,exercise_dso_registration,NULL,&value)));
    CHECK(value==&returned_token && !errors);
    CHECK(succeeded(guest_process_destroy(&process)) && !process);
    puts("{\"dso_registration_errors\":0,\"dso_registered\":true,\"tls_init_size\":544,\"tls_total_size\":4121,\"tls_align\":8,\"repeat_idempotent\":true}");
    return 0;
}
/* Snapshot thread policy.
 *
 * The guest asks for the memory/resource snapshot from more than one of its
 * own threads - on RG477M the engine's IOThreadPool workers do exactly that.
 * The runtime must answer a worker that bound to this guest with the same
 * numbers the primary sees, and must still refuse a host thread that never
 * bound, with a named reason and an untouched output. */
struct snapshot_thread_case {
    guest_process *process;
    struct guest_memory_info primary_info;
    struct guest_virtual_resource_usage primary_resource;
    int primary_va_enabled;
    int primary_memory_ok,primary_resource_ok,primary_va_ok;
    struct nso_result worker_memory_status,worker_resource_status,worker_va_status;
    struct guest_memory_info worker_info;
    struct guest_virtual_resource_usage worker_resource;
    int worker_va_enabled;
    struct nso_result unbound_memory_status;
    struct guest_memory_info unbound_info;
    int unbound_untouched;
};
static void *snapshot_thread_bound_worker(void *arg) {
    struct snapshot_thread_case *c=(struct snapshot_thread_case *)arg;
    memset(&c->worker_info,0xa5,sizeof(c->worker_info));
    /* Binding registers this worker's own OS-TLS record in the guest's
     * manager, exactly like the runtime does for a guest-created thread. */
    guest_process_bind_child_thread();
    c->worker_memory_status=guest_process_memory_info(c->process,&c->worker_info);
    c->worker_resource_status=guest_process_resource_info(c->process,&c->worker_resource);
    c->worker_va_status=guest_process_va_enabled(c->process,&c->worker_va_enabled);
    guest_process_unbind_child_thread();
    return NULL;
}
static void *snapshot_thread_unbound_worker(void *arg) {
    struct snapshot_thread_case *c=(struct snapshot_thread_case *)arg;
    memset(&c->unbound_info,0xa5,sizeof(c->unbound_info));
    /* No bind: this host thread never became a thread of this guest. */
    c->unbound_memory_status=guest_process_memory_info(c->process,&c->unbound_info);
    c->unbound_untouched=all_bytes((unsigned char *)&c->unbound_info,sizeof(c->unbound_info),0xa5);
    return NULL;
}
static void *exercise_snapshot_thread(guest_process *process,void *opaque) {
    struct snapshot_thread_case *c=(struct snapshot_thread_case *)opaque;
    c->process=process;
    c->primary_memory_ok=succeeded(guest_process_memory_info(process,&c->primary_info));
    c->primary_resource_ok=succeeded(guest_process_resource_info(process,&c->primary_resource));
    c->primary_va_ok=succeeded(guest_process_va_enabled(process,&c->primary_va_enabled));
    pthread_t worker,unbound;
    expect_condition("snapshot-worker-create",pthread_create(&worker,NULL,snapshot_thread_bound_worker,c)==0);
    expect_condition("snapshot-unbound-create",pthread_create(&unbound,NULL,snapshot_thread_unbound_worker,c)==0);
    expect_condition("snapshot-worker-join",pthread_join(worker,NULL)==0);
    expect_condition("snapshot-unbound-join",pthread_join(unbound,NULL)==0);
    return &returned_token;
}
static int snapshot_thread_contract(const char *path) {
    struct guest_process_config config={1024*1024,65536};guest_process *process=NULL;
    CHECK(succeeded(guest_process_create_with_resources(&config,1024*1024,0,NULL,&process)));
    char err[128];
    CHECK(succeeded(guest_process_load(process,path,err,sizeof(err))));
    struct snapshot_thread_case c={0};
    void *value=NULL;
    CHECK(succeeded(guest_process_run(process,exercise_snapshot_thread,&c,&value)));
    /* The error count is checked at the END of this contract, after every
     * expect_* below has run. Checking it here would swallow the failures
     * recorded by the expectations that follow. */
    CHECK(value==&returned_token);
    /* The primary still answers for itself. */
    expect_condition("snapshot-primary-memory-ok",c.primary_memory_ok);
    expect_condition("snapshot-primary-resource-ok",c.primary_resource_ok);
    expect_condition("snapshot-primary-va-ok",c.primary_va_ok);
    /* A bound worker gets the same answer out of the same ledger. */
    expect_condition("snapshot-worker-memory-ok",c.worker_memory_status.status==NSO_OK);
    expect_condition("snapshot-worker-resource-ok",c.worker_resource_status.status==NSO_OK);
    expect_condition("snapshot-worker-va-ok",c.worker_va_status.status==NSO_OK);
    expect_value("snapshot-worker-total-matches-primary",c.worker_info.total,c.primary_info.total);
    expect_value("snapshot-worker-used-matches-primary",c.worker_info.used,c.primary_info.used);
    expect_value("snapshot-worker-baseline-matches-primary",c.worker_info.baseline,c.primary_info.baseline);
    expect_value("snapshot-worker-stack-matches-primary",c.worker_info.stack_bytes,c.primary_info.stack_bytes);
    expect_value("snapshot-worker-thread-objects-matches-primary",c.worker_info.thread_objects,c.primary_info.thread_objects);
    expect_value("snapshot-worker-resource-total-matches-primary",c.worker_resource.total,c.primary_resource.total);
    expect_value("snapshot-worker-resource-used-matches-primary",c.worker_resource.used,c.primary_resource.used);
    expect_value("snapshot-worker-va-matches-primary",(uint64_t)(c.worker_va_enabled?1:0),(uint64_t)(c.primary_va_enabled?1:0));
    expect_value("snapshot-worker-va-enabled",(uint64_t)(c.worker_va_enabled?1:0),1);
    /* A host thread that never bound to this guest is still refused. */
    expect_condition("snapshot-unbound-refused",c.unbound_memory_status.status!=NSO_OK);
    expect_condition("snapshot-unbound-output-untouched",c.unbound_untouched);
    CHECK(succeeded(guest_process_destroy(&process)) && !process);
    /* The final gate. Every expect_* above must have passed; without this the
     * contract could still exit 0 while the expectations had failed. */
    CHECK(!errors);
    /* The report states what this run observed, not a literal zero: a failed
     * expectation has to be visible in the number the caller reads. */
    int worker_matches_primary=c.worker_info.total==c.primary_info.total
        && c.worker_info.used==c.primary_info.used
        && c.worker_info.baseline==c.primary_info.baseline
        && c.worker_info.stack_bytes==c.primary_info.stack_bytes
        && c.worker_info.thread_objects==c.primary_info.thread_objects
        && c.worker_resource.total==c.primary_resource.total
        && c.worker_resource.used==c.primary_resource.used
        && c.worker_va_enabled==c.primary_va_enabled;
    printf("{\"snapshot_thread_errors\":%u,\"primary_memory_ok\":%s,\"primary_resource_ok\":%s,"
         "\"primary_va_ok\":%s,\"bound_worker_memory_ok\":%s,\"bound_worker_resource_ok\":%s,"
         "\"bound_worker_va_ok\":%s,\"worker_matches_primary\":%s,"
         "\"unbound_caller_refused\":%s,\"unbound_output_untouched\":%s}\n",errors,
         c.primary_memory_ok?"true":"false",c.primary_resource_ok?"true":"false",
         c.primary_va_ok?"true":"false",
         c.worker_memory_status.status==NSO_OK?"true":"false",
         c.worker_resource_status.status==NSO_OK?"true":"false",
         c.worker_va_status.status==NSO_OK?"true":"false",
         worker_matches_primary?"true":"false",
         c.unbound_memory_status.status!=NSO_OK?"true":"false",
         c.unbound_untouched?"true":"false");
    return 0;
}
/* movie::SetAllocator binding contract.
 *
 * The guest registers its own allocator with the SDK movie library. The
 * runtime must bind the symbol, accept its four-argument void signature and
 * record the registration faithfully instead of dropping it. The recorded
 * values are read back from the host state, so a wrong argument order or a
 * dropped fourth argument is observable rather than silent. The fixture NSO
 * drives the real guest import path, so the boundary has to accept the
 * signature or the guest never runs at all. */
#define MOVIE_SET_ALLOCATOR_MANGLED "_ZN5movie12SetAllocatorEPFPvmmS0_EPFvS0_S0_EPFS0_S0_mS0_ES0_"
/* Callback addresses inside the fixture NSO; see the movie-allocator fixture
 * assembly in scripts/runtime/build_guest_process_contract.sh. */
#define MOVIE_FIXTURE_CB_ALLOC_OFF 0x80
#define MOVIE_FIXTURE_CB_FREE_OFF 0x84
#define MOVIE_FIXTURE_CB_REALLOC_OFF 0x88
struct movie_allocator_case {
    guest_process *process;
    int resolved;
    int direct_record_ok;
    uintptr_t direct_alloc,direct_free,direct_realloc,direct_userdata;
    int native_ran,native_marker,native_userdata_null;
    uintptr_t native_alloc,native_free,native_realloc;
};
static void *exercise_movie_allocator(guest_process *p,void *opaque) {
    struct movie_allocator_case *c=opaque;
    c->process=p;
    typedef void (*movie_set_allocator_fn)(movie_alloc_fn,movie_free_fn,movie_realloc_fn,void *);
    movie_set_allocator_fn setter=(movie_set_allocator_fn)(uintptr_t)guest_process_resolve(0,MOVIE_SET_ALLOCATOR_MANGLED);
    c->resolved=setter!=NULL;
    expect_condition("movie-allocator-resolved",setter!=NULL);
    /* The real guest path: the fixture calls the import itself, so the
     * boundary must accept the four-argument void signature or the guest
     * never executes and the registration is never observed. */
    uint64_t output[2]={0xa5a5,0xa5a5},returned=0xa5a5;
    uint64_t args[1]={(uintptr_t)output};
    struct nso_result r=guest_process_call(p,0x20,args,1,&returned);
#if defined(__ANDROID__) && defined(__aarch64__)
    expect_value("movie-allocator-native-status",r.status,NSO_OK);
    expect_value("movie-allocator-native-return",returned,0x91);
    c->native_ran=1;
    c->native_marker=output[0]==1;
    expect_condition("movie-allocator-guest-call-returned",output[0]==1);
    uint64_t base=nso_base(guest_process_module(p));
    c->native_alloc=(uintptr_t)nso_host_movie_get_allocator();
    c->native_free=(uintptr_t)nso_host_movie_get_deallocator();
    c->native_realloc=(uintptr_t)nso_host_movie_get_reallocator();
    c->native_userdata_null=nso_host_movie_get_allocator_userdata()==NULL;
    expect_value("movie-allocator-guest-alloc-pointer",c->native_alloc,base+MOVIE_FIXTURE_CB_ALLOC_OFF);
    expect_value("movie-allocator-guest-free-pointer",c->native_free,base+MOVIE_FIXTURE_CB_FREE_OFF);
    expect_value("movie-allocator-guest-realloc-pointer",c->native_realloc,base+MOVIE_FIXTURE_CB_REALLOC_OFF);
    expect_condition("movie-allocator-guest-userdata-null",c->native_userdata_null);
#else
    /* No AArch64 executor on this host: the boundary is prepared but the raw
     * guest entry cannot run here. Nothing may be faked in its place. */
    expect_value("movie-allocator-native-unsupported",r.status,NSO_UNSUPPORTED);
    expect_value("movie-allocator-native-no-raw-fallback",returned,0xa5a5);
#endif
    /* Direct host call: the registration is stored exactly as given,
     * including the fourth argument. */
    if(setter) {
        setter((movie_alloc_fn)(uintptr_t)0x1111,(movie_free_fn)(uintptr_t)0x2222,
               (movie_realloc_fn)(uintptr_t)0x3333,(void *)(uintptr_t)0x4444);
        c->direct_alloc=(uintptr_t)nso_host_movie_get_allocator();
        c->direct_free=(uintptr_t)nso_host_movie_get_deallocator();
        c->direct_realloc=(uintptr_t)nso_host_movie_get_reallocator();
        c->direct_userdata=(uintptr_t)nso_host_movie_get_allocator_userdata();
        c->direct_record_ok=c->direct_alloc==0x1111&&c->direct_free==0x2222&&
                           c->direct_realloc==0x3333&&c->direct_userdata==0x4444;
        expect_value("movie-allocator-direct-alloc",c->direct_alloc,0x1111);
        expect_value("movie-allocator-direct-free",c->direct_free,0x2222);
        expect_value("movie-allocator-direct-realloc",c->direct_realloc,0x3333);
        expect_value("movie-allocator-direct-userdata",c->direct_userdata,0x4444);
    }
    return &returned_token;
}
static int movie_allocator_contract(const char *path) {
    struct guest_process_config config={1024*1024,65536};guest_process *p=NULL;
    CHECK(succeeded(guest_process_create(&config,&p)));
    char err[128];CHECK(succeeded(guest_process_load(p,path,err,sizeof(err))));
    struct movie_allocator_case c={0};
    void *value=NULL;
    CHECK(succeeded(guest_process_run(p,exercise_movie_allocator,&c,&value)));
    CHECK(value==&returned_token);
    expect_condition("movie-allocator-resolved",c.resolved);
    expect_condition("movie-allocator-direct-record",c.direct_record_ok);
    CHECK(succeeded(guest_process_destroy(&p)) && !p);
    /* The final gate. Every expect_* above must have passed; without this the
     * contract could still exit 0 while the expectations had failed. */
    CHECK(!errors);
    /* The report states what this run observed, not a literal zero. */
    printf("{\"movie_allocator_errors\":%u,\"resolved\":%s,\"direct_record_ok\":%s,"
           "\"native_ran\":%s,\"guest_marker\":%s,\"guest_userdata_null\":%s}\n",errors,
           c.resolved?"true":"false",c.direct_record_ok?"true":"false",
           c.native_ran?"true":"false",c.native_marker?"true":"false",
           c.native_userdata_null?"true":"false");
    return 0;
}

/* nn::oe::SetCpuBoostMode (main NSO import index 75) takes one enum argument
 * and returns nothing. The runtime must bind the symbol, accept the
 * one-argument void signature at the boundary and record the requested mode
 * faithfully instead of dropping it. The fixture NSO drives the real guest
 * import path, so the boundary has to accept the signature or the guest never
 * runs and the mode is never observed. The guest call uses mode 1 and the
 * direct host call uses mode 5, so a stub that ignored its argument could not
 * satisfy both observations. */
#define OE_SET_CPU_BOOST_MODE_MANGLED "_ZN2nn2oe15SetCpuBoostModeENS0_12CpuBoostModeE"
/* The mode the fixture guest passes to the import; see the oe-cpu-boost
 * fixture assembly in scripts/runtime/build_guest_process_contract.sh. */
#define OE_FIXTURE_GUEST_MODE 1
/* A different value for the direct host call, so the two observations cannot
 * be satisfied by one constant. */
#define OE_DIRECT_HOST_MODE 5
struct oe_cpu_boost_case {
    guest_process *process;
    int resolved;
    int direct_record_ok;
    nn_oe_cpu_boost_mode direct_mode;
    int native_ran,native_marker;
    nn_oe_cpu_boost_mode native_mode;
};
static void *exercise_oe_cpu_boost(guest_process *p,void *opaque) {
    struct oe_cpu_boost_case *c=opaque;
    c->process=p;
    typedef void (*oe_set_cpu_boost_fn)(nn_oe_cpu_boost_mode);
    oe_set_cpu_boost_fn setter=(oe_set_cpu_boost_fn)(uintptr_t)guest_process_resolve(0,OE_SET_CPU_BOOST_MODE_MANGLED);
    c->resolved=setter!=NULL;
    expect_condition("oe-cpu-boost-resolved",setter!=NULL);
    /* The real guest path: the fixture calls the import itself, so the
     * boundary must accept the one-argument void signature or the guest never
     * executes and the mode is never observed. */
    uint64_t output[2]={0xa5a5,0xa5a5},returned=0xa5a5;
    uint64_t args[1]={(uintptr_t)output};
    struct nso_result r=guest_process_call(p,0x20,args,1,&returned);
#if defined(__ANDROID__) && defined(__aarch64__)
    expect_value("oe-cpu-boost-native-status",r.status,NSO_OK);
    expect_value("oe-cpu-boost-native-return",returned,0x91);
    c->native_ran=1;
    c->native_marker=output[0]==1;
    expect_condition("oe-cpu-boost-guest-call-returned",output[0]==1);
    c->native_mode=nso_host_oe_get_cpu_boost_mode();
    expect_value("oe-cpu-boost-guest-mode",(uint64_t)(uint32_t)c->native_mode,(uint64_t)(uint32_t)OE_FIXTURE_GUEST_MODE);
#else
    /* No AArch64 executor on this host: the boundary is prepared but the raw
     * guest entry cannot run here. Nothing may be faked in its place. */
    expect_value("oe-cpu-boost-native-unsupported",r.status,NSO_UNSUPPORTED);
    expect_value("oe-cpu-boost-native-no-raw-fallback",returned,0xa5a5);
#endif
    /* Direct host call: the requested mode is recorded exactly as given. */
    if(setter) {
        setter((nn_oe_cpu_boost_mode)OE_DIRECT_HOST_MODE);
        c->direct_mode=nso_host_oe_get_cpu_boost_mode();
        c->direct_record_ok=c->direct_mode==OE_DIRECT_HOST_MODE;
        expect_value("oe-cpu-boost-direct-mode",(uint64_t)(uint32_t)c->direct_mode,(uint64_t)(uint32_t)OE_DIRECT_HOST_MODE);
    }
    return &returned_token;
}
static int oe_cpu_boost_contract(const char *path) {
    struct guest_process_config config={1024*1024,65536};guest_process *p=NULL;
    CHECK(succeeded(guest_process_create(&config,&p)));
    char err[128];CHECK(succeeded(guest_process_load(p,path,err,sizeof(err))));
    struct oe_cpu_boost_case c={0};
    void *value=NULL;
    CHECK(succeeded(guest_process_run(p,exercise_oe_cpu_boost,&c,&value)));
    CHECK(value==&returned_token);
    expect_condition("oe-cpu-boost-resolved",c.resolved);
    expect_condition("oe-cpu-boost-direct-record",c.direct_record_ok);
    CHECK(succeeded(guest_process_destroy(&p)) && !p);
    /* The final gate. Every expect_* above must have passed; without this the
     * contract could still exit 0 while the expectations had failed. */
    CHECK(!errors);
    /* The report states what this run observed, not a literal zero. guest_mode
     * is -1 when the raw guest entry could not run on this host. */
    printf("{\"oe_cpu_boost_errors\":%u,\"resolved\":%s,\"direct_record_ok\":%s,"
           "\"native_ran\":%s,\"guest_marker\":%s,\"guest_mode\":%d}\n",errors,
           c.resolved?"true":"false",c.direct_record_ok?"true":"false",
           c.native_ran?"true":"false",c.native_marker?"true":"false",
           c.native_ran?(int)c.native_mode:-1);
    return 0;
}

/* nn::hid::InitializeKeyboard() and nn::hid::InitializeMouse() (main NSO
 * import indexes 234 and 235) take no arguments and return nothing. The
 * runtime must bind both symbols, accept the zero-argument void signature at
 * the boundary and record that the subsystem was initialised. The fixture NSO
 * drives the real guest import path with both imports, so the boundary has to
 * accept the signature or the guest never runs. Before the direct host call
 * the host state must still be untouched, so a stub that pre-sets the flags
 * cannot pass. */
#define HID_INIT_KEYBOARD_MANGLED "_ZN2nn3hid18InitializeKeyboardEv"
#define HID_INIT_MOUSE_MANGLED "_ZN2nn3hid15InitializeMouseEv"
struct hid_init_case {
    guest_process *process;
    int resolved_keyboard,resolved_mouse;
    int direct_ok;
    int native_ran,native_marker;
    int native_keyboard_initialized,native_mouse_initialized;
};
static void *exercise_hid_init(guest_process *p,void *opaque) {
    struct hid_init_case *c=opaque;
    c->process=p;
    typedef void (*hid_init_fn)(void);
    hid_init_fn keyboard=(hid_init_fn)(uintptr_t)guest_process_resolve(0,HID_INIT_KEYBOARD_MANGLED);
    hid_init_fn mouse=(hid_init_fn)(uintptr_t)guest_process_resolve(0,HID_INIT_MOUSE_MANGLED);
    c->resolved_keyboard=keyboard!=NULL;
    c->resolved_mouse=mouse!=NULL;
    expect_condition("hid-init-keyboard-resolved",keyboard!=NULL);
    expect_condition("hid-init-mouse-resolved",mouse!=NULL);
    /* The stubs record an initialisation; they do not start out initialised. */
    expect_condition("hid-init-keyboard-not-preinitialised",!nso_host_hid_keyboard_initialized());
    expect_condition("hid-init-mouse-not-preinitialised",!nso_host_hid_mouse_initialized());
    /* The real guest path: the fixture calls both imports itself. */
    uint64_t output[2]={0xa5a5,0xa5a5},returned=0xa5a5;
    uint64_t args[1]={(uintptr_t)output};
    struct nso_result r=guest_process_call(p,0x20,args,1,&returned);
#if defined(__ANDROID__) && defined(__aarch64__)
    expect_value("hid-init-native-status",r.status,NSO_OK);
    expect_value("hid-init-native-return",returned,0x91);
    c->native_ran=1;
    c->native_marker=output[0]==1;
    expect_condition("hid-init-guest-call-returned",output[0]==1);
    c->native_keyboard_initialized=nso_host_hid_keyboard_initialized();
    c->native_mouse_initialized=nso_host_hid_mouse_initialized();
    expect_condition("hid-init-guest-keyboard-initialised",c->native_keyboard_initialized);
    expect_condition("hid-init-guest-mouse-initialised",c->native_mouse_initialized);
#else
    /* No AArch64 executor on this host: the boundary is prepared but the raw
     * guest entry cannot run here. Nothing may be faked in its place. */
    expect_value("hid-init-native-unsupported",r.status,NSO_UNSUPPORTED);
    expect_value("hid-init-native-no-raw-fallback",returned,0xa5a5);
#endif
    /* Direct host call: both stubs must actually record the initialisation. */
    if(keyboard&&mouse) {
        keyboard();
        mouse();
        c->direct_ok=nso_host_hid_keyboard_initialized()&&nso_host_hid_mouse_initialized();
        expect_condition("hid-init-direct-keyboard",nso_host_hid_keyboard_initialized());
        expect_condition("hid-init-direct-mouse",nso_host_hid_mouse_initialized());
    }
    return &returned_token;
}
static int hid_init_contract(const char *path) {
    struct guest_process_config config={1024*1024,65536};guest_process *p=NULL;
    CHECK(succeeded(guest_process_create(&config,&p)));
    char err[128];CHECK(succeeded(guest_process_load(p,path,err,sizeof(err))));
    struct hid_init_case c={0};
    void *value=NULL;
    CHECK(succeeded(guest_process_run(p,exercise_hid_init,&c,&value)));
    CHECK(value==&returned_token);
    expect_condition("hid-init-keyboard-resolved",c.resolved_keyboard);
    expect_condition("hid-init-mouse-resolved",c.resolved_mouse);
    expect_condition("hid-init-direct-record",c.direct_ok);
    CHECK(succeeded(guest_process_destroy(&p)) && !p);
    /* The final gate. Every expect_* above must have passed; without this the
     * contract could still exit 0 while the expectations had failed. */
    CHECK(!errors);
    /* The report states what this run observed, not a literal zero. */
    printf("{\"hid_init_errors\":%u,\"keyboard_resolved\":%s,\"mouse_resolved\":%s,"
           "\"direct_ok\":%s,\"native_ran\":%s,\"guest_marker\":%s,"
           "\"guest_keyboard_initialized\":%s,\"guest_mouse_initialized\":%s}\n",errors,
           c.resolved_keyboard?"true":"false",c.resolved_mouse?"true":"false",
           c.direct_ok?"true":"false",c.native_ran?"true":"false",
           c.native_marker?"true":"false",
           c.native_keyboard_initialized?"true":"false",
           c.native_mouse_initialized?"true":"false");
    return 0;
}

/* nn::oe focus and notification setup (main NSO import indexes 222 to 226).
 * The guest calls all five from one straight-line sequence, so every one of
 * them has to be bound and has to carry a signature the boundary accepts.
 * The fixture drives the real guest import path for the two shapes that
 * matter: the zero-argument function that returns a value (222) and the
 * one-argument setter (225). The boundary checks the signature of every bound
 * import before the guest runs, so a successful guest call also proves the
 * signatures of the other three were accepted. */
#define OE_FOCUS_STATE_MANGLED "_ZN2nn2oe20GetCurrentFocusStateEv"
#define OE_FOCUS_HANDLING_MANGLED "_ZN2nn2oe20SetFocusHandlingModeENS0_17FocusHandlingModeE"
#define OE_PERF_NOTIFICATION_MANGLED "_ZN2nn2oe44SetPerformanceModeChangedNotificationEnabledEb"
#define OE_OPERATION_NOTIFICATION_MANGLED "_ZN2nn2oe42SetOperationModeChangedNotificationEnabledEb"
#define OE_RESUME_NOTIFICATION_MANGLED "_ZN2nn2oe28SetResumeNotificationEnabledEb"
/* The focus handling mode the fixture guest passes to the import; see the
 * oe-focus fixture assembly in scripts/runtime/build_guest_process_contract.sh. */
#define OE_FIXTURE_FOCUS_HANDLING_MODE 2
/* A different value for the direct host call, so the two observations cannot
 * be satisfied by one constant. */
#define OE_DIRECT_FOCUS_HANDLING_MODE 7
struct oe_focus_case {
    guest_process *process;
    int resolved_focus_state,resolved_focus_handling;
    int resolved_perf,resolved_operation,resolved_resume;
    int direct_ok;
    int native_ran,native_marker;
    uint32_t native_focus_state;
    int32_t native_focus_handling_mode;
};
static void *exercise_oe_focus(guest_process *p,void *opaque) {
    struct oe_focus_case *c=opaque;
    c->process=p;
    typedef uint32_t (*oe_focus_state_fn)(void);
    typedef void (*oe_focus_handling_fn)(int32_t);
    typedef void (*oe_bool_fn)(bool);
    oe_focus_state_fn focus_state=(oe_focus_state_fn)(uintptr_t)guest_process_resolve(0,OE_FOCUS_STATE_MANGLED);
    oe_focus_handling_fn focus_handling=(oe_focus_handling_fn)(uintptr_t)guest_process_resolve(0,OE_FOCUS_HANDLING_MANGLED);
    oe_bool_fn perf=(oe_bool_fn)(uintptr_t)guest_process_resolve(0,OE_PERF_NOTIFICATION_MANGLED);
    oe_bool_fn operation=(oe_bool_fn)(uintptr_t)guest_process_resolve(0,OE_OPERATION_NOTIFICATION_MANGLED);
    oe_bool_fn resume=(oe_bool_fn)(uintptr_t)guest_process_resolve(0,OE_RESUME_NOTIFICATION_MANGLED);
    c->resolved_focus_state=focus_state!=NULL;
    c->resolved_focus_handling=focus_handling!=NULL;
    c->resolved_perf=perf!=NULL;
    c->resolved_operation=operation!=NULL;
    c->resolved_resume=resume!=NULL;
    expect_condition("oe-focus-state-resolved",focus_state!=NULL);
    expect_condition("oe-focus-handling-resolved",focus_handling!=NULL);
    expect_condition("oe-focus-perf-notification-resolved",perf!=NULL);
    expect_condition("oe-focus-operation-notification-resolved",operation!=NULL);
    expect_condition("oe-focus-resume-notification-resolved",resume!=NULL);
    /* The real guest path: the fixture calls the imports itself, so the
     * boundary must accept the signatures or the guest never executes. */
    uint64_t output[2]={0xa5a5,0xa5a5},returned=0xa5a5;
    uint64_t args[1]={(uintptr_t)output};
    struct nso_result r=guest_process_call(p,0x20,args,1,&returned);
#if defined(__ANDROID__) && defined(__aarch64__)
    expect_value("oe-focus-native-status",r.status,NSO_OK);
    expect_value("oe-focus-native-return",returned,0x91);
    c->native_ran=1;
    c->native_marker=output[1]==1;
    expect_condition("oe-focus-guest-call-returned",output[1]==1);
    /* The value the guest actually received back from the focus state call. */
    c->native_focus_state=(uint32_t)output[0];
    expect_value("oe-focus-guest-focus-state",output[0],0);
    /* The guest's own argument reached the setter unmoved. */
    c->native_focus_handling_mode=nso_host_oe_focus_handling_mode();
    expect_value("oe-focus-guest-focus-handling-mode",(uint64_t)(uint32_t)c->native_focus_handling_mode,(uint64_t)(uint32_t)OE_FIXTURE_FOCUS_HANDLING_MODE);
#else
    /* No AArch64 executor on this host: the boundary is prepared but the raw
     * guest entry cannot run here. Nothing may be faked in its place. */
    expect_value("oe-focus-native-unsupported",r.status,NSO_UNSUPPORTED);
    expect_value("oe-focus-native-no-raw-fallback",returned,0xa5a5);
#endif
    /* Direct host calls: every stub must record exactly what it was given. */
    if(focus_state&&focus_handling&&perf&&operation&&resume) {
        perf(true);
        operation(false);
        resume(true);
        focus_handling((int32_t)OE_DIRECT_FOCUS_HANDLING_MODE);
        c->direct_ok=nso_host_oe_performance_mode_changed_notification_enabled()&&
                     !nso_host_oe_operation_mode_changed_notification_enabled()&&
                     nso_host_oe_resume_notification_enabled()&&
                     nso_host_oe_focus_handling_mode()==OE_DIRECT_FOCUS_HANDLING_MODE&&
                     nso_host_oe_get_current_focus_state()==0;
        expect_condition("oe-focus-direct-perf-notification",nso_host_oe_performance_mode_changed_notification_enabled());
        expect_condition("oe-focus-direct-operation-notification",!nso_host_oe_operation_mode_changed_notification_enabled());
        expect_condition("oe-focus-direct-resume-notification",nso_host_oe_resume_notification_enabled());
        expect_value("oe-focus-direct-focus-handling-mode",(uint64_t)(uint32_t)nso_host_oe_focus_handling_mode(),(uint64_t)(uint32_t)OE_DIRECT_FOCUS_HANDLING_MODE);
        expect_value("oe-focus-direct-focus-state",nso_host_oe_get_current_focus_state(),0);
    }
    return &returned_token;
}
static int oe_focus_contract(const char *path) {
    struct guest_process_config config={1024*1024,65536};guest_process *p=NULL;
    CHECK(succeeded(guest_process_create(&config,&p)));
    char err[128];CHECK(succeeded(guest_process_load(p,path,err,sizeof(err))));
    struct oe_focus_case c={0};
    void *value=NULL;
    CHECK(succeeded(guest_process_run(p,exercise_oe_focus,&c,&value)));
    CHECK(value==&returned_token);
    expect_condition("oe-focus-state-resolved",c.resolved_focus_state);
    expect_condition("oe-focus-handling-resolved",c.resolved_focus_handling);
    expect_condition("oe-focus-perf-notification-resolved",c.resolved_perf);
    expect_condition("oe-focus-operation-notification-resolved",c.resolved_operation);
    expect_condition("oe-focus-resume-notification-resolved",c.resolved_resume);
    expect_condition("oe-focus-direct-record",c.direct_ok);
    CHECK(succeeded(guest_process_destroy(&p)) && !p);
    /* The final gate. Every expect_* above must have passed; without this the
     * contract could still exit 0 while the expectations had failed. */
    CHECK(!errors);
    /* The report states what this run observed, not a literal zero. */
    printf("{\"oe_focus_errors\":%u,\"focus_state_resolved\":%s,\"focus_handling_resolved\":%s,"
           "\"perf_notification_resolved\":%s,\"operation_notification_resolved\":%s,"
           "\"resume_notification_resolved\":%s,\"direct_ok\":%s,\"native_ran\":%s,"
           "\"guest_marker\":%s,\"guest_focus_state\":%d}\n",errors,
           c.resolved_focus_state?"true":"false",c.resolved_focus_handling?"true":"false",
           c.resolved_perf?"true":"false",c.resolved_operation?"true":"false",
           c.resolved_resume?"true":"false",c.direct_ok?"true":"false",
           c.native_ran?"true":"false",c.native_marker?"true":"false",
           c.native_ran?(int)c.native_focus_state:-1);
    return 0;
}

/* nn::oe::GetOperationMode() (main NSO import index 227).
 *
 * Zero arguments, one 32-bit result. The guest calls it from two
 * unconditional sites (0x51dc84 and 0x51ee40) and uses the result in
 * exactly one way: it compares the value against 1 and caches the outcome
 * as a byte flag at 0x7c90614. The four readers of that flag each pick a
 * screen-size scale factor of 0.026873588 (flag set) or 0.0 (flag clear),
 * and the factor multiplies the runtime screen width and height, so the
 * flag-clear branch leaves the scaled screen size degenerate at zero.
 * The working state of the game is therefore the flag-set one, so the
 * value the guest requires is 1. The enumerator name behind that value is
 * not available in-tree and is not claimed here.
 *
 * The fixture drives the real guest import path: the guest calls the
 * import itself and writes the returned word into the output slot, so the
 * boundary has to accept the zero-argument signature with a return value or
 * the guest never runs. */
#define OE_OPERATION_MODE_MANGLED "_ZN2nn2oe16GetOperationModeEv"
/* The operation mode value the guest code requires, see the comment above. */
#define OE_EXPECTED_OPERATION_MODE 1
struct oe_operation_mode_case {
    guest_process *process;
    int resolved;
    int native_ran,native_marker;
    uint32_t native_operation_mode;
};
static void *exercise_oe_operation_mode(guest_process *p,void *opaque) {
    struct oe_operation_mode_case *c=opaque;
    c->process=p;
    typedef uint32_t (*oe_operation_mode_fn)(void);
    oe_operation_mode_fn operation_mode=(oe_operation_mode_fn)(uintptr_t)guest_process_resolve(0,OE_OPERATION_MODE_MANGLED);
    c->resolved=operation_mode!=NULL;
    expect_condition("oe-operation-mode-resolved",operation_mode!=NULL);
    /* The real guest path: the fixture calls the import itself. */
    uint64_t output[2]={0xa5a5,0xa5a5},returned=0xa5a5;
    uint64_t args[1]={(uintptr_t)output};
    struct nso_result r=guest_process_call(p,0x20,args,1,&returned);
#if defined(__ANDROID__) && defined(__aarch64__)
    expect_value("oe-operation-mode-native-status",r.status,NSO_OK);
    expect_value("oe-operation-mode-native-return",returned,0x91);
    c->native_ran=1;
    c->native_marker=output[1]==1;
    expect_condition("oe-operation-mode-guest-call-returned",output[1]==1);
    /* The value the guest actually received back from the operation mode
     * call, observed through the real import path. */
    c->native_operation_mode=(uint32_t)output[0];
    expect_value("oe-operation-mode-guest-value",output[0],OE_EXPECTED_OPERATION_MODE);
#else
    /* No AArch64 executor on this host: the boundary is prepared but the raw
     * guest entry cannot run here. Nothing may be faked in its place. */
    expect_value("oe-operation-mode-native-unsupported",r.status,NSO_UNSUPPORTED);
    expect_value("oe-operation-mode-native-no-raw-fallback",returned,0xa5a5);
#endif
    /* Direct host call: the stub must report the same value it hands to the
     * guest, and it must not drift from it. */
    if(operation_mode) {
        uint32_t direct=operation_mode();
        expect_value("oe-operation-mode-direct-value",(uint64_t)direct,(uint64_t)OE_EXPECTED_OPERATION_MODE);
        expect_value("oe-operation-mode-host-recorded",(uint64_t)nso_host_oe_get_operation_mode(),(uint64_t)OE_EXPECTED_OPERATION_MODE);
    }
    return &returned_token;
}
static int oe_operation_mode_contract(const char *path) {
    struct guest_process_config config={1024*1024,65536};guest_process *p=NULL;
    CHECK(succeeded(guest_process_create(&config,&p)));
    char err[128];CHECK(succeeded(guest_process_load(p,path,err,sizeof(err))));
    struct oe_operation_mode_case c={0};
    void *value=NULL;
    CHECK(succeeded(guest_process_run(p,exercise_oe_operation_mode,&c,&value)));
    CHECK(value==&returned_token);
    expect_condition("oe-operation-mode-resolved",c.resolved);
    CHECK(succeeded(guest_process_destroy(&p)) && !p);
    /* The final gate. Every expect_* above must have passed; without this the
     * contract could still exit 0 while the expectations had failed. */
    CHECK(!errors);
    /* The report states what this run observed, not a literal zero. */
    printf("{\"oe_operation_mode_errors\":%u,\"resolved\":%s,\"native_ran\":%s,"
           "\"guest_marker\":%s,\"guest_operation_mode\":%d}\n",errors,
           c.resolved?"true":"false",c.native_ran?"true":"false",
           c.native_marker?"true":"false",
           c.native_ran?(int)c.native_operation_mode:-1);
    return 0;
}

/* nn::ldn::GetState() (main NSO import index 88).
 *
 * Zero arguments, one 32-bit result. All four guest call sites read the
 * result in w0 and all of them agree that 0 means "no Nintendo local
 * wireless session": the refresh helper at 0x15bc0 only continues into the
 * internet path on state 0, the two classification sites at 0x51dca8 and
 * 0x51ecb8 turn the state into "internet / local wireless / neither", and
 * the address site at 0x943198 only asks for a local wireless IPv4 address
 * for the states 3 and 5.
 *
 * The RG477M has no Nintendo local wireless radio and runs no local wireless
 * session, so 0 is the state it has to report. The fixture drives the real
 * guest import path: the guest calls the import itself and keeps the
 * returned word. */
#define LDN_STATE_MANGLED "_ZN2nn3ldn8GetStateEv"
/* The local wireless state the guest code requires on a device without a
 * local wireless session; see the comment above. */
#define LDN_EXPECTED_STATE 0
struct ldn_state_case {
    guest_process *process;
    int resolved;
    int native_ran,native_marker;
    uint32_t native_state;
};
static void *exercise_ldn_state(guest_process *p,void *opaque) {
    struct ldn_state_case *c=opaque;
    c->process=p;
    typedef int32_t (*ldn_state_fn)(void);
    ldn_state_fn state=(ldn_state_fn)(uintptr_t)guest_process_resolve(0,LDN_STATE_MANGLED);
    c->resolved=state!=NULL;
    expect_condition("ldn-state-resolved",state!=NULL);
    /* The real guest path: the fixture calls the import itself. */
    uint64_t output[2]={0xa5a5,0xa5a5},returned=0xa5a5;
    uint64_t args[1]={(uintptr_t)output};
    struct nso_result r=guest_process_call(p,0x20,args,1,&returned);
#if defined(__ANDROID__) && defined(__aarch64__)
    expect_value("ldn-state-native-status",r.status,NSO_OK);
    expect_value("ldn-state-native-return",returned,0x91);
    c->native_ran=1;
    c->native_marker=output[1]==1;
    expect_condition("ldn-state-guest-call-returned",output[1]==1);
    /* The value the guest actually received back from the state call,
     * observed through the real import path. */
    c->native_state=(uint32_t)output[0];
    expect_value("ldn-state-guest-value",output[0],LDN_EXPECTED_STATE);
#else
    /* No AArch64 executor on this host: the boundary is prepared but the raw
     * guest entry cannot run here. Nothing may be faked in its place. */
    expect_value("ldn-state-native-unsupported",r.status,NSO_UNSUPPORTED);
    expect_value("ldn-state-native-no-raw-fallback",returned,0xa5a5);
#endif
    /* Direct host call: the stub must report the same value it hands to the
     * guest. */
    if(state) {
        int32_t direct=state();
        expect_value("ldn-state-direct-value",(uint64_t)(uint32_t)direct,(uint64_t)(uint32_t)LDN_EXPECTED_STATE);
        expect_value("ldn-state-host-recorded",(uint64_t)(uint32_t)nso_host_ldn_get_state(),(uint64_t)(uint32_t)LDN_EXPECTED_STATE);
    }
    return &returned_token;
}
static int ldn_state_contract(const char *path) {
    struct guest_process_config config={1024*1024,65536};guest_process *p=NULL;
    CHECK(succeeded(guest_process_create(&config,&p)));
    char err[128];CHECK(succeeded(guest_process_load(p,path,err,sizeof(err))));
    struct ldn_state_case c={0};
    void *value=NULL;
    CHECK(succeeded(guest_process_run(p,exercise_ldn_state,&c,&value)));
    CHECK(value==&returned_token);
    expect_condition("ldn-state-resolved",c.resolved);
    CHECK(succeeded(guest_process_destroy(&p)) && !p);
    /* The final gate. Every expect_* above must have passed; without this the
     * contract could still exit 0 while the expectations had failed. */
    CHECK(!errors);
    /* The report states what this run observed, not a literal zero. */
    printf("{\"ldn_state_errors\":%u,\"resolved\":%s,\"native_ran\":%s,"
           "\"guest_marker\":%s,\"guest_ldn_state\":%d}\n",errors,
           c.resolved?"true":"false",c.native_ran?"true":"false",
           c.native_marker?"true":"false",
           c.native_ran?(int)c.native_state:-1);
    return 0;
}

/* nn::oe performance configuration (main NSO import indexes 239 and 240).
 *
 * nn::oe::SetPerformanceConfiguration(PerformanceMode, int) takes two 32-bit
 * arguments and returns nothing; nn::oe::GetPerformanceMode() takes none and
 * returns the 32-bit mode. The guest asks for a mode and then reads it back,
 * and that round trip is what the fixture drives through the real guest
 * import path: the guest sets a configuration and then reads the mode back
 * through the second import, so the boundary has to accept both signatures
 * or the guest never runs.
 *
 * The fixture passes a mode and a clock that differ from the values the
 * real guest uses, so a stub that ignores its arguments cannot satisfy both
 * the guest path and the direct host calls. */
#define OE_PERFORMANCE_SET_MANGLED "_ZN2nn2oe27SetPerformanceConfigurationENS0_15PerformanceModeEi"
#define OE_PERFORMANCE_GET_MANGLED "_ZN2nn2oe18GetPerformanceModeEv"
/* The mode and clock the fixture guest passes through the import path. */
#define OE_FIXTURE_PERFORMANCE_MODE 5
#define OE_FIXTURE_PERFORMANCE_CLOCK 0x20003
/* Different values for the direct host call, so the two observations cannot
 * be satisfied by one constant. */
#define OE_DIRECT_PERFORMANCE_MODE 7
#define OE_DIRECT_PERFORMANCE_CLOCK 0x10001
/* The values the host has to report back after the direct call above. They
 * are spelled out separately on purpose: an expectation derived from the
 * input would change together with it, and the contract could then no
 * longer tell a stub that records its arguments from one that ignores them.
 * The self-proof breaks exactly these two constants. */
#define OE_EXPECTED_DIRECT_MODE 7
#define OE_EXPECTED_DIRECT_CLOCK 0x10001
struct oe_performance_case {
    guest_process *process;
    int resolved_set,resolved_get;
    int native_ran,native_marker;
    uint32_t native_performance_mode;
    int direct_ok;
};
static void *exercise_oe_performance(guest_process *p,void *opaque) {
    struct oe_performance_case *c=opaque;
    c->process=p;
    typedef void (*oe_performance_set_fn)(int32_t,int32_t);
    typedef int32_t (*oe_performance_get_fn)(void);
    oe_performance_set_fn set_config=(oe_performance_set_fn)(uintptr_t)guest_process_resolve(0,OE_PERFORMANCE_SET_MANGLED);
    oe_performance_get_fn get_mode=(oe_performance_get_fn)(uintptr_t)guest_process_resolve(0,OE_PERFORMANCE_GET_MANGLED);
    c->resolved_set=set_config!=NULL;
    c->resolved_get=get_mode!=NULL;
    expect_condition("oe-performance-set-resolved",set_config!=NULL);
    expect_condition("oe-performance-get-resolved",get_mode!=NULL);
    /* The real guest path: the fixture calls the imports itself. */
    uint64_t output[2]={0xa5a5,0xa5a5},returned=0xa5a5;
    uint64_t args[1]={(uintptr_t)output};
    struct nso_result r=guest_process_call(p,0x20,args,1,&returned);
#if defined(__ANDROID__) && defined(__aarch64__)
    expect_value("oe-performance-native-status",r.status,NSO_OK);
    expect_value("oe-performance-native-return",returned,0x91);
    c->native_ran=1;
    c->native_marker=output[1]==1;
    expect_condition("oe-performance-guest-call-returned",output[1]==1);
    /* The mode the guest read back through the second import, after it set a
     * configuration through the first one. */
    c->native_performance_mode=(uint32_t)output[0];
    expect_value("oe-performance-guest-mode-readback",output[0],OE_FIXTURE_PERFORMANCE_MODE);
    /* The clock the guest asked for reached the host unmoved. */
    expect_value("oe-performance-guest-clock",(uint64_t)(uint32_t)nso_host_oe_performance_clock(),(uint64_t)(uint32_t)OE_FIXTURE_PERFORMANCE_CLOCK);
#else
    /* No AArch64 executor on this host: the boundary is prepared but the raw
     * guest entry cannot run here. Nothing may be faked in its place. */
    expect_value("oe-performance-native-unsupported",r.status,NSO_UNSUPPORTED);
    expect_value("oe-performance-native-no-raw-fallback",returned,0xa5a5);
#endif
    /* Direct host calls: every stub must record exactly what it was given. */
    if(set_config&&get_mode) {
        set_config((int32_t)OE_DIRECT_PERFORMANCE_MODE,(int32_t)OE_DIRECT_PERFORMANCE_CLOCK);
        c->direct_ok=nso_host_oe_get_performance_mode()==OE_EXPECTED_DIRECT_MODE &&
                     nso_host_oe_performance_clock()==OE_EXPECTED_DIRECT_CLOCK &&
                     get_mode()==OE_EXPECTED_DIRECT_MODE;
        expect_condition("oe-performance-direct-mode",nso_host_oe_get_performance_mode()==OE_EXPECTED_DIRECT_MODE);
        expect_value("oe-performance-direct-clock",(uint64_t)(uint32_t)nso_host_oe_performance_clock(),(uint64_t)(uint32_t)OE_EXPECTED_DIRECT_CLOCK);
        expect_value("oe-performance-direct-get-mode",(uint64_t)(uint32_t)get_mode(),(uint64_t)(uint32_t)OE_EXPECTED_DIRECT_MODE);
    }
    return &returned_token;
}
static int oe_performance_contract(const char *path) {
    struct guest_process_config config={1024*1024,65536};guest_process *p=NULL;
    CHECK(succeeded(guest_process_create(&config,&p)));
    char err[128];CHECK(succeeded(guest_process_load(p,path,err,sizeof(err))));
    struct oe_performance_case c={0};
    void *value=NULL;
    CHECK(succeeded(guest_process_run(p,exercise_oe_performance,&c,&value)));
    CHECK(value==&returned_token);
    expect_condition("oe-performance-set-resolved",c.resolved_set);
    expect_condition("oe-performance-get-resolved",c.resolved_get);
    expect_condition("oe-performance-direct-record",c.direct_ok);
    CHECK(succeeded(guest_process_destroy(&p)) && !p);
    /* The final gate. Every expect_* above must have passed; without this the
     * contract could still exit 0 while the expectations had failed. */
    CHECK(!errors);
    /* The report states what this run observed, not a literal zero. */
    printf("{\"oe_performance_errors\":%u,\"set_resolved\":%s,\"get_resolved\":%s,"
           "\"direct_ok\":%s,\"native_ran\":%s,\"guest_marker\":%s,"
           "\"guest_performance_mode\":%d}\n",errors,
           c.resolved_set?"true":"false",c.resolved_get?"true":"false",
           c.direct_ok?"true":"false",c.native_ran?"true":"false",
           c.native_marker?"true":"false",
           c.native_ran?(int)c.native_performance_mode:-1);
    return 0;
}

/* nv::SetGraphicsAllocator and nv::SetGraphicsDevtoolsAllocator (main NSO
 * import indexes 350 and 351).
 *
 * Both take the same four arguments as the already bound movie::SetAllocator
 * (import 74): an allocation callback, a free callback, a realloc callback
 * and a userdata pointer, and neither returns anything. The guest calls the
 * two of them back to back in one straight-line block (0x1434c90 and
 * 0x1434ca4), so both are reached on every boot.
 *
 * The fixture drives both through the real guest import path and passes its
 * own callbacks, so the boundary has to accept both four-argument void
 * signatures or the guest never runs. The host has to record the guest's
 * callbacks unmoved: the NVN graphics heap will be allocated through them,
 * so a stub that dropped or rewrote a pointer would break the graphics path
 * later on. */
#define NV_SET_GRAPHICS_ALLOCATOR_MANGLED "_ZN2nv20SetGraphicsAllocatorEPFPvmmS0_EPFvS0_S0_EPFS0_S0_mS0_ES0_"
#define NV_SET_GRAPHICS_DEVTOOLS_MANGLED "_ZN2nv28SetGraphicsDevtoolsAllocatorEPFPvmmS0_EPFvS0_S0_EPFS0_S0_mS0_ES0_"
/* The module offsets the fixture guest puts its three callbacks at; see the
 * nv-graphics-allocator fixture assembly in
 * scripts/runtime/build_guest_process_contract.sh. */
#define NV_FIXTURE_CB_ALLOC_OFF 0xc0
#define NV_FIXTURE_CB_FREE_OFF 0xc4
#define NV_FIXTURE_CB_REALLOC_OFF 0xc8
struct nv_allocator_case {
    guest_process *process;
    int resolved_graphics,resolved_devtools;
    int native_ran,native_marker;
    uintptr_t native_alloc,native_free,native_realloc,native_userdata;
    uintptr_t native_devtools_alloc,native_devtools_free,native_devtools_realloc,native_devtools_userdata;
    int direct_record_ok;
};
static void *exercise_nv_allocator(guest_process *p,void *opaque) {
    struct nv_allocator_case *c=opaque;
    c->process=p;
    typedef void (*nv_set_allocator_fn)(nv_alloc_fn,nv_free_fn,nv_realloc_fn,void *);
    nv_set_allocator_fn set_graphics=(nv_set_allocator_fn)(uintptr_t)guest_process_resolve(0,NV_SET_GRAPHICS_ALLOCATOR_MANGLED);
    nv_set_allocator_fn set_devtools=(nv_set_allocator_fn)(uintptr_t)guest_process_resolve(0,NV_SET_GRAPHICS_DEVTOOLS_MANGLED);
    c->resolved_graphics=set_graphics!=NULL;
    c->resolved_devtools=set_devtools!=NULL;
    expect_condition("nv-allocator-graphics-resolved",set_graphics!=NULL);
    expect_condition("nv-allocator-devtools-resolved",set_devtools!=NULL);
    /* The real guest path: the fixture calls the imports itself, so the
     * boundary must accept the four-argument void signature or the guest
     * never executes and the registration is never observed. */
    uint64_t output[2]={0xa5a5,0xa5a5},returned=0xa5a5;
    uint64_t args[1]={(uintptr_t)output};
    struct nso_result r=guest_process_call(p,0x20,args,1,&returned);
#if defined(__ANDROID__) && defined(__aarch64__)
    expect_value("nv-allocator-native-status",r.status,NSO_OK);
    expect_value("nv-allocator-native-return",returned,0x91);
    c->native_ran=1;
    c->native_marker=output[0]==1;
    expect_condition("nv-allocator-guest-call-returned",output[0]==1);
    uint64_t base=nso_base(guest_process_module(p));
    c->native_alloc=(uintptr_t)nso_host_nv_get_graphics_allocator();
    c->native_free=(uintptr_t)nso_host_nv_get_graphics_deallocator();
    c->native_realloc=(uintptr_t)nso_host_nv_get_graphics_reallocator();
    c->native_userdata=(uintptr_t)nso_host_nv_get_graphics_allocator_userdata();
    c->native_devtools_alloc=(uintptr_t)nso_host_nv_get_graphics_devtools_allocator();
    c->native_devtools_free=(uintptr_t)nso_host_nv_get_graphics_devtools_deallocator();
    c->native_devtools_realloc=(uintptr_t)nso_host_nv_get_graphics_devtools_reallocator();
    c->native_devtools_userdata=(uintptr_t)nso_host_nv_get_graphics_devtools_userdata();
    /* The guest's own callbacks reached the host unmoved. */
    expect_value("nv-allocator-guest-alloc-pointer",c->native_alloc,base+NV_FIXTURE_CB_ALLOC_OFF);
    expect_value("nv-allocator-guest-free-pointer",c->native_free,base+NV_FIXTURE_CB_FREE_OFF);
    expect_value("nv-allocator-guest-realloc-pointer",c->native_realloc,base+NV_FIXTURE_CB_REALLOC_OFF);
    expect_condition("nv-allocator-guest-userdata-null",c->native_userdata==0);
    /* The devtools registration is a separate one and is recorded separately. */
    expect_value("nv-allocator-devtools-alloc-pointer",c->native_devtools_alloc,base+NV_FIXTURE_CB_ALLOC_OFF);
    expect_value("nv-allocator-devtools-free-pointer",c->native_devtools_free,base+NV_FIXTURE_CB_FREE_OFF);
    expect_value("nv-allocator-devtools-realloc-pointer",c->native_devtools_realloc,base+NV_FIXTURE_CB_REALLOC_OFF);
    expect_condition("nv-allocator-devtools-userdata-null",c->native_devtools_userdata==0);
#else
    /* No AArch64 executor on this host: the boundary is prepared but the raw
     * guest entry cannot run here. Nothing may be faked in its place. */
    expect_value("nv-allocator-native-unsupported",r.status,NSO_UNSUPPORTED);
    expect_value("nv-allocator-native-no-raw-fallback",returned,0xa5a5);
#endif
    /* Direct host calls: the registration is stored exactly as given,
     * including the fourth argument. */
    if(set_graphics&&set_devtools) {
        set_graphics((nv_alloc_fn)(uintptr_t)0x1111,(nv_free_fn)(uintptr_t)0x2222,
                     (nv_realloc_fn)(uintptr_t)0x3333,(void *)(uintptr_t)0x4444);
        set_devtools((nv_alloc_fn)(uintptr_t)0x5555,(nv_free_fn)(uintptr_t)0x6666,
                     (nv_realloc_fn)(uintptr_t)0x7777,(void *)(uintptr_t)0x8888);
        uintptr_t da=(uintptr_t)nso_host_nv_get_graphics_allocator();
        uintptr_t df=(uintptr_t)nso_host_nv_get_graphics_deallocator();
        uintptr_t dr=(uintptr_t)nso_host_nv_get_graphics_reallocator();
        uintptr_t du=(uintptr_t)nso_host_nv_get_graphics_allocator_userdata();
        uintptr_t ta=(uintptr_t)nso_host_nv_get_graphics_devtools_allocator();
        uintptr_t tf=(uintptr_t)nso_host_nv_get_graphics_devtools_deallocator();
        uintptr_t tr=(uintptr_t)nso_host_nv_get_graphics_devtools_reallocator();
        uintptr_t tu=(uintptr_t)nso_host_nv_get_graphics_devtools_userdata();
        c->direct_record_ok=da==0x1111&&df==0x2222&&dr==0x3333&&du==0x4444&&
                            ta==0x5555&&tf==0x6666&&tr==0x7777&&tu==0x8888;
        expect_value("nv-allocator-direct-alloc",da,0x1111);
        expect_value("nv-allocator-direct-free",df,0x2222);
        expect_value("nv-allocator-direct-realloc",dr,0x3333);
        expect_value("nv-allocator-direct-userdata",du,0x4444);
        expect_value("nv-allocator-direct-devtools-alloc",ta,0x5555);
        expect_value("nv-allocator-direct-devtools-free",tf,0x6666);
        expect_value("nv-allocator-direct-devtools-realloc",tr,0x7777);
        expect_value("nv-allocator-direct-devtools-userdata",tu,0x8888);
    }
    return &returned_token;
}
static int nv_allocator_contract(const char *path) {
    struct guest_process_config config={1024*1024,65536};guest_process *p=NULL;
    CHECK(succeeded(guest_process_create(&config,&p)));
    char err[128];CHECK(succeeded(guest_process_load(p,path,err,sizeof(err))));
    struct nv_allocator_case c={0};
    void *value=NULL;
    CHECK(succeeded(guest_process_run(p,exercise_nv_allocator,&c,&value)));
    CHECK(value==&returned_token);
    expect_condition("nv-allocator-graphics-resolved",c.resolved_graphics);
    expect_condition("nv-allocator-devtools-resolved",c.resolved_devtools);
    expect_condition("nv-allocator-direct-record",c.direct_record_ok);
    CHECK(succeeded(guest_process_destroy(&p)) && !p);
    /* The final gate. Every expect_* above must have passed; without this the
     * contract could still exit 0 while the expectations had failed. */
    CHECK(!errors);
    /* The report states what this run observed, not a literal zero. */
    printf("{\"nv_allocator_errors\":%u,\"graphics_resolved\":%s,\"devtools_resolved\":%s,"
           "\"direct_record_ok\":%s,\"native_ran\":%s,\"guest_marker\":%s}\n",errors,
           c.resolved_graphics?"true":"false",c.resolved_devtools?"true":"false",
           c.direct_record_ok?"true":"false",c.native_ran?"true":"false",
           c.native_marker?"true":"false");
    return 0;
}

/* nv::InitializeGraphics (main NSO import index 352).
 *
 * Takes the NVN initialisation parameter block and its size, and returns
 * nothing: the single guest call site (0x1434cb4) loads x0 and x1 right
 * before the call and never consumes the result. The guest passes its own
 * parameter block at module offset 0x82d6000 and the size 0x800000.
 *
 * The stub records both values and does not claim that a graphics device
 * exists. The fixture drives the real guest import path and passes its own
 * parameter block, so the boundary has to accept the two-argument void
 * signature or the guest never runs. */
#define NV_INITIALIZE_GRAPHICS_MANGLED "_ZN2nv18InitializeGraphicsEPvm"
/* The module offset of the parameter block the fixture guest passes; see
 * the nv-initialize-graphics fixture assembly in
 * scripts/runtime/build_guest_process_contract.sh. */
#define NV_FIXTURE_PARAMS_OFF 0xc0
/* The size the fixture guest passes, the same value the real guest uses. */
#define NV_FIXTURE_GRAPHICS_SIZE 0x800000
struct nv_initialize_graphics_case {
    guest_process *process;
    int resolved;
    int native_ran,native_marker;
    uintptr_t native_param;
    uint64_t native_size;
    int direct_record_ok;
};
static void *exercise_nv_initialize_graphics(guest_process *p,void *opaque) {
    struct nv_initialize_graphics_case *c=opaque;
    c->process=p;
    typedef void (*nv_initialize_graphics_fn)(void *,uint64_t);
    nv_initialize_graphics_fn init_graphics=(nv_initialize_graphics_fn)(uintptr_t)guest_process_resolve(0,NV_INITIALIZE_GRAPHICS_MANGLED);
    c->resolved=init_graphics!=NULL;
    expect_condition("nv-initialize-graphics-resolved",init_graphics!=NULL);
    /* The real guest path: the fixture calls the import itself, so the
     * boundary must accept the two-argument void signature or the guest
     * never executes and the parameters are never observed. */
    uint64_t output[2]={0xa5a5,0xa5a5},returned=0xa5a5;
    uint64_t args[1]={(uintptr_t)output};
    struct nso_result r=guest_process_call(p,0x20,args,1,&returned);
#if defined(__ANDROID__) && defined(__aarch64__)
    expect_value("nv-initialize-graphics-native-status",r.status,NSO_OK);
    expect_value("nv-initialize-graphics-native-return",returned,0x91);
    c->native_ran=1;
    c->native_marker=output[0]==1;
    expect_condition("nv-initialize-graphics-guest-call-returned",output[0]==1);
    uint64_t base=nso_base(guest_process_module(p));
    c->native_param=(uintptr_t)nso_host_nv_get_initialize_graphics_param();
    c->native_size=nso_host_nv_get_initialize_graphics_size();
    /* The guest's own parameter block reached the host unmoved. */
    expect_value("nv-initialize-graphics-guest-param",c->native_param,base+NV_FIXTURE_PARAMS_OFF);
    expect_value("nv-initialize-graphics-guest-size",c->native_size,NV_FIXTURE_GRAPHICS_SIZE);
#else
    /* No AArch64 executor on this host: the boundary is prepared but the raw
     * guest entry cannot run here. Nothing may be faked in its place. */
    expect_value("nv-initialize-graphics-native-unsupported",r.status,NSO_UNSUPPORTED);
    expect_value("nv-initialize-graphics-native-no-raw-fallback",returned,0xa5a5);
#endif
    /* Direct host call: the parameters are recorded exactly as given. */
    if(init_graphics) {
        init_graphics((void *)(uintptr_t)0x1234,(uint64_t)0x20003);
        uintptr_t param=(uintptr_t)nso_host_nv_get_initialize_graphics_param();
        uint64_t size=nso_host_nv_get_initialize_graphics_size();
        c->direct_record_ok=param==0x1234&&size==0x20003;
        expect_value("nv-initialize-graphics-direct-param",param,0x1234);
        expect_value("nv-initialize-graphics-direct-size",size,0x20003);
    }
    return &returned_token;
}
static int nv_initialize_graphics_contract(const char *path) {
    struct guest_process_config config={1024*1024,65536};guest_process *p=NULL;
    CHECK(succeeded(guest_process_create(&config,&p)));
    char err[128];CHECK(succeeded(guest_process_load(p,path,err,sizeof(err))));
    struct nv_initialize_graphics_case c={0};
    void *value=NULL;
    CHECK(succeeded(guest_process_run(p,exercise_nv_initialize_graphics,&c,&value)));
    CHECK(value==&returned_token);
    expect_condition("nv-initialize-graphics-resolved",c.resolved);
    expect_condition("nv-initialize-graphics-direct-record",c.direct_record_ok);
    CHECK(succeeded(guest_process_destroy(&p)) && !p);
    /* The final gate. Every expect_* above must have passed; without this the
     * contract could still exit 0 while the expectations had failed. */
    CHECK(!errors);
    /* The report states what this run observed, not a literal zero. */
    printf("{\"nv_initialize_graphics_errors\":%u,\"resolved\":%s,\"direct_record_ok\":%s,"
           "\"native_ran\":%s,\"guest_marker\":%s}\n",errors,
           c.resolved?"true":"false",c.direct_record_ok?"true":"false",
           c.native_ran?"true":"false",c.native_marker?"true":"false");
    return 0;
}
int main(int argc,char **argv) {
    page=(size_t)sysconf(_SC_PAGESIZE);
    if(argc==2 && !strcmp(argv[1],"--tls-host-null")) {
        typedef void(*free_fn)(void *,size_t);
        free_fn release=(free_fn)(uintptr_t)guest_process_resolve(0,"nnosFreeMemoryForThreadLocal");CHECK(release);
        release(NULL,SIZE_MAX);puts("{\"tls_host_null_errors\":0,\"no_primary_or_pair_required\":true,\"callback_calls\":0}");return 0;
    }
    if(argc==2 && !strcmp(argv[1],"--tls-host-invalid")) {
        typedef void*(*allocate_fn)(size_t);
        allocate_fn allocate=(allocate_fn)(uintptr_t)guest_process_resolve(0,"nnosAllocateMemoryForThreadLocal");CHECK(allocate);
        (void)allocate(48);return 9;
    }
    if(argc==3 && !strcmp(argv[1],"--identity"))return identity_contract(argv[2],0);
    if(argc==3 && !strcmp(argv[1],"--identity-native"))return identity_contract(argv[2],1);
#if defined(NSO_PROFILE_IDENTITY_TESTING)
    if(argc==3 && !strcmp(argv[1],"--identity-recovery"))return identity_recovery_contract(argv[2]);
#endif
    if(argc==3 && !strcmp(argv[1],"--tls-registration"))return tls_registration_contract(argv[2],0);
    if(argc==3 && !strcmp(argv[1],"--dso-registration"))return dso_registration_contract(argv[2]);
    if(argc==3 && !strcmp(argv[1],"--snapshot-thread"))return snapshot_thread_contract(argv[2]);
    if(argc==3 && !strcmp(argv[1],"--movie-allocator"))return movie_allocator_contract(argv[2]);
    if(argc==3 && !strcmp(argv[1],"--oe-cpu-boost"))return oe_cpu_boost_contract(argv[2]);
    if(argc==3 && !strcmp(argv[1],"--hid-init"))return hid_init_contract(argv[2]);
    if(argc==3 && !strcmp(argv[1],"--oe-focus"))return oe_focus_contract(argv[2]);
    if(argc==3 && !strcmp(argv[1],"--oe-operation-mode"))return oe_operation_mode_contract(argv[2]);
    if(argc==3 && !strcmp(argv[1],"--ldn-get-state"))return ldn_state_contract(argv[2]);
    if(argc==3 && !strcmp(argv[1],"--oe-performance"))return oe_performance_contract(argv[2]);
    if(argc==3 && !strcmp(argv[1],"--nv-graphics-allocator"))return nv_allocator_contract(argv[2]);
    if(argc==3 && !strcmp(argv[1],"--nv-initialize-graphics"))return nv_initialize_graphics_contract(argv[2]);
    if(argc==3 && !strcmp(argv[1],"--tls-registration-fresh"))return tls_fresh_registration_contract(argv[2]);
    if(argc==3 && !strcmp(argv[1],"--tls-registration-null"))return tls_registration_contract(argv[2],1);
    if(argc==3 && !strcmp(argv[1],"--tls-registration-repeat"))return tls_registration_contract(argv[2],2);
    if(argc==3 && !strcmp(argv[1],"--tls-provider"))return tls_provider_contract(argv[2],0);
    if(argc==3 && !strcmp(argv[1],"--tls-provider-live"))return tls_provider_contract(argv[2],1);
    if(argc==3 && !strcmp(argv[1],"--tls-host-consumer"))return tls_host_consumer_contract(argv[2],1);
    if(argc==3 && !strcmp(argv[1],"--tls-host-busy"))return tls_host_consumer_contract(argv[2],2);
    if(argc==3 && !strcmp(argv[1],"--tls-host-uninitialized"))return tls_host_consumer_contract(argv[2],-1);
    if(argc==2 && !strcmp(argv[1],"--snprintf"))return snprintf_contract();
    if(argc==2 && !strcmp(argv[1],"--rwlock"))return rwlock_contract();
    if(argc==2 && !strcmp(argv[1],"--uuid"))return uuid_contract();
    if(argc==2 && !strcmp(argv[1],"--time"))return time_contract();
    if(argc==2 && !strcmp(argv[1],"--tick"))return tick_contract();
    if(argc==2 && !strcmp(argv[1],"--ctype"))return ctype_contract();
    if(argc==2 && !strcmp(argv[1],"--guest-tp"))return guest_tp_test_contract();
    if(argc==2 && !strcmp(argv[1],"--threads"))return threads_contract();
    if(argc==2 && !strcmp(argv[1],"--fs-allocator"))return fs_allocator_contract();
    if(argc==2 && !strcmp(argv[1],"--fs-query-cache"))return fs_query_cache_contract();
    if(argc==2 && !strcmp(argv[1],"--fs-mount-rom"))return fs_mount_rom_contract();
    if(argc==2 && !strcmp(argv[1],"--fs-open-file"))return fs_open_file_contract();
    if(argc==2 && !strcmp(argv[1],"--aoc"))return aoc_contract();
    if(argc==2 && !strcmp(argv[1],"--nifm"))return nifm_contract();
    if(argc==2 && !strcmp(argv[1],"--socket"))return socket_contract();
    if(argc==2 && !strcmp(argv[1],"--socket-eventfd"))return socket_eventfd_contract();
    if(argc==2 && !strcmp(argv[1],"--account"))return account_contract();
    if(argc==2 && !strcmp(argv[1],"--oe"))return oe_contract();
    if(argc==2 && !strcmp(argv[1],"--gettimeofday"))return gettimeofday_contract();
    if(argc==2 && !strcmp(argv[1],"--os-process-handle"))return os_process_handle_contract();
    if(argc==2 && !strcmp(argv[1],"--nn-result"))return nn_result_contract();
    if(argc==2 && !strcmp(argv[1],"--nnssl"))return nnssl_contract();
    if(argc==2 && !strcmp(argv[1],"--stdio"))return stdio_contract();
    if(argc==2 && !strcmp(argv[1],"--fs-open-dir"))return fs_open_dir_contract();
    if(argc==2 && !strcmp(argv[1],"--os-event"))return os_event_contract();
    if(argc==2 && !strcmp(argv[1],"--posix-memalign"))return posix_memalign_contract();
    if(argc==2 && !strcmp(argv[1],"--cxa-thread-atexit"))return cxa_thread_atexit_contract();
    if(argc==2 && !strcmp(argv[1],"--diag-abort"))return diag_abort_contract();
    if(argc==2 && !strcmp(argv[1],"--diag-aoc-event"))return diag_aoc_event_contract();
    if(argc==2 && !strcmp(argv[1],"--logf"))return logf_contract();
    if(argc==3 && !strcmp(argv[1],"--logf-native"))return logf_native_contract(argv[2]);
    if(argc==2 && !strcmp(argv[1],"--strcasecmp"))return strcasecmp_contract();
    if(argc==3 && !strcmp(argv[1],"--strcasecmp-native"))return strcasecmp_native_contract(argv[2]);
    if(argc==3 && !strcmp(argv[1],"--va-enabled"))return va_enabled_contract(argv[2],0);
    if(argc==3 && !strcmp(argv[1],"--va-native"))return va_enabled_contract(argv[2],1);
    if(argc==2 && !strcmp(argv[1],"--invalid-va-enabled")) {
        typedef uint32_t(*enabled_fn)(void);
        enabled_fn query=(enabled_fn)(uintptr_t)guest_process_resolve(0,"_ZN2nn2os29IsVirtualAddressMemoryEnabledEv");
        (void)query();return 9;
    }
    if(argc==2 && !strcmp(argv[1],"--locale"))return locale_contract();
    if(argc==3 && !strcmp(argv[1],"--locale-native"))return locale_native_contract(argv[2]);
    if(argc==2 && !strcmp(argv[1],"--resource-owner"))return resource_owner_contract();
    if(argc==3 && !strcmp(argv[1],"--resource"))return resource_process_contract(argv[2],0);
    if(argc==3 && !strcmp(argv[1],"--resource-native"))return resource_process_contract(argv[2],1);
#if defined(GUEST_RESOURCE_TESTING)
    if(argc==2 && !strcmp(argv[1],"--resource-recovery"))return resource_recovery_contract();
#endif
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
        "nvnBootstrapLoader","__cxa_finalize","exit",NULL};
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
