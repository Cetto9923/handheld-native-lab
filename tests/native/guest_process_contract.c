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
