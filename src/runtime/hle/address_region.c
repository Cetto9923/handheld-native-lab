#define _GNU_SOURCE
#include "address_region.h"
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

struct accessible_interval {
    uintptr_t begin,end;
    struct accessible_interval *next;
};
struct reservation {
    uintptr_t address;
    void *mapping;
    size_t mapping_size,requested_size;
    size_t mapped_guest_bytes;
    int quarantined;
    struct accessible_interval *accessible;
    struct reservation *next;
};
static pthread_mutex_t region_mutex=PTHREAD_MUTEX_INITIALIZER;
static struct reservation *regions;
static struct nso_region_allocator node_allocator;
int nso_host_region_allocator(const struct nso_region_allocator *allocator) {
    pthread_mutex_lock(&region_mutex);
    int error=regions?EBUSY:0;
    if(!error && allocator && (!allocator->allocate || !allocator->release || !allocator->owns))error=EINVAL;
    if(!error)node_allocator=allocator?*allocator:(struct nso_region_allocator){0};
    pthread_mutex_unlock(&region_mutex);return error;
}
static void *allocate_node(unsigned kind,size_t bytes) {
    return node_allocator.allocate?node_allocator.allocate(node_allocator.owner,kind,bytes):calloc(1,bytes);
}
static void release_node(unsigned kind,void *p) {
    if(!p)return;
    if(node_allocator.release)node_allocator.release(node_allocator.owner,kind,p);
    else free(p);
}

static size_t host_page_size(void) {
    long value=sysconf(_SC_PAGESIZE);
    if(value<=0 || ((size_t)value&((size_t)value-1)))return 0;
    return (size_t)value;
}
static int anonymous_flags(void) {
    int flags=MAP_PRIVATE|MAP_ANONYMOUS;
#ifdef MAP_NORESERVE
    flags|=MAP_NORESERVE;
#endif
    return flags;
}
static void *replace_none(void *address,size_t size) {
    /* Only called with a checked, host-page-aligned owned interior, under the
     * owner lock. Keep the VA reservation but discard the previous backing. */
    return mmap(address,size,PROT_NONE,anonymous_flags()|MAP_FIXED,-1,0);
}
#if defined(NSO_OWNED_PAGES_TESTING)
static int (*test_protect)(void *,size_t,int);
static void *(*test_replace_none)(void *,size_t);
void nso_host_owned_pages_test_vm(int (*protect)(void *,size_t,int),
                                void *(*replace)(void *,size_t)) {
    pthread_mutex_lock(&region_mutex);
    test_protect=protect;test_replace_none=replace;
    pthread_mutex_unlock(&region_mutex);
}
#endif
static int protect_owned(void *address,size_t size,int protection) {
#if defined(NSO_OWNED_PAGES_TESTING)
    if(test_protect)return test_protect(address,size,protection);
#endif
    return mprotect(address,size,protection);
}
static void *replace_owned_none(void *address,size_t size) {
#if defined(NSO_OWNED_PAGES_TESTING)
    if(test_replace_none)return test_replace_none(address,size);
#endif
    return replace_none(address,size);
}
static struct nso_owned_page_result page_result(enum nso_owned_page_status status,
        enum nso_owned_page_detail detail,int failure,int restore) {
    return (struct nso_owned_page_result){status,detail,failure,restore};
}
static struct nso_owned_page_result page_contract(uintptr_t address,size_t size) {
    if(!size || size>UINTPTR_MAX-address)
        return page_result(NSO_OWNED_PAGE_UNSUPPORTED,NSO_OWNED_PAGE_DETAIL_RANGE,0,0);
    size_t page=host_page_size();
    if(!page || address%4096 || size%4096 || address%page || size%page)
        return page_result(NSO_OWNED_PAGE_UNSUPPORTED,NSO_OWNED_PAGE_DETAIL_ALIGNMENT,0,0);
    return page_result(NSO_OWNED_PAGE_OK,NSO_OWNED_PAGE_DETAIL_NONE,0,0);
}
static struct reservation *find_owner(uintptr_t address,size_t size) {
    for(struct reservation *r=regions;r;r=r->next)
        if(address>=r->address && address-r->address<r->requested_size &&
                size<=r->requested_size-(address-r->address))return r;
    return NULL;
}
int nso_host_is_guest_memory(uintptr_t address) {
    if(!address)return 0;
    pthread_mutex_lock(&region_mutex);
    struct reservation *r=find_owner(address,1);
    pthread_mutex_unlock(&region_mutex);
    return r!=NULL;
}
uint32_t nso_host_allocate_address_region(uintptr_t *out,size_t size) {
    /* SDK VammManager +0x145da0: 64-KiB alignment below 2 MiB,
     * 2-MiB alignment otherwise, with gaps of at least 4 KiB.
     * Host page granularity may require a larger inaccessible guard. */
    if(!out || !size || size%4096) {
        fprintf(stderr,"[address-region] unsupported size/output contract size=0x%zx\n",size);
        abort();
    }
    size_t alignment=size<0x200000?0x10000:0x200000;
    size_t page=host_page_size();
    if(!page) {
        fprintf(stderr,"[address-region] unsupported host page size\n");
        abort();
    }
    if(size>SIZE_MAX-(page-1))return 0x1803;
    size_t rounded=(size+page-1)&~(page-1);
    if(rounded>SIZE_MAX-alignment-2*page)return 0x1803;
    size_t mapping_size=rounded+alignment+2*page;
    struct reservation *r=allocate_node(1,sizeof(*r));
    if(!r)return 0x1003; /* SDK metadata-node exhaustion */
    void *mapping=mmap(NULL,mapping_size,PROT_NONE,anonymous_flags(),-1,0);
    if(mapping==MAP_FAILED) {
        fprintf(stderr,"[address-region] virtual reservation failed size=0x%zx errno=%d\n",size,errno);
        release_node(1,r);return 0x1803; /* SDK address-space exhaustion */
    }
    uintptr_t address=((uintptr_t)mapping+page+alignment-1)&~(uintptr_t)(alignment-1);
    *r=(struct reservation){.address=address,.mapping=mapping,
        .mapping_size=mapping_size,.requested_size=size};
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
    /* The full mapping also covers all split/remapped accessible intervals,
     * guards and padding. Quarantine is released the same way, without access. */
    if(munmap(r->mapping,r->mapping_size)) {
        int failure=errno;
        pthread_mutex_unlock(&region_mutex);
        fprintf(stderr,"[address-region] munmap of owned reservation failed errno=%d\n",failure);
        abort();
    }
    *slot=r->next;
    struct accessible_interval *interval=r->accessible;
    while(interval) {
        struct accessible_interval *next=interval->next;
        release_node(2,interval);interval=next;
    }
    pthread_mutex_unlock(&region_mutex);
    fprintf(stderr,"[address-region] released address=0x%" PRIxPTR " size=0x%zx\n",address,r->requested_size);
    release_node(1,r);
    return 0;
}

