#define _GNU_SOURCE
#include "guest_pages.h"
#include "rb_tree.h"
#include <pthread.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

struct page_allocation {
    uintptr_t address;
    size_t bytes;
    guest_charge_ticket *ticket;
    int committed, backed;
    struct hle_rb_node rb;
};
struct page_region {
    uintptr_t base;
    size_t bytes;
    int live, quarantined;
    struct hle_rb_node *allocations;
    struct page_region *next;
};
_Static_assert(sizeof(struct page_allocation)<=224,"page_allocation exceeds metadata slab");
_Static_assert(sizeof(struct page_region)<=224,"page_region exceeds metadata slab");

static struct page_allocation *alloc_first(struct hle_rb_node *root) {
    struct hle_rb_node *n=hle_rb_first(root);
    return n?HLE_RB_ENTRY(n,struct page_allocation,rb):NULL;
}

static struct page_allocation *alloc_next(struct page_allocation *a) {
    struct hle_rb_node *n=hle_rb_next(&a->rb);
    return n?HLE_RB_ENTRY(n,struct page_allocation,rb):NULL;
}

/* First disjoint allocation with address + bytes > x in inorder traversal. */
static struct page_allocation *alloc_first_ending_after(struct hle_rb_node *root,uintptr_t x) {
    struct page_allocation *best=NULL;
    struct hle_rb_node *cur=root;
    while(cur) {
        struct page_allocation *a=HLE_RB_ENTRY(cur,struct page_allocation,rb);
        if(a->address+a->bytes>x) {best=a;cur=cur->child[0];}
        else cur=cur->child[1];
    }
    return best;
}

static void alloc_insert(struct hle_rb_node **root,struct page_allocation *z) {
    struct hle_rb_node **link=root,*parent=NULL;
    while(*link) {
        struct page_allocation *a=HLE_RB_ENTRY(*link,struct page_allocation,rb);
        parent=*link;
        link=&parent->child[z->address>=a->address];
    }
    hle_rb_link_and_insert(root,parent,link,&z->rb);
}
struct guest_pages {
    guest_context *context;
    pthread_mutex_t mutex;
    size_t host_page;
    uint64_t generation;
    struct page_region *regions;
};
static pthread_mutex_t owner_mutex = PTHREAD_MUTEX_INITIALIZER;
static guest_pages *active_owner;

static struct guest_pages_result result(enum guest_pages_status status) {
    struct guest_pages_result r = {0};
    r.status = status;
    return r;
}
static void changed(guest_pages *p) {
    if (p->generation != UINT64_MAX) ++p->generation;
}
static int add_size(size_t *total, size_t bytes) {
    if (bytes > SIZE_MAX - *total) return 0;
    *total += bytes;
    return 1;
}
static int valid_range(const guest_pages *p, uintptr_t address, size_t bytes) {
    return bytes && bytes <= UINTPTR_MAX - address &&
           p->host_page && !(address % 4096) && !(bytes % 4096);
}
static int overlaps(uintptr_t a, size_t an, uintptr_t b, size_t bn) {
    return a < b + bn && b < a + an;
}
static struct page_region *containing(guest_pages *p, uintptr_t address, size_t bytes) {
    for (struct page_region *r = p->regions; r; r = r->next)
        if (r->live && address >= r->base && address - r->base < r->bytes &&
            bytes <= r->bytes - (address - r->base)) return r;
    return NULL;
}
static enum guest_pages_status context_status(enum guest_context_status status) {
    if (status == GUEST_CONTEXT_LIMIT) return GUEST_PAGES_QUOTA;
    if (status == GUEST_CONTEXT_NO_MEMORY) return GUEST_PAGES_NO_MEMORY;
    if (status == GUEST_CONTEXT_QUARANTINED) return GUEST_PAGES_QUARANTINED;
    return GUEST_PAGES_CONTEXT_ERROR;
}
static enum guest_pages_status vm_status(enum nso_owned_page_status status) {
    if (status == NSO_OWNED_PAGE_QUARANTINED) return GUEST_PAGES_QUARANTINED;
    if (status == NSO_OWNED_PAGE_NO_MEMORY) return GUEST_PAGES_NO_MEMORY;
    if (status == NSO_OWNED_PAGE_NOT_OWNED) return GUEST_PAGES_INCONSISTENT;
    if (status == NSO_OWNED_PAGE_UNSUPPORTED) return GUEST_PAGES_UNSUPPORTED;
    return GUEST_PAGES_VM_ERROR;
}
static struct guest_context_result drop_ticket(struct page_allocation *a) {
    return a->committed ? guest_charge_release(a->ticket) : guest_charge_cancel(a->ticket);
}

