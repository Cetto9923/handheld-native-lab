#define _GNU_SOURCE
#include "../../src/runtime/hle/address_region.h"
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

#define CHECK(value) do {if(!(value)) { \
    fprintf(stderr,"owned-page contract failed line=%d expression=%s\n",__LINE__,#value); \
    return 1;}} while(0)

static size_t page;
static int protection_is(uintptr_t address,int readwrite) {
#if defined(__APPLE__)
    mach_vm_address_t start=address;mach_vm_size_t size=0;
    vm_region_basic_info_data_64_t info;
    mach_msg_type_number_t count=VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t object=MACH_PORT_NULL;
    kern_return_t status=mach_vm_region(mach_task_self(),&start,&size,VM_REGION_BASIC_INFO_64,
                                       (vm_region_info_t)&info,&count,&object);
    if(object!=MACH_PORT_NULL)mach_port_deallocate(mach_task_self(),object);
    return status==KERN_SUCCESS && start<=address && address-start<size &&
        info.protection==(readwrite?(VM_PROT_READ|VM_PROT_WRITE):VM_PROT_NONE);
#elif defined(__linux__)
    /* A bounded test-only lookup of this process's own requested address. No
     * mapping contents or unrelated address inventory is returned or stored. */
    FILE *maps=fopen("/proc/self/maps","r");
    if(!maps)return 0;
    char line[1024],permissions[5];uintptr_t begin,end;int found=0;
    while(fgets(line,sizeof(line),maps))
        if(sscanf(line,"%" SCNxPTR "-%" SCNxPTR " %4s",&begin,&end,permissions)==3 &&
                begin<=address && address<end) {
            found=permissions[0]==(readwrite?'r':'-') &&
                permissions[1]==(readwrite?'w':'-') && permissions[2]=='-';break;
        }
    fclose(maps);return found;
#else
    (void)address;(void)readwrite;return 0;
#endif
}
static int ok(struct nso_owned_page_result result) {
    if(result.status==NSO_OWNED_PAGE_OK)return 1;
    fprintf(stderr,"page result status=%d detail=%d errno=%d restore=%d\n",
            result.status,result.detail,result.native_errno,result.restore_errno);
    return 0;
}
static int usage(size_t regions,size_t reserved,size_t mapped) {
    struct nso_owned_page_usage view;
    return ok(nso_host_owned_pages_usage(&view)) && view.mapped_guest_bytes_valid &&
        !view.quarantined_regions && view.region_count==regions &&
        view.reserved_guest_bytes==reserved && view.mapped_guest_bytes==mapped;
}
static int bytes_equal(uintptr_t address,size_t size,unsigned char value) {
    volatile const unsigned char *p=(volatile const unsigned char *)address;
    for(size_t i=0;i<size;i++)if(p[i]!=value)return 0;
    return 1;
}
static void fill(uintptr_t address,size_t size,unsigned char value) {
    volatile unsigned char *p=(volatile unsigned char *)address;
    for(size_t i=0;i<size;i++)p[i]=value;
}
static int split_cycle(uintptr_t address,unsigned char value) {
    CHECK(ok(nso_host_owned_pages_map(address+page,4*page)));
    CHECK(bytes_equal(address+page,4*page,0));
    fill(address+page,4*page,value);
    CHECK(bytes_equal(address+page,4*page,value));
    CHECK(ok(nso_host_owned_pages_free(address+2*page,2*page)));
    /* Only the still-accessible sides are read. The freed middle is not read
     * until an actual new successful mapping has granted access again. */
    CHECK(bytes_equal(address+page,page,value));
    CHECK(bytes_equal(address+4*page,page,value));
    CHECK(ok(nso_host_owned_pages_map(address+2*page,2*page)));
    CHECK(bytes_equal(address+2*page,2*page,0));
    CHECK(bytes_equal(address+page,page,value));
    CHECK(bytes_equal(address+4*page,page,value));
    return 0;
}
static int small_contract(void) {
    uintptr_t address=0;
    CHECK(usage(0,0,0));
    CHECK(!nso_host_allocate_address_region(&address,8*page));
    CHECK(usage(1,8*page,0));
    CHECK(protection_is(address+page,0));
    CHECK(protection_is(address-page,0));
    CHECK(ok(nso_host_owned_pages_map(address+page,4*page)));
    CHECK(protection_is(address+page,1));
    CHECK(bytes_equal(address+page,4*page,0));
    fill(address+page,4*page,0x67);
    CHECK(usage(1,8*page,4*page));
    struct nso_owned_page_result result=nso_host_owned_pages_map(address+page,page);
    CHECK(result.status==NSO_OWNED_PAGE_UNSUPPORTED && result.detail==NSO_OWNED_PAGE_DETAIL_OVERLAP);
    result=nso_host_owned_pages_free(address,2*page);
    CHECK(result.status==NSO_OWNED_PAGE_UNSUPPORTED && result.detail==NSO_OWNED_PAGE_DETAIL_HOLES);
    CHECK(usage(1,8*page,4*page));
    CHECK(bytes_equal(address+page,4*page,0x67));
    CHECK(ok(nso_host_owned_pages_free(address+2*page,2*page)));
    CHECK(protection_is(address+2*page,0));CHECK(protection_is(address+3*page,0));
    CHECK(usage(1,8*page,2*page));
    CHECK(bytes_equal(address+page,page,0x67));
    CHECK(bytes_equal(address+4*page,page,0x67));
    CHECK(ok(nso_host_owned_pages_map(address+2*page,2*page)));
    CHECK(bytes_equal(address+2*page,2*page,0));
    CHECK(bytes_equal(address+page,page,0x67));
    CHECK(bytes_equal(address+4*page,page,0x67));
    CHECK(usage(1,8*page,4*page));
    CHECK(ok(nso_host_owned_pages_free(address+page,4*page)));
    CHECK(usage(1,8*page,0));
    CHECK(ok(nso_host_owned_pages_map(address+page,page)));
    CHECK(ok(nso_host_owned_pages_map(address+2*page,page)));
    CHECK(ok(nso_host_owned_pages_free(address+page,2*page)));
    CHECK(usage(1,8*page,0));
    CHECK(ok(nso_host_owned_pages_map(address+page,4*page)));
    fill(address+page,4*page,0x91);
    CHECK(ok(nso_host_owned_pages_free(address+page,page)));
    CHECK(usage(1,8*page,3*page));
    CHECK(bytes_equal(address+2*page,3*page,0x91));
    CHECK(ok(nso_host_owned_pages_free(address+4*page,page)));
    CHECK(usage(1,8*page,2*page));
    CHECK(bytes_equal(address+2*page,2*page,0x91));
    CHECK(ok(nso_host_owned_pages_free(address+2*page,2*page)));
    CHECK(usage(1,8*page,0));
    CHECK(!split_cycle(address,0xa3));
    /* Whole release must account for all three live accessible intervals. */
    CHECK(!nso_host_free_address_region(address));
    CHECK(usage(0,0,0));
    puts("phase=split_recycle_and_whole_release completed");
    return 0;
}
static int large_reservation(void) {
    uintptr_t address=0;
    size_t bytes=UINT64_C(0xf80000000);
    CHECK(!nso_host_allocate_address_region(&address,bytes));
    CHECK(usage(1,bytes,0));
    uintptr_t small=address+0x200000;
    CHECK(ok(nso_host_owned_pages_map(small,page)));
    fill(small,page,0x5d);
    CHECK(bytes_equal(small,page,0x5d));
    CHECK(protection_is(address,0));CHECK(protection_is(address+bytes-page,0));
    CHECK(usage(1,bytes,page));
    CHECK(ok(nso_host_owned_pages_free(small,page)));
    CHECK(usage(1,bytes,0));
    CHECK(!nso_host_free_address_region(address));
    CHECK(usage(0,0,0));
    printf("phase=large_reservation completed reserved_bytes=%zu accessible_bytes_touched=%zu\n",bytes,page);
    return 0;
}

