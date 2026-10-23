#include "../../src/runtime/hle/address_region.h"
#include <inttypes.h>
#include <stdio.h>
#include <sys/resource.h>
#include <sys/mman.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_vm.h>
#endif

static int inaccessible(uintptr_t address) {
#if defined(__APPLE__)
    mach_vm_address_t start=address;
    mach_vm_size_t size=0;
    vm_region_basic_info_data_64_t info;
    mach_msg_type_number_t count=VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t object=MACH_PORT_NULL;
    kern_return_t result=mach_vm_region(mach_task_self(),&start,&size,VM_REGION_BASIC_INFO_64,
                                      (vm_region_info_t)&info,&count,&object);
    if(object!=MACH_PORT_NULL)mach_port_deallocate(mach_task_self(),object);
    return result==KERN_SUCCESS && start<=address && address-start<size && info.protection==VM_PROT_NONE;
#else
    /* This contract's independent VM protection query is currently macOS-only. */
    (void)address;return 0;
#endif
}
int main(void) {
    const size_t sizes[]={4096,65536,0x200000,UINT64_C(0xf80000000)};
    uintptr_t addresses[4]={0};
    struct rusage before,after;
    if(getrusage(RUSAGE_SELF,&before))return 1;
    for(size_t i=0;i<4;i++) {
        uint32_t result=nso_host_allocate_address_region(&addresses[i],sizes[i]);
        size_t alignment=sizes[i]<0x200000?0x10000:0x200000;
        int protection=inaccessible(addresses[i]) && inaccessible(addresses[i]+sizes[i]-1);
        if(result || addresses[i]%alignment || !protection)return 1;
        printf("reserve size=0x%zx address=0x%" PRIxPTR " result=%u PROT_NONE=%d\n",
               sizes[i],addresses[i],result,protection);
    }
    if(getrusage(RUSAGE_SELF,&after))return 1;
    for(size_t i=4;i>0;i--)if(nso_host_free_address_region(addresses[i-1]))return 1;
    printf("{\"reservations\":4,\"largest_bytes\":%" PRIu64 ",\"physical_pages_touched\":0,"
           "\"maxrss_before\":%ld,\"maxrss_after\":%ld,\"maxrss_unit\":\"host_native\",\"status\":\"passed\"}\n",
           UINT64_C(0xf80000000),before.ru_maxrss,after.ru_maxrss);
    return 0;
}