struct nso_owned_page_result nso_host_owned_pages_map(uintptr_t address,size_t size) {
    struct nso_owned_page_result result=page_contract(address,size);
    if(result.status!=NSO_OWNED_PAGE_OK)return result;
    uintptr_t end=address+size;
    pthread_mutex_lock(&region_mutex);
    struct reservation *r=find_owner(address,size);
    if(!r)result=page_result(NSO_OWNED_PAGE_NOT_OWNED,NSO_OWNED_PAGE_DETAIL_NONE,0,0);
    else if(r->quarantined)result=page_result(NSO_OWNED_PAGE_QUARANTINED,NSO_OWNED_PAGE_DETAIL_NONE,0,0);
    else {
        struct accessible_interval **slot=&r->accessible;
        while(*slot && (*slot)->end<=address)slot=&(*slot)->next;
        if(*slot && (*slot)->begin<end)
            result=page_result(NSO_OWNED_PAGE_UNSUPPORTED,NSO_OWNED_PAGE_DETAIL_OVERLAP,0,0);
        else {
            struct accessible_interval *node=allocate_node(2,sizeof(*node));
            if(!node)result=page_result(NSO_OWNED_PAGE_NO_MEMORY,NSO_OWNED_PAGE_DETAIL_NONE,ENOMEM,0);
            else if(protect_owned((void *)address,size,PROT_READ|PROT_WRITE)) {
                int failure=errno,restore=0,restore_failed=0;
                /* An unsuccessful mprotect may already have changed a prefix.
                 * This span was entirely PROT_NONE, so restore that whole span. */
                if(protect_owned((void *)address,size,PROT_NONE)) {
                    restore=errno;restore_failed=1;r->quarantined=1;
                }
                release_node(2,node);
                result=page_result(restore_failed?NSO_OWNED_PAGE_QUARANTINED:NSO_OWNED_PAGE_SYSTEM_ERROR,
                                   NSO_OWNED_PAGE_DETAIL_NONE,failure,restore);
            } else {
                *node=(struct accessible_interval){address,end,*slot};
                /* Owned containment plus disjointness bounds the sum by the
                 * requested reservation size, so this addition cannot wrap. */
                *slot=node;r->mapped_guest_bytes+=size;
            }
        }
    }
    pthread_mutex_unlock(&region_mutex);
    return result;
}

