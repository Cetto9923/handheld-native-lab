#define _GNU_SOURCE
#include "guest_pages.h"
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
    struct page_allocation *next;
};
struct page_region {
    uintptr_t base;
    size_t bytes;
    int live, quarantined;
    struct page_allocation *allocations;
    struct page_region *next;
};
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
           !(address % 4096) && !(bytes % 4096) &&
           !(address % p->host_page) && !(bytes % p->host_page);
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
    for (struct page_allocation *a = r->allocations; a; a = a->next)
        if (overlaps(address, bytes, a->address, a->bytes)) {
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
    a->next = r->allocations; r->allocations = a;
    changed(p);
done:
    pthread_mutex_unlock(&p->mutex);
    return v;
}

struct guest_pages_result guest_pages_free_exact(guest_pages *p, uintptr_t address, size_t bytes) {
    if (!p) return result(GUEST_PAGES_INVALID);
    if (!valid_range(p, address, bytes)) return result(GUEST_PAGES_UNSUPPORTED);
    struct guest_pages_result v = result(GUEST_PAGES_OK);
    pthread_mutex_lock(&p->mutex);
    struct page_region *r = containing(p, address, bytes);
    if (!r) { v.status = GUEST_PAGES_NOT_OWNED; goto done; }
    if (r->quarantined) { v.status = GUEST_PAGES_QUARANTINED; goto done; }
    struct page_allocation **link = &r->allocations;
    while (*link && ((*link)->address != address || (*link)->bytes != bytes)) link = &(*link)->next;
    if (!*link) {
        v.status = GUEST_PAGES_NOT_OWNED;
        for (struct page_allocation *a = r->allocations; a; a = a->next)
            if (overlaps(address, bytes, a->address, a->bytes)) v.status = GUEST_PAGES_UNSUPPORTED;
        goto done;
    }
    struct page_allocation *a = *link;
    if (a->backed) {
        v.vm_attempted = 1;
        v.vm = nso_host_owned_pages_free(address, bytes);
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
    *link = a->next; drop_node(p,GUEST_METADATA_PAGE_ALLOCATION,a); changed(p);
done:
    pthread_mutex_unlock(&p->mutex);
    return v;
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
        for (struct page_allocation *a = r->allocations; a; a = a->next) a->backed = 0;
    }
    while (r->allocations) {
        struct page_allocation *a = r->allocations;
        v.ticket_cleanup = drop_ticket(a);
        if (v.ticket_cleanup.status != GUEST_CONTEXT_OK) {
            v.status = context_status(v.ticket_cleanup.status); return v;
        }
        r->allocations = a->next; drop_node(p,GUEST_METADATA_PAGE_ALLOCATION,a); changed(p);
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
        for (struct page_allocation *a = r->allocations; a; a = a->next) {
            ++u.metadata_allocations;
            if(!guest_metadata_is_owned(p->context,GUEST_METADATA_PAGE_ALLOCATION,a,sizeof(*a))) {u.ledger_valid=0;break;}
            if (!a->ticket || !valid_range(p,a->address,a->bytes) || a->address < r->base ||
                a->address - r->base >= r->bytes || a->bytes > r->bytes - (a->address - r->base) ||
                (!r->live && a->backed)) u.ledger_valid = 0;
            if (!add_size(&u.allocation_count,1) || !add_size(&u.ticket_count,1) ||
                !add_size(a->committed ? &u.charged_bytes : &u.pending_bytes,a->bytes)) u.ledger_valid = 0;
            if (a->backed && !add_size(&u.backing_bytes,a->bytes)) u.ledger_valid = 0;
            for (struct page_region *s = p->regions; s; s = s->next)
                for (struct page_allocation *b = s->allocations; b; b = b->next)
                    if (a != b && (a->ticket == b->ticket || (r == s && overlaps(a->address,a->bytes,b->address,b->bytes)))) u.ledger_valid = 0;
        }
    }
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