static void *page_node(guest_pages *p,enum guest_metadata_kind kind,size_t bytes) {
    void *node=NULL;
    struct guest_context_result r=guest_metadata_allocate(p->context,kind,bytes,&node);
    if(r.status!=GUEST_CONTEXT_OK)errno=r.native_error?r.native_error:ENOMEM;
    return node;
}
static void drop_node(guest_pages *p,enum guest_metadata_kind kind,void *node) {
    struct guest_context_result r=guest_metadata_release(p->context,kind,node);
    if(r.status!=GUEST_CONTEXT_OK) {fprintf(stderr,"[metadata-fatal] node release status=%d errno=%d\n",r.status,r.native_error);abort();}
}
static void *raw_node_allocate(void *owner,unsigned kind,size_t bytes) {
    return page_node(owner,kind==1?GUEST_METADATA_VA_REGION:GUEST_METADATA_VA_INTERVAL,bytes);
}
static void raw_node_release(void *owner,unsigned kind,void *node) {
    drop_node(owner,kind==1?GUEST_METADATA_VA_REGION:GUEST_METADATA_VA_INTERVAL,node);
}
static int raw_node_owned(void *owner,unsigned kind,const void *node,size_t bytes) {
    guest_pages *p=owner;
    return guest_metadata_is_owned(p->context,kind==1?GUEST_METADATA_VA_REGION:GUEST_METADATA_VA_INTERVAL,node,bytes);
}
struct guest_pages_result guest_pages_create(guest_context *context, guest_pages **out) {
    if (!context || !out) return result(GUEST_PAGES_INVALID);
    struct guest_pages_result v = result(GUEST_PAGES_OK);
    *out = NULL;
    struct guest_context_usage cu;
    v.context = guest_context_usage(context, &cu);
    if (v.context.status != GUEST_CONTEXT_OK) {
        v.status = context_status(v.context.status);
        return v;
    }
    long page = sysconf(_SC_PAGESIZE);
    if (page <= 0 || ((size_t)page & ((size_t)page - 1))) return result(GUEST_PAGES_UNSUPPORTED);
    guest_pages *p = calloc(1, sizeof(*p));
    if (!p) return result(GUEST_PAGES_NO_MEMORY);
    p->context = context;
    p->host_page = (size_t)page;
    int e = pthread_mutex_init(&p->mutex, NULL);
    if (e) { free(p); v.status = GUEST_PAGES_VM_ERROR; v.native_error = e; return v; }
    pthread_mutex_lock(&owner_mutex);
    struct nso_owned_page_usage raw = {0};
    v.vm = nso_host_owned_pages_usage(&raw);
    if (active_owner || raw.region_count || v.vm.status != NSO_OWNED_PAGE_OK) {
        v.status = GUEST_PAGES_BUSY;
    } else {
        struct nso_region_allocator allocator={p,raw_node_allocate,raw_node_release,raw_node_owned};
        v.native_error=nso_host_region_allocator(&allocator);
        if(v.native_error)v.status=GUEST_PAGES_BUSY;
        else {active_owner = p;*out = p;}
    }
    pthread_mutex_unlock(&owner_mutex);
    if (v.status != GUEST_PAGES_OK) { pthread_mutex_destroy(&p->mutex); free(p); }
    return v;
}

