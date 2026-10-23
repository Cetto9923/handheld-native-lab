#define _GNU_SOURCE
#include "../../src/runtime/hle/guest_process.h"
#include "../../src/runtime/hle/guest_context.h"
#include "../../src/runtime/hle/nso_host.h"
#include <errno.h>
#include <inttypes.h>
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
struct callback_input {size_t quota,image;};
static void *exercise(guest_process *process,void *opaque) {
    struct callback_input *input=opaque;calls++;
    struct guest_memory_info info;
    memset(&info,0xa5,sizeof(info));
    if(!succeeded(guest_process_memory_info(process,&info))) {expect_condition("query-initial-status",0);return NULL;}
    expect_value("query-total",info.total,input->quota);
    expect_value("query-used",info.used,input->image+65536);
    expect_value("query-heap-capacity",info.heap_capacity,0);
    expect_value("query-heap-blocks",info.heap_blocks,0);
    expect_value("query-baseline",info.baseline,input->image);
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
int main(int argc,char **argv) {
    page=(size_t)sysconf(_SC_PAGESIZE);
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
        "nvnBootstrapLoader","_ZN2nn2os19AllocateMemoryPagesEmm","__nnmusl_init_dso",NULL};
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
    struct callback_input input={config.quota_bytes,nso_image_size(guest_process_module(process))};
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
    CHECK(!extension_calls && !errors && calls==2);
    printf("{\"errors\":%u,\"calls\":%u,\"extension_calls\":%u,\"stack_bytes\":65536,\"query_fields\":7,\"padding_preserved\":true,\"thread_record_joined\":true,\"signal_dispositions_restored\":true,\"cleanup_failure_cases\":%d}\n",
        errors,calls,extension_calls,
#if defined(NSO_LOADER_TESTING)
        5
#else
        0
#endif
    );return 0;
}