struct nso_owned_page_result nso_host_owned_pages_free(uintptr_t address,size_t size) {
    struct nso_owned_page_result result=page_contract(address,size);
    if(result.status!=NSO_OWNED_PAGE_OK)return result;
    uintptr_t end=address+size;
    pthread_mutex_lock(&region_mutex);
    struct reservation *r=find_owner(address,size);
    if(!r)result=page_result(NSO_OWNED_PAGE_NOT_OWNED,NSO_OWNED_PAGE_DETAIL_NONE,0,0);
    else if(r->quarantined)result=page_result(NSO_OWNED_PAGE_QUARANTINED,NSO_OWNED_PAGE_DETAIL_NONE,0,0);
    else {
        struct accessible_interval **slot=&r->accessible;
        while(*slot && (*slot)->end<=address)slot=&(*slot)->next;
        struct accessible_interval *first=*slot,*cursor=first;
        uintptr_t covered=address;
        while(cursor && cursor->begin<=covered && covered<end) {
            covered=cursor->end;
            cursor=cursor->next;
        }
        if(covered<end)
            result=page_result(NSO_OWNED_PAGE_UNSUPPORTED,NSO_OWNED_PAGE_DETAIL_HOLES,0,0);
        else {
            /* Only a strict middle cut of one interval needs a new right node.
             * Allocate it before MAP_FIXED can discard any old data. */
            int split=first->begin<address && first->end>end;
            struct accessible_interval *right=split?allocate_node(2,sizeof(*right)):NULL;
            if(split && !right)
                result=page_result(NSO_OWNED_PAGE_NO_MEMORY,NSO_OWNED_PAGE_DETAIL_NONE,ENOMEM,0);
            else {
                void *mapped=replace_owned_none((void *)address,size);
                if(mapped==MAP_FAILED || mapped!=(void *)address) {
                    int failure=mapped==MAP_FAILED?errno:EFAULT;
                    /* A failed destructive replacement cannot prove old data
                     * survived. Do not publish the old complete count or guess
                     * a data-restoring rollback. Only full release is allowed. */
                    r->quarantined=1;release_node(2,right);
                    result=page_result(NSO_OWNED_PAGE_QUARANTINED,NSO_OWNED_PAGE_DETAIL_NONE,failure,0);
                } else {
                    if(split) {
                        *right=(struct accessible_interval){end,first->end,first->next};
                        first->end=address;first->next=right;
                    } else {
                        if(first->begin<address) {
                            first->end=address;slot=&first->next;
                        }
                        while(*slot && (*slot)->begin<end) {
                            struct accessible_interval *node=*slot;
                            if(node->end>end) {node->begin=end;break;}
                            *slot=node->next;release_node(2,node);
                        }
                    }
                    r->mapped_guest_bytes-=size;
                }
            }
        }
    }
    pthread_mutex_unlock(&region_mutex);
    return result;
}

struct nso_owned_page_result nso_host_owned_pages_usage(struct nso_owned_page_usage *out) {
    if(!out)return page_result(NSO_OWNED_PAGE_UNSUPPORTED,NSO_OWNED_PAGE_DETAIL_RANGE,0,0);
    *out=(struct nso_owned_page_usage){.mapped_guest_bytes_valid=1};
    struct nso_owned_page_result result=page_result(NSO_OWNED_PAGE_OK,NSO_OWNED_PAGE_DETAIL_NONE,0,0);
    pthread_mutex_lock(&region_mutex);
    for(struct reservation *r=regions;r;r=r->next) {
        if(node_allocator.owns && !node_allocator.owns(node_allocator.owner,1,r,sizeof(*r))) {
            out->mapped_guest_bytes_valid=0;result=page_result(NSO_OWNED_PAGE_QUARANTINED,NSO_OWNED_PAGE_DETAIL_NONE,EFAULT,0);break;
        }
        if(out->region_count==SIZE_MAX || r->requested_size>SIZE_MAX-out->reserved_guest_bytes ||
                r->mapped_guest_bytes>SIZE_MAX-out->mapped_guest_bytes) {
            result=page_result(NSO_OWNED_PAGE_UNSUPPORTED,NSO_OWNED_PAGE_DETAIL_OVERFLOW,0,0);
            out->mapped_guest_bytes_valid=0;break;
        }
        out->region_count++;out->reserved_guest_bytes+=r->requested_size;
        for(struct accessible_interval *i=r->accessible;i;i=i->next) {
            if(node_allocator.owns && !node_allocator.owns(node_allocator.owner,2,i,sizeof(*i))) {
                out->mapped_guest_bytes_valid=0;result=page_result(NSO_OWNED_PAGE_QUARANTINED,NSO_OWNED_PAGE_DETAIL_NONE,EFAULT,0);break;
            }
            if(out->interval_count==SIZE_MAX) {out->mapped_guest_bytes_valid=0;result=page_result(NSO_OWNED_PAGE_UNSUPPORTED,NSO_OWNED_PAGE_DETAIL_OVERFLOW,0,0);break;}
            out->interval_count++;
        }
        out->mapped_guest_bytes+=r->mapped_guest_bytes;
        if(r->quarantined) {
            out->quarantined_regions++;
            out->mapped_guest_bytes_valid=0;
            result=page_result(NSO_OWNED_PAGE_QUARANTINED,NSO_OWNED_PAGE_DETAIL_NONE,0,0);
        }
    }
    if(!out->mapped_guest_bytes_valid)out->mapped_guest_bytes=0;
    pthread_mutex_unlock(&region_mutex);
    return result;
}