struct guest_pages_result guest_pages_reserve(guest_pages *p, size_t bytes, uintptr_t *out) {
    if (!p || !out) return result(GUEST_PAGES_INVALID);
    *out = 0;
    if (!valid_range(p, 0, bytes)) return result(GUEST_PAGES_UNSUPPORTED);
    struct guest_pages_result v = result(GUEST_PAGES_OK);
    pthread_mutex_lock(&p->mutex);
    /* Allocator occupancy and ledger publication share the owner lock, so a
     * concurrent usage snapshot cannot observe an unpublished region node. */
    struct page_region *r = page_node(p,GUEST_METADATA_PAGE_REGION,sizeof(*r));
    if (!r) {v.status=GUEST_PAGES_NO_MEMORY;goto done;}
    /* A retired reservation can still own a failed-to-drop ticket. Finish that
     * release before reserving more VA, which could otherwise reuse its base. */
    for (struct page_region *s = p->regions; s; s = s->next)
        if (!s->live) { v.status = GUEST_PAGES_BUSY; drop_node(p,GUEST_METADATA_PAGE_REGION,r); goto done; }
    v.region_attempted = 1;
    v.region_result = nso_host_allocate_address_region(&r->base, bytes);
    if (v.region_result) { v.status = GUEST_PAGES_VM_ERROR; drop_node(p,GUEST_METADATA_PAGE_REGION,r); }
    else {
        r->bytes = bytes; r->live = 1;
        r->next = p->regions; p->regions = r;
        *out = r->base;
        changed(p);
    }
done:
    pthread_mutex_unlock(&p->mutex);
    return v;
}

struct guest_pages_result guest_pages_map(guest_pages *p, uintptr_t address, size_t bytes) {
    if (!p) return result(GUEST_PAGES_INVALID);
    if (!valid_range(p, address, bytes)) return result(GUEST_PAGES_UNSUPPORTED);
    struct guest_pages_result v = result(GUEST_PAGES_OK);
    pthread_mutex_lock(&p->mutex);
    struct page_region *r = containing(p, address, bytes);
    if (!r) { v.status = GUEST_PAGES_NOT_OWNED; goto done; }
    if (r->quarantined) { v.status = GUEST_PAGES_QUARANTINED; goto done; }
    struct page_allocation *hit = alloc_first_ending_after(r->allocations, address);
    if (hit && overlaps(address, bytes, hit->address, hit->bytes)) {
        v.status = GUEST_PAGES_OVERLAP; goto done;
    }
    struct page_allocation *a = page_node(p,GUEST_METADATA_PAGE_ALLOCATION,sizeof(*a));
    if (!a) { v.status = GUEST_PAGES_NO_MEMORY; goto done; }
    a->address = address; a->bytes = bytes;
    v.context = guest_charge_reserve(p->context, bytes, &a->ticket);
    if (v.context.status != GUEST_CONTEXT_OK) {
        v.status = context_status(v.context.status); drop_node(p,GUEST_METADATA_PAGE_ALLOCATION,a); goto done;
    }
    /* Owner lock serializes the ticket identity; the Context lock is released
     * before entering address_region and therefore before any real VM call. */
    v.vm_attempted = 1;
    v.vm = nso_host_owned_pages_map(address, bytes);
    if (v.vm.status == NSO_OWNED_PAGE_OK) {
        a->backed = 1;
        v.context = guest_charge_commit(a->ticket);
        if (v.context.status == GUEST_CONTEXT_OK) a->committed = 1;
        else {
            v.status = context_status(v.context.status);
            v.rollback_attempted = 1;
            v.rollback_vm = nso_host_owned_pages_free(address, bytes);
            if (v.rollback_vm.status == NSO_OWNED_PAGE_OK) a->backed = 0;
            else { r->quarantined = 1; v.status = GUEST_PAGES_QUARANTINED; }
        }
    } else {
        v.status = vm_status(v.vm.status);
        if (v.vm.status == NSO_OWNED_PAGE_QUARANTINED) r->quarantined = 1;
    }
    if (!a->committed && !a->backed && !r->quarantined) {
        /* Raw map failure with successful NONE restoration has no backing.
         * All other retained cases remain charged/pending until whole unmap. */
        v.ticket_cleanup = guest_charge_cancel(a->ticket);
        if (v.ticket_cleanup.status == GUEST_CONTEXT_OK) { drop_node(p,GUEST_METADATA_PAGE_ALLOCATION,a); goto done; }
        v.status = context_status(v.ticket_cleanup.status);
    }
    alloc_insert(&r->allocations, a);
    changed(p);
done:
    pthread_mutex_unlock(&p->mutex);
    return v;
}

