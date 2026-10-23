#define _GNU_SOURCE
#include "../../src/runtime/hle/guest_context.h"
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_vm.h>
#endif
#define CHECK(value) do {if(!(value)) {fprintf(stderr,"context contract line=%d: %s\n",__LINE__,#value);return 1;}} while(0)
static size_t page;
static _Thread_local int host_tls_marker;
static int return_token;
static int ok(struct guest_context_result r) {
    if(r.status==GUEST_CONTEXT_OK)return 1;
    fprintf(stderr,"context status=%d native=%d cleanup=%d\n",r.status,r.native_error,r.cleanup_error);return 0;
}
static int usage_is(guest_context *context,size_t pending,size_t charged,size_t tickets) {
    struct guest_context_usage v;
    return ok(guest_context_usage(context,&v)) && v.pending_bytes==pending &&
        v.charged_bytes==charged && v.tickets==tickets && !v.quarantined && !v.memory_info_valid_fields;
}
static int protection_is(uintptr_t address,int rw) {
#if defined(__APPLE__)
    mach_vm_address_t start=address;mach_vm_size_t bytes=0;
    vm_region_basic_info_data_64_t info;mach_msg_type_number_t count=VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t object=MACH_PORT_NULL;
    kern_return_t r=mach_vm_region(mach_task_self(),&start,&bytes,VM_REGION_BASIC_INFO_64,
                                  (vm_region_info_t)&info,&count,&object);
    if(object!=MACH_PORT_NULL)mach_port_deallocate(mach_task_self(),object);
    return r==KERN_SUCCESS && start<=address && address-start<bytes &&
        info.protection==(rw?(VM_PROT_READ|VM_PROT_WRITE):VM_PROT_NONE);
#else
    FILE *f=fopen("/proc/self/maps","r");if(!f)return 0;
    char line[1024],p[5];uintptr_t a,b;int found=0;
    while(fgets(line,sizeof(line),f))
        if(sscanf(line,"%" SCNxPTR "-%" SCNxPTR " %4s",&a,&b,p)==3 && a<=address && address<b) {
            found=p[0]==(rw?'r':'-') && p[1]==(rw?'w':'-') && p[2]=='-';break;
        }
    fclose(f);return found;
#endif
}
static int ticket_contract(void) {
    guest_context *context=NULL;
    struct guest_context_config config={.quota_bytes=2*page,.primary_stack_bytes=65536};
    CHECK(ok(guest_context_create(&config,&context)));
    guest_charge_ticket *first=NULL,*second=NULL,*extra=NULL;
    CHECK(ok(guest_charge_reserve(context,page,&first)));
    CHECK(usage_is(context,page,0,1));
    void *memory=mmap(NULL,page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    CHECK(memory!=MAP_FAILED);*(volatile unsigned char *)memory=0x5b;
    CHECK(*(volatile unsigned char *)memory==0x5b);
    CHECK(ok(guest_charge_commit(first)));CHECK(usage_is(context,0,page,1));
    CHECK(ok(guest_charge_reserve(context,page,&second)));
    CHECK(usage_is(context,page,page,2));
    CHECK(guest_charge_reserve(context,1,&extra).status==GUEST_CONTEXT_LIMIT && !extra);
    CHECK(ok(guest_charge_cancel(second)));CHECK(usage_is(context,0,page,1));
    CHECK(guest_context_destroy(context).status==GUEST_CONTEXT_BUSY);
    CHECK(!munmap(memory,page));CHECK(ok(guest_charge_release(first)));
    CHECK(usage_is(context,0,0,0));CHECK(ok(guest_context_destroy(context)));
    return 0;
}
struct callback_state {size_t stack_bytes;int calls,errors;};
static void *primary_callback(guest_context *context,void *opaque) {
    struct callback_state *state=opaque;state->calls++;
    volatile unsigned char local[64];local[0]=0x37;local[63]=0x59;
    struct guest_primary_stack stack;
    if(!ok(guest_context_primary_stack(context,&stack))) {state->errors++;return NULL;}
    uintptr_t address=(uintptr_t)&local[0];
    uintptr_t sp;
#if defined(__aarch64__)
    __asm__ volatile("mov %0, sp":"=r"(sp));
#else
#error "the native SP contract requires the selected AArch64 target"
#endif
    if(stack.bytes!=state->stack_bytes || address<stack.address || address-stack.address>=stack.bytes ||
            sp<stack.address || sp-stack.address>=stack.bytes ||
            local[0]!=0x37 || local[63]!=0x59 || host_tls_marker)state->errors++;
    if(!protection_is(stack.address,1) || !protection_is(stack.address-stack.guard_bytes,0) ||
            !protection_is(stack.address+stack.bytes,0))state->errors++;
    host_tls_marker=0x71;errno=EDOM;
    if(!usage_is(context,0,stack.bytes,1) || host_tls_marker!=0x71 || errno!=EDOM)state->errors++;
    if(guest_context_destroy(context).status!=GUEST_CONTEXT_BUSY)state->errors++;
    void *nested=NULL;
    if(guest_context_run_primary(context,primary_callback,opaque,&nested).status!=GUEST_CONTEXT_BUSY)state->errors++;
    return &return_token;
}
static int stack_contract(void) {
    struct guest_context_config config={.quota_bytes=2*65536,.primary_stack_bytes=65536};
    guest_context *context=NULL;
    CHECK(ok(guest_context_create(&config,&context)));
    host_tls_marker=0x22;
    struct callback_state state={.stack_bytes=config.primary_stack_bytes};
    for(unsigned i=0;i<3;i++) {
        void *value=NULL;
        CHECK(ok(guest_context_run_primary(context,primary_callback,&state,&value)));
        CHECK(value==&return_token && !state.errors && host_tls_marker==0x22);
        CHECK(usage_is(context,0,0,0));
    }
#if defined(GUEST_CONTEXT_TESTING)
    guest_context_test_fail_next_map(context,ENOMEM);
    void *value=NULL;int calls=state.calls;
    struct guest_context_result r=guest_context_run_primary(context,primary_callback,&state,&value);
    CHECK(r.status==GUEST_CONTEXT_SYSTEM_ERROR && r.native_error==ENOMEM && !r.cleanup_error);
    CHECK(!value && state.calls==calls && usage_is(context,0,0,0));
    CHECK(guest_context_test_pending_before_map(context)==config.primary_stack_bytes);
    CHECK(ok(guest_context_run_primary(context,primary_callback,&state,&value)) && value==&return_token);
    CHECK(!state.errors && usage_is(context,0,0,0));
#endif
    CHECK(ok(guest_context_destroy(context)));
    /* Explicit smaller quota rejects the same configured stack before mmap. */
    config.quota_bytes=config.primary_stack_bytes-1;
    CHECK(ok(guest_context_create(&config,&context)));
    void *ignored=NULL;int limited_calls=state.calls;
    CHECK(guest_context_run_primary(context,primary_callback,&state,&ignored).status==GUEST_CONTEXT_LIMIT);
    CHECK(state.calls==limited_calls && usage_is(context,0,0,0));
    CHECK(ok(guest_context_destroy(context)));
    return 0;
}
enum {THREADS=8};
struct competition {
    guest_context *context;pthread_mutex_t mutex;pthread_cond_t ready;
    unsigned attempted,accepted,errors;int release;
};
static void *competitor(void *opaque) {
    struct competition *group=opaque;guest_charge_ticket *ticket=NULL;
    struct guest_context_result r=guest_charge_reserve(group->context,page,&ticket);
    void *memory=NULL;int failure=0,accepted=r.status==GUEST_CONTEXT_OK;
    if(accepted) {
        struct guest_context_usage v;
        if(!ok(guest_context_usage(group->context,&v)) || v.pending_bytes<page)failure=1;
        memory=mmap(NULL,page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        if(memory==MAP_FAILED) {guest_charge_cancel(ticket);ticket=NULL;failure=1;}
        else {
            *(volatile unsigned char *)memory=0x48;
            if(*(volatile unsigned char *)memory!=0x48 || !ok(guest_charge_commit(ticket)))failure=1;
        }
    } else if(r.status!=GUEST_CONTEXT_LIMIT)failure=1;
    pthread_mutex_lock(&group->mutex);
    group->attempted++;group->accepted+=(unsigned)accepted;group->errors+=(unsigned)failure;
    pthread_cond_broadcast(&group->ready);
    while(!group->release)pthread_cond_wait(&group->ready,&group->mutex);
    pthread_mutex_unlock(&group->mutex);
    if(memory && memory!=MAP_FAILED) {
        failure=munmap(memory,page)!=0;
        if(!failure && !ok(guest_charge_release(ticket)))failure=1;
        if(failure) {pthread_mutex_lock(&group->mutex);group->errors++;pthread_mutex_unlock(&group->mutex);}
    }
    return NULL;
}
static int quota_competition(void) {
    struct guest_context_config config={.quota_bytes=2*page,.primary_stack_bytes=65536};
    struct competition group={.mutex=PTHREAD_MUTEX_INITIALIZER,.ready=PTHREAD_COND_INITIALIZER};
    CHECK(ok(guest_context_create(&config,&group.context)));
    pthread_t threads[THREADS];
    for(unsigned i=0;i<THREADS;i++)CHECK(!pthread_create(&threads[i],NULL,competitor,&group));
    pthread_mutex_lock(&group.mutex);
    while(group.attempted<THREADS)pthread_cond_wait(&group.ready,&group.mutex);
    int correct=group.accepted==2 && !group.errors && usage_is(group.context,0,2*page,2);
    group.release=1;pthread_cond_broadcast(&group.ready);pthread_mutex_unlock(&group.mutex);
    for(unsigned i=0;i<THREADS;i++)CHECK(!pthread_join(threads[i],NULL));
    CHECK(correct && !group.errors && usage_is(group.context,0,0,0));
    CHECK(ok(guest_context_destroy(group.context)));
    CHECK(!pthread_cond_destroy(&group.ready));CHECK(!pthread_mutex_destroy(&group.mutex));
    return 0;
}
int main(void) {
    long bytes=sysconf(_SC_PAGESIZE);CHECK(bytes>0 && 65536%(size_t)bytes==0);page=(size_t)bytes;
    guest_context *context=NULL;
    struct guest_context_config missing={0};
    CHECK(guest_context_create(&missing,&context).status==GUEST_CONTEXT_INVALID && !context);
    CHECK(!ticket_contract());puts("phase=real_allocation_ticket_lifecycle completed");
    CHECK(!stack_contract());puts("phase=owned_65536_stack_guards_callback_tls_and_join completed");
    CHECK(!quota_competition());puts("phase=8_thread_quota_competition completed accepted=2 limited=6");
    int recovery=0;
#if defined(GUEST_CONTEXT_TESTING)
    recovery=1;
#endif
    printf("{\"stack_bytes\":65536,\"host_page_bytes\":%zu,\"quota_explicit\":true,"
           "\"quota_threads\":8,\"quota_accepted\":2,\"quota_limited\":6,\"callback_sp_verified\":true,"
           "\"charged_after_join\":0,\"pending_after_join\":0,\"map_failure_rollback_cases\":%d,"
           "\"memory_info_valid_fields\":0,\"errors\":0}\n",page,recovery);
    return 0;
}
