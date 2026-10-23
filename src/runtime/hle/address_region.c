#define _GNU_SOURCE
#include "address_region.h"
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

struct reservation {
    uintptr_t address;
    void *mapping;
    size_t mapping_size,requested_size;
    struct reservation *next;
};
static pthread_mutex_t region_mutex=PTHREAD_MUTEX_INITIALIZER;
static struct reservation *regions;
uint32_t nso_host_allocate_address_region(uintptr_t *out,size_t size) {
    /* SDK VammManager +0x145da0: 64-KiB alignment below 2 MiB,
     * 2-MiB alignment otherwise, with gaps of at least 4 KiB.
     * Host page granularity may require a larger inaccessible guard. */
    if(!out || !size || size%4096) {
        fprintf(stderr,"[address-region] unsupported size/output contract size=0x%zx\n",size);
        abort();
    }
    size_t alignment=size<0x200000?0x10000:0x200000;
    size_t page=(size_t)sysconf(_SC_PAGESIZE);
    if(size>SIZE_MAX-(page-1))return 0x1803;
    size_t rounded=(size+page-1)&~(page-1);
    if(rounded>SIZE_MAX-alignment-2*page)return 0x1803;
    size_t mapping_size=rounded+alignment+2*page;
    struct reservation *r=malloc(sizeof(*r));
    if(!r)return 0x1003; /* SDK metadata-node exhaustion */
    int flags=MAP_PRIVATE|MAP_ANONYMOUS;
#ifdef MAP_NORESERVE
    flags|=MAP_NORESERVE;
#endif
    void *mapping=mmap(NULL,mapping_size,PROT_NONE,flags,-1,0);
    if(mapping==MAP_FAILED) {
        fprintf(stderr,"[address-region] virtual reservation failed size=0x%zx errno=%d\n",size,errno);
        free(r);return 0x1803; /* SDK address-space exhaustion */
    }
    uintptr_t address=((uintptr_t)mapping+page+alignment-1)&~(uintptr_t)(alignment-1);
    *r=(struct reservation){address,mapping,mapping_size,size,NULL};
    pthread_mutex_lock(&region_mutex);
    r->next=regions;regions=r;
    pthread_mutex_unlock(&region_mutex);
    *out=address;
    fprintf(stderr,"[address-region] reserved PROT_NONE address=0x%" PRIxPTR " size=0x%zx alignment=0x%zx\n",
            address,size,alignment);
    return 0;
}
uint32_t nso_host_free_address_region(uintptr_t address) {
    pthread_mutex_lock(&region_mutex);
    struct reservation **slot=&regions;
    while(*slot && (*slot)->address!=address)slot=&(*slot)->next;
    if(!*slot) {pthread_mutex_unlock(&region_mutex);return 0xe03;}
    struct reservation *r=*slot;
    /* The exact original mapping includes host guard/alignment padding.
     * This adapter never mprotects or touches the reserved interior. */
    if(munmap(r->mapping,r->mapping_size)) {
        int failure=errno;
        pthread_mutex_unlock(&region_mutex);
        fprintf(stderr,"[address-region] munmap of owned reservation failed errno=%d\n",failure);
        abort();
    }
    *slot=r->next;
    pthread_mutex_unlock(&region_mutex);
    fprintf(stderr,"[address-region] released address=0x%" PRIxPTR " size=0x%zx\n",address,r->requested_size);
    free(r);
    return 0;
}