enum { WORKERS=8, CYCLES=32, RELEASE_RACES=32 };
struct worker {unsigned id;int failure;};
static void *worker_main(void *opaque) {
    struct worker *worker=opaque;
    for(unsigned i=0;i<CYCLES;i++) {
        uintptr_t address=0;
        if(nso_host_allocate_address_region(&address,8*page)) {worker->failure=1;return NULL;}
        unsigned char value=(unsigned char)((worker->id*17+i)%255+1);
        if(split_cycle(address,value))worker->failure=1;
        if(nso_host_free_address_region(address))worker->failure=1;
        if(worker->failure)return NULL;
    }
    return NULL;
}
static int threaded_contract(void) {
    pthread_t threads[WORKERS];struct worker workers[WORKERS]={0};
    for(unsigned i=0;i<WORKERS;i++) {
        workers[i].id=i;
        CHECK(!pthread_create(&threads[i],NULL,worker_main,&workers[i]));
    }
    for(unsigned i=0;i<WORKERS;i++) {
        CHECK(!pthread_join(threads[i],NULL));CHECK(!workers[i].failure);
    }
    CHECK(usage(0,0,0));
    puts("phase=independent_thread_reservations completed");
    return 0;
}
struct release_race {
    pthread_mutex_t mutex;
    pthread_cond_t ready;
    int start;
    uintptr_t address;
    struct nso_owned_page_result map_result;
    uint32_t release_result;
};
static void race_wait(struct release_race *race) {
    pthread_mutex_lock(&race->mutex);
    while(!race->start)pthread_cond_wait(&race->ready,&race->mutex);
    pthread_mutex_unlock(&race->mutex);
}
static void *race_map(void *opaque) {
    struct release_race *race=opaque;race_wait(race);
    race->map_result=nso_host_owned_pages_map(race->address,page);
    /* Never dereference: release may already have run after map returned. */
    return NULL;
}
static void *race_release(void *opaque) {
    struct release_race *race=opaque;race_wait(race);
    race->release_result=nso_host_free_address_region(race->address);
    return NULL;
}
static int serialized_release(void) {
    for(unsigned i=0;i<RELEASE_RACES;i++) {
        struct release_race race={.mutex=PTHREAD_MUTEX_INITIALIZER,.ready=PTHREAD_COND_INITIALIZER};
        CHECK(!nso_host_allocate_address_region(&race.address,8*page));
        pthread_t map_thread,release_thread;
        CHECK(!pthread_create(&map_thread,NULL,race_map,&race));
        CHECK(!pthread_create(&release_thread,NULL,race_release,&race));
        pthread_mutex_lock(&race.mutex);race.start=1;
        pthread_cond_broadcast(&race.ready);pthread_mutex_unlock(&race.mutex);
        CHECK(!pthread_join(map_thread,NULL));CHECK(!pthread_join(release_thread,NULL));
        CHECK(!race.release_result);
        CHECK(race.map_result.status==NSO_OWNED_PAGE_OK || race.map_result.status==NSO_OWNED_PAGE_NOT_OWNED);
        CHECK(!pthread_cond_destroy(&race.ready));CHECK(!pthread_mutex_destroy(&race.mutex));
        CHECK(usage(0,0,0));
    }
    puts("phase=owner_release_serialization completed");
    return 0;
}