static struct guest_pages_result free_node_locked(guest_pages *p, struct page_region *r,
                                                  struct page_allocation *a) {
    struct guest_pages_result v = result(GUEST_PAGES_OK);
    if (a->backed) {
        v.vm_attempted = 1;
        v.vm = nso_host_owned_pages_free(a->address, a->bytes);
        if (v.vm.status != NSO_OWNED_PAGE_OK) {
            r->quarantined = 1; changed(p);
            v.status = GUEST_PAGES_QUARANTINED; goto done;
        }
        a->backed = 0; changed(p);
    }
    v.ticket_cleanup = drop_ticket(a);
    if (v.ticket_cleanup.status != GUEST_CONTEXT_OK) {
        v.status = context_status(v.ticket_cleanup.status); goto done;
    }
    hle_rb_erase(&r->allocations, &a->rb);
    drop_node(p,GUEST_METADATA_PAGE_ALLOCATION,a); changed(p);
done:
    return v;
}

static struct guest_pages_result free_exact_locked(guest_pages *p,uintptr_t address,size_t bytes) {
    struct guest_pages_result v = result(GUEST_PAGES_OK);
    struct page_region *r = containing(p, address, bytes);
    if (!r) { v.status = GUEST_PAGES_NOT_OWNED; return v; }
    if (r->quarantined) { v.status = GUEST_PAGES_QUARANTINED; return v; }
    struct page_allocation *a = alloc_first_ending_after(r->allocations, address);
    if (!a || a->address != address || a->bytes != bytes) {
        v.status = (a && overlaps(address, bytes, a->address, a->bytes))
            ? GUEST_PAGES_UNSUPPORTED : GUEST_PAGES_NOT_OWNED;
        return v;
    }
    return free_node_locked(p, r, a);
}
struct guest_pages_result guest_pages_free_exact(guest_pages *p,uintptr_t address,size_t bytes) {
    if(!p)return result(GUEST_PAGES_INVALID);
    if(!valid_range(p,address,bytes))return result(GUEST_PAGES_UNSUPPORTED);
    pthread_mutex_lock(&p->mutex);
    struct guest_pages_result v=free_exact_locked(p,address,bytes);
    pthread_mutex_unlock(&p->mutex);return v;
}
/* Refine a live allocation at an aligned boundary without changing backing
 * or charged totals. New metadata is secured before publishing either half. */
static struct guest_pages_result split_at_locked(guest_pages *p,struct page_region *r,uintptr_t cut) {
    struct page_allocation *a=alloc_first_ending_after(r->allocations,cut);
    if(a && cut>a->address && cut-a->address<a->bytes){
        struct page_allocation *right=page_node(p,GUEST_METADATA_PAGE_ALLOCATION,sizeof(*right));
        if(!right)return result(GUEST_PAGES_NO_MEMORY);
        guest_charge_ticket *ticket=NULL;
        struct guest_context_result c=guest_charge_split(a->ticket,cut-a->address,&ticket);
        if(c.status!=GUEST_CONTEXT_OK){drop_node(p,GUEST_METADATA_PAGE_ALLOCATION,right);struct guest_pages_result v=result(context_status(c.status));v.context=c;return v;}
        right->address=cut;right->bytes=a->bytes-(cut-a->address);right->ticket=ticket;
        right->committed=a->committed;right->backed=a->backed;
        a->bytes=cut-a->address;alloc_insert(&r->allocations,right);changed(p);
    }
    return result(GUEST_PAGES_OK);
}
/* Preflight gapless owned backing under one lock, partition accounting at
 * the two range boundaries, then perform actual frees/refunds. A failed VM
 * operation quarantines the region with all remaining identities retained. */
