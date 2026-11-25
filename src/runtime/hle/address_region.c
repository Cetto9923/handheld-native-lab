#define _GNU_SOURCE
#include "address_region.h"
#include "rb_tree.h"
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

struct accessible_interval {
    uintptr_t begin,end;
    struct hle_rb_node rb;
};
struct reservation {
    uintptr_t address;
    void *mapping;
    size_t mapping_size,requested_size;
    size_t mapped_guest_bytes;
    int quarantined;
    struct hle_rb_node *accessible;
    struct reservation *next;
};
_Static_assert(sizeof(struct accessible_interval)<=224,"accessible_interval exceeds metadata slab");
_Static_assert(sizeof(struct reservation)<=224,"reservation exceeds metadata slab");

static struct accessible_interval *iv_first(struct hle_rb_node *root) {
    struct hle_rb_node *n=hle_rb_first(root);
    return n?HLE_RB_ENTRY(n,struct accessible_interval,rb):NULL;
}

static struct accessible_interval *iv_next(struct accessible_interval *i) {
    struct hle_rb_node *n=hle_rb_next(&i->rb);
    return n?HLE_RB_ENTRY(n,struct accessible_interval,rb):NULL;
}

/* First disjoint interval with end > x in inorder traversal. */
static struct accessible_interval *iv_first_ending_after(struct hle_rb_node *root,uintptr_t x) {
    struct accessible_interval *best=NULL;
    struct hle_rb_node *cur=root;
    while(cur) {
        struct accessible_interval *i=HLE_RB_ENTRY(cur,struct accessible_interval,rb);
        if(i->end>x) {best=i;cur=cur->child[0];}
        else cur=cur->child[1];
    }
    return best;
}