#if defined(NSO_OWNED_PAGES_TESTING)
static int protect_calls,fail_restore;
static int partial_protect_failure(void *address,size_t size,int protection) {
    protect_calls++;
    if(protect_calls==1) {
        if(mprotect(address,size/2,protection))return -1;
        errno=EACCES;return -1;
    }
    if(fail_restore) {errno=EIO;return -1;}
    return mprotect(address,size,protection);
}
static void *replacement_failure(void *address,size_t size) {
    (void)address;(void)size;errno=ENOMEM;return MAP_FAILED;
}
static int recovery_contract(void) {
    uintptr_t address=0;
    CHECK(!nso_host_allocate_address_region(&address,8*page));
    protect_calls=0;fail_restore=0;
    nso_host_owned_pages_test_vm(partial_protect_failure,NULL);
    struct nso_owned_page_result result=nso_host_owned_pages_map(address,2*page);
    CHECK(result.status==NSO_OWNED_PAGE_SYSTEM_ERROR && result.native_errno==EACCES && !result.restore_errno);
    CHECK(protect_calls==2);CHECK(usage(1,8*page,0));
    CHECK(protection_is(address,0));CHECK(protection_is(address+page,0));
    nso_host_owned_pages_test_vm(NULL,NULL);
    CHECK(ok(nso_host_owned_pages_map(address,2*page)));
    CHECK(bytes_equal(address,2*page,0));
    CHECK(!nso_host_free_address_region(address));CHECK(usage(0,0,0));

    CHECK(!nso_host_allocate_address_region(&address,8*page));
    protect_calls=0;fail_restore=1;
    nso_host_owned_pages_test_vm(partial_protect_failure,NULL);
    result=nso_host_owned_pages_map(address,2*page);
    CHECK(result.status==NSO_OWNED_PAGE_QUARANTINED && result.native_errno==EACCES && result.restore_errno==EIO);
    struct nso_owned_page_usage view;
    CHECK(nso_host_owned_pages_usage(&view).status==NSO_OWNED_PAGE_QUARANTINED);
    CHECK(!view.mapped_guest_bytes_valid && !view.mapped_guest_bytes && view.quarantined_regions==1);
    CHECK(nso_host_owned_pages_map(address,2*page).status==NSO_OWNED_PAGE_QUARANTINED);
    CHECK(nso_host_owned_pages_free(address,2*page).status==NSO_OWNED_PAGE_QUARANTINED);
    /* Quarantine: only release, no reads even of an apparently changed prefix. */
    nso_host_owned_pages_test_vm(NULL,NULL);
    CHECK(!nso_host_free_address_region(address));CHECK(usage(0,0,0));

    CHECK(!nso_host_allocate_address_region(&address,8*page));
    CHECK(ok(nso_host_owned_pages_map(address,2*page)));
    fill(address,2*page,0x39);
    nso_host_owned_pages_test_vm(NULL,replacement_failure);
    result=nso_host_owned_pages_free(address,page);
    CHECK(result.status==NSO_OWNED_PAGE_QUARANTINED && result.native_errno==ENOMEM);
    CHECK(nso_host_owned_pages_usage(&view).status==NSO_OWNED_PAGE_QUARANTINED);
    CHECK(!view.mapped_guest_bytes_valid && !view.mapped_guest_bytes);
    nso_host_owned_pages_test_vm(NULL,NULL);
    CHECK(!nso_host_free_address_region(address));CHECK(usage(0,0,0));
    puts("phase=controlled_vm_error_recovery completed");
    return 0;
}
#endif

int main(void) {
    long value=sysconf(_SC_PAGESIZE);
    CHECK(value>=4096 && value<=65536 && !(value%4096));page=(size_t)value;
    CHECK(!small_contract());CHECK(!large_reservation());
    CHECK(!threaded_contract());CHECK(!serialized_release());
    int recovery=0;
#if defined(NSO_OWNED_PAGES_TESTING)
    CHECK(!recovery_contract());recovery=3;
#endif
    CHECK(usage(0,0,0));
    printf("{\"host_page_bytes\":%zu,\"threads\":%u,\"cycles_per_thread\":%u,"
           "\"release_races\":%u,\"controlled_vm_failure_cases\":%d,"
           "\"mapped_guest_bytes\":0,\"ledger_valid\":true,\"errors\":0,"
           "\"physical_budget_claimed\":false}\n",page,WORKERS,CYCLES,RELEASE_RACES,recovery);
    return 0;
}