struct guest_pages_result guest_pages_free_allocations(guest_pages *p,uintptr_t address,size_t bytes) {
    if(!p)return result(GUEST_PAGES_INVALID);
    if(!valid_range(p,address,bytes))return result(GUEST_PAGES_UNSUPPORTED);
    pthread_mutex_lock(&p->mutex);
    struct guest_pages_result v=result(GUEST_PAGES_OK);
    struct page_region *r=containing(p,address,bytes);
    if(!r){v.status=GUEST_PAGES_NOT_OWNED;goto done;}
    if(r->quarantined){v.status=GUEST_PAGES_QUARANTINED;goto done;}
    uintptr_t end=address+bytes;
    uintptr_t at=address;
    for(struct page_allocation *a=alloc_first_ending_after(r->allocations,address);at<end;a=alloc_next(a)){
        if(!a || at<a->address || at-a->address>=a->bytes || !a->committed || !a->backed){
            v.status=GUEST_PAGES_UNSUPPORTED;goto done;
        }
        uintptr_t next=a->address+a->bytes;at=next<end?next:end;
    }
    v=split_at_locked(p,r,address);if(v.status!=GUEST_PAGES_OK)goto done;
    v=split_at_locked(p,r,end);if(v.status!=GUEST_PAGES_OK)goto done;
    at=address;
    struct page_allocation *cur=alloc_first_ending_after(r->allocations,address);
    while(at<end){
        struct page_allocation *next=alloc_next(cur);
        size_t count=cur->bytes;
        v=free_node_locked(p,r,cur);
        if(v.status!=GUEST_PAGES_OK)goto done;
        at+=count;
        cur=next;
    }
done:
    pthread_mutex_unlock(&p->mutex);return v;
}

/* Called with owner lock. A successful munmap precedes every refund. Keep a
 * retired metadata record if a ticket operation fails, so retry never double
 * unmaps or loses its identity. The lower munmap failure still aborts. */
static struct guest_pages_result release_locked(guest_pages *p, struct page_region **link) {
    struct page_region *r = *link;
    struct guest_pages_result v = result(GUEST_PAGES_OK);
    if (r->live) {
        v.region_attempted = 1;
        v.region_result = nso_host_free_address_region(r->base);
        if (v.region_result) { v.status = GUEST_PAGES_VM_ERROR; return v; }
        r->live = 0; r->quarantined = 0; changed(p);
        for (struct page_allocation *a = alloc_first(r->allocations); a; a = alloc_next(a)) a->backed = 0;
    }
    while (r->allocations) {
        struct page_allocation *a = HLE_RB_ENTRY(r->allocations, struct page_allocation, rb);
        v.ticket_cleanup = drop_ticket(a);
        if (v.ticket_cleanup.status != GUEST_CONTEXT_OK) {
            v.status = context_status(v.ticket_cleanup.status); return v;
        }
        hle_rb_erase(&r->allocations, &a->rb);
        drop_node(p,GUEST_METADATA_PAGE_ALLOCATION,a); changed(p);
    }
    *link = r->next; drop_node(p,GUEST_METADATA_PAGE_REGION,r); changed(p);
    return v;
}
struct guest_pages_result guest_pages_release_region(guest_pages *p, uintptr_t base) {
    if (!p) return result(GUEST_PAGES_INVALID);
    pthread_mutex_lock(&p->mutex);
    struct page_region **link = &p->regions;
    while (*link && (*link)->base != base) link = &(*link)->next;
    struct guest_pages_result v = *link ? release_locked(p, link) : result(GUEST_PAGES_NOT_OWNED);
    pthread_mutex_unlock(&p->mutex);
    return v;
}