static void iv_insert(struct hle_rb_node **root,struct accessible_interval *z) {
    struct hle_rb_node **link=root,*parent=NULL;
    while(*link) {
        struct accessible_interval *i=HLE_RB_ENTRY(*link,struct accessible_interval,rb);
        parent=*link;
        link=&parent->child[z->begin>=i->begin];
    }
    hle_rb_link_and_insert(root,parent,link,&z->rb);
}
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
    if(!page || page%4096 || address%4096 || size%4096)
        return page_result(NSO_OWNED_PAGE_UNSUPPORTED,NSO_OWNED_PAGE_DETAIL_ALIGNMENT,0,0);
    return page_result(NSO_OWNED_PAGE_OK,NSO_OWNED_PAGE_DETAIL_NONE,0,0);
}
static struct reservation *find_owner(uintptr_t address,size_t size) {
    for(struct reservation *r=regions;r;r=r->next)
        if(address>=r->address && address-r->address<r->requested_size &&
                size<=r->requested_size-(address-r->address))return r;
    return NULL;
}
static int accessible_outside(struct reservation *r,uintptr_t host_begin,uintptr_t host_end,
                              uintptr_t begin,uintptr_t end) {
    if(host_begin<begin) {
        uintptr_t head_end=host_end<begin?host_end:begin;
        struct accessible_interval *i=iv_first_ending_after(r->accessible,host_begin);
        if(i && i->begin<head_end)return 1;
    }
    if(end<host_end) {
        uintptr_t tail_begin=host_begin>end?host_begin:end;
        struct accessible_interval *i=iv_first_ending_after(r->accessible,tail_begin);
        if(i && i->begin<host_end)return 1;
    }
    return 0;
}
static void unshared_host_span(struct reservation *r,uintptr_t begin,uintptr_t end,
                              uintptr_t *host_begin,uintptr_t *host_end) {
    size_t page=host_page_size();
    /* Owned reservations include host-rounded padding. Only the two boundary
     * pages can contain live guest bytes outside this gapless/disjoint range. */
    *host_begin=begin&~(uintptr_t)(page-1);
    *host_end=(end+page-1)&~(uintptr_t)(page-1);
    /* Complete boundary host pages have no bytes outside the requested range. */
    if(*host_begin==begin && *host_end==end)return;
    if(accessible_outside(r,*host_begin,*host_begin+page,begin,end))*host_begin+=page;
    if(*host_begin<*host_end && accessible_outside(r,*host_end-page,*host_end,begin,end))
        *host_end-=page;
}
static void zero_shared_edges(uintptr_t begin,uintptr_t end,uintptr_t host_begin,uintptr_t host_end) {
    if(host_begin==host_end)memset((void *)begin,0,end-begin);
    else {
        if(begin<host_begin)memset((void *)begin,0,host_begin-begin);
        if(host_end<end)memset((void *)host_end,0,end-host_end);
    }
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
    struct hle_rb_node *interval;
    while((interval=hle_rb_pop_vine(&r->accessible))!=NULL)
        release_node(2,HLE_RB_ENTRY(interval,struct accessible_interval,rb));
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
        struct accessible_interval *hit=iv_first_ending_after(r->accessible,address);
        if(hit && hit->begin<end)
            result=page_result(NSO_OWNED_PAGE_UNSUPPORTED,NSO_OWNED_PAGE_DETAIL_OVERLAP,0,0);
        else {
            struct accessible_interval *node=allocate_node(2,sizeof(*node));
            uintptr_t host_begin,host_end;
            unshared_host_span(r,address,end,&host_begin,&host_end);
            if(!node)result=page_result(NSO_OWNED_PAGE_NO_MEMORY,NSO_OWNED_PAGE_DETAIL_NONE,ENOMEM,0);
            else if(host_begin<host_end && protect_owned((void *)host_begin,host_end-host_begin,PROT_READ|PROT_WRITE)) {
                int failure=errno,restore=0,restore_failed=0;
                /* An unsuccessful mprotect may already have changed a prefix.
                 * Restore only newly opened host pages, never a live neighbor. */
                if(protect_owned((void *)host_begin,host_end-host_begin,PROT_NONE)) {
                    restore=errno;restore_failed=1;r->quarantined=1;
                }
                release_node(2,node);
                result=page_result(restore_failed?NSO_OWNED_PAGE_QUARANTINED:NSO_OWNED_PAGE_SYSTEM_ERROR,
                                   NSO_OWNED_PAGE_DETAIL_NONE,failure,restore);
            } else {
                /* New anonymous host pages are already zero and stay lazy.
                 * Clear only new guest bytes sharing a live neighbor's page. */
                zero_shared_edges(address,end,host_begin,host_end);
                /* Owned containment plus disjointness bounds the sum by the
                 * requested reservation size, so this addition cannot wrap. */
                node->begin=address;
                node->end=end;
                iv_insert(&r->accessible,node);
                r->mapped_guest_bytes+=size;
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
        struct accessible_interval *first=iv_first_ending_after(r->accessible,address),*cursor=first;
        uintptr_t covered=address;
        while(cursor && cursor->begin<=covered && covered<end) {
            covered=cursor->end;
            cursor=iv_next(cursor);
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
                uintptr_t host_begin,host_end;
                unshared_host_span(r,address,end,&host_begin,&host_end);
                void *mapped=(void *)host_begin;
                if(host_begin<host_end)mapped=replace_owned_none((void *)host_begin,host_end-host_begin);
                if(mapped==MAP_FAILED || mapped!=(void *)host_begin) {
                    int failure=mapped==MAP_FAILED?errno:EFAULT;
                    /* A failed destructive replacement cannot prove old data
                     * survived. Do not publish the old complete count or guess
                     * a data-restoring rollback. Only full release is allowed. */
                    r->quarantined=1;release_node(2,right);
                    result=page_result(NSO_OWNED_PAGE_QUARANTINED,NSO_OWNED_PAGE_DETAIL_NONE,failure,0);
                } else {
                    zero_shared_edges(address,end,host_begin,host_end);
                    if(split) {
                        uintptr_t tail_end=first->end;
                        first->end=address;
                        right->begin=end;
                        right->end=tail_end;
                        iv_insert(&r->accessible,right);
                    } else {
                        struct accessible_interval *node=first;
                        if(node->begin<address) {
                            node->end=address;
                            node=iv_next(node);
                        }
                        while(node && node->begin<end) {
                            if(node->end>end) {node->begin=end;break;}
                            struct accessible_interval *next=iv_next(node);
                            hle_rb_erase(&r->accessible,&node->rb);
                            release_node(2,node);
                            node=next;
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
        for(struct accessible_interval *i=iv_first(r->accessible);i;i=iv_next(i)) {
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