static int compare_ticket_ptr(const void *lhs, const void *rhs) {
    uintptr_t a = (uintptr_t)*(guest_charge_ticket *const *)lhs;
    uintptr_t b = (uintptr_t)*(guest_charge_ticket *const *)rhs;
    return (a > b) - (a < b);
}

static int has_duplicate_ticket(guest_pages *p, size_t count) {
    if (count < 2) return 0;
    guest_charge_ticket *stack_buf[64];
    guest_charge_ticket **buf = count <= 64 ? stack_buf : malloc(count * sizeof(*buf));
    if (!buf) {
        for (struct page_region *r = p->regions; r; r = r->next)
            for (struct page_allocation *a = alloc_first(r->allocations); a; a = alloc_next(a))
                for (struct page_region *s = p->regions; s; s = s->next)
                    for (struct page_allocation *b = alloc_first(s->allocations); b; b = alloc_next(b))
                        if (a != b && a->ticket == b->ticket) return 1;
        return 0;
    }
    size_t n = 0;
    for (struct page_region *r = p->regions; r; r = r->next)
        for (struct page_allocation *a = alloc_first(r->allocations); a; a = alloc_next(a))
            if (n < count) buf[n++] = a->ticket;
    qsort(buf, n, sizeof(*buf), compare_ticket_ptr);
    int dup = 0;
    for (size_t i = 1; i < n; i++)
        if (buf[i] == buf[i - 1]) { dup = 1; break; }
    if (buf != stack_buf) free(buf);
    return dup;
}

struct guest_pages_result guest_pages_usage(guest_pages *p, struct guest_pages_usage *out) {
    if (!p || !out) return result(GUEST_PAGES_INVALID);
    struct guest_pages_usage u = {0};
    struct guest_pages_result v = result(GUEST_PAGES_OK);
    pthread_mutex_lock(&p->mutex);
    u.generation = p->generation; u.backing_valid = u.ledger_valid = 1;
    for (struct page_region *r = p->regions; r; r = r->next) {
        ++u.metadata_regions;
        if(!guest_metadata_is_owned(p->context,GUEST_METADATA_PAGE_REGION,r,sizeof(*r))) {u.ledger_valid=0;break;}
        if (!valid_range(p, r->base, r->bytes)) u.ledger_valid = 0;
        for (struct page_region *s = r->next; s; s = s->next)
            if (r->base == s->base || (r->live && s->live && overlaps(r->base,r->bytes,s->base,s->bytes))) u.ledger_valid = 0;
        if (r->live && (!add_size(&u.region_count,1) || !add_size(&u.reserved_bytes,r->bytes))) u.ledger_valid = 0;
        if (r->quarantined) { ++u.quarantined_regions; u.backing_valid = 0; }
        struct page_allocation *prev = NULL;
        for (struct page_allocation *a = alloc_first(r->allocations); a; a = alloc_next(a)) {
            ++u.metadata_allocations;
            if(!guest_metadata_is_owned(p->context,GUEST_METADATA_PAGE_ALLOCATION,a,sizeof(*a))) {u.ledger_valid=0;break;}
            if (!a->ticket || !valid_range(p,a->address,a->bytes) || a->address < r->base ||
                a->address - r->base >= r->bytes || a->bytes > r->bytes - (a->address - r->base) ||
                (!r->live && a->backed)) u.ledger_valid = 0;
            if (prev && (a->address < prev->address || overlaps(prev->address,prev->bytes,a->address,a->bytes)))
                u.ledger_valid = 0;
            prev = a;
            if (!add_size(&u.allocation_count,1) || !add_size(&u.ticket_count,1) ||
                !add_size(a->committed ? &u.charged_bytes : &u.pending_bytes,a->bytes)) u.ledger_valid = 0;
            if (a->backed && !add_size(&u.backing_bytes,a->bytes)) u.ledger_valid = 0;
        }
    }
    if (u.ledger_valid && has_duplicate_ticket(p, u.allocation_count)) u.ledger_valid = 0;
    v.vm = nso_host_owned_pages_usage(&u.vm);
    v.context = guest_context_usage(p->context,&u.context);
    struct guest_context_result resource=guest_context_resource_usage(p->context,&u.resources);
    if(resource.status!=GUEST_CONTEXT_UNSUPPORTED && (resource.status!=GUEST_CONTEXT_OK || !u.resources.valid ||
        u.resources.counts[GUEST_METADATA_TICKET]!=u.context.tickets ||
        u.resources.counts[GUEST_METADATA_PAGE_REGION]!=u.metadata_regions ||
        u.resources.counts[GUEST_METADATA_PAGE_ALLOCATION]!=u.metadata_allocations ||
        u.resources.counts[GUEST_METADATA_VA_REGION]!=u.vm.region_count ||
        u.resources.counts[GUEST_METADATA_VA_INTERVAL]!=u.vm.interval_count))u.ledger_valid=0;
    if (u.vm.region_count != u.region_count || u.vm.reserved_guest_bytes != u.reserved_bytes ||
        u.vm.quarantined_regions > u.quarantined_regions ||
        u.context.pending_bytes < u.pending_bytes || u.context.charged_bytes < u.charged_bytes ||
        u.context.tickets < u.ticket_count || u.context.pending_bytes > u.context.quota_bytes ||
        u.context.charged_bytes > u.context.quota_bytes - u.context.pending_bytes) u.ledger_valid = 0;
    if (v.vm.status == NSO_OWNED_PAGE_QUARANTINED || !u.vm.mapped_guest_bytes_valid) u.backing_valid = 0;
    else if (v.vm.status != NSO_OWNED_PAGE_OK || u.backing_bytes != u.vm.mapped_guest_bytes) u.ledger_valid = 0;
    if (v.context.status != GUEST_CONTEXT_OK) {
        u.backing_valid = 0; v.status = context_status(v.context.status);
    }
    if (!u.ledger_valid) { u.backing_valid = 0; v.status = GUEST_PAGES_INCONSISTENT; }
    else if (!u.backing_valid && v.status == GUEST_PAGES_OK) v.status = GUEST_PAGES_QUARANTINED;
    if (!u.backing_valid) u.backing_bytes = 0;
    *out = u;
    pthread_mutex_unlock(&p->mutex);
    return v;
}

struct guest_pages_result guest_pages_destroy(guest_pages *p) {
    if (!p) return result(GUEST_PAGES_INVALID);
    pthread_mutex_lock(&p->mutex);
    struct guest_pages_result v = result(GUEST_PAGES_OK);
    while (p->regions) {
        v = release_locked(p,&p->regions);
        if (v.status != GUEST_PAGES_OK) { pthread_mutex_unlock(&p->mutex); return v; }
    }
    pthread_mutex_lock(&owner_mutex);
    if (active_owner != p) {
        pthread_mutex_unlock(&owner_mutex); pthread_mutex_unlock(&p->mutex);
        return result(GUEST_PAGES_INCONSISTENT);
    }
    v.native_error=nso_host_region_allocator(NULL);
    if(v.native_error) {
        pthread_mutex_unlock(&owner_mutex);pthread_mutex_unlock(&p->mutex);return result(GUEST_PAGES_BUSY);
    }
    active_owner = NULL;
    pthread_mutex_unlock(&owner_mutex);
    pthread_mutex_unlock(&p->mutex);
    pthread_mutex_destroy(&p->mutex);
    free(p);
    return v;
}
