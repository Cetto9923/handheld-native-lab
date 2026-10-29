#define _GNU_SOURCE
#include "guest_context.h"
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

struct guest_charge_ticket {
    guest_context *owner;
    size_t bytes;
    int committed;
};
struct guest_context {
    pthread_mutex_t mutex;
    size_t quota,pending,charged,tickets,stack_bytes,page;
    int primary_active,quarantined,join_pending;
    size_t capacity_shortfall;
    int capacity_error,capacity_unsupported;
    unsigned memory_info_valid_fields;
    struct guest_primary_stack primary;
    void *retained_mapping;
    size_t retained_size;
    guest_charge_ticket *retained_ticket;
    void *resource_mapping;
    size_t resource_bytes,resource_next,resource_free,resource_blocks;
    size_t resource_counts[GUEST_METADATA_KIND_END];
#if defined(GUEST_CONTEXT_TESTING)
    int fail_next_map;
    size_t pending_before_map;
    size_t test_shortfall;
    unsigned test_reports,test_attempts;
    int test_query_error;
#endif
};
static struct guest_context_result result(enum guest_context_status status,int error,int cleanup) {
    return (struct guest_context_result){status,error,cleanup};
}
static struct guest_context_result success(void) {return result(GUEST_CONTEXT_OK,0,0);}

enum {RESOURCE_BLOCK_BYTES=256};
struct resource_block {size_t next,bytes;uint32_t kind,live;uint64_t marker;};
_Static_assert(sizeof(struct resource_block)==32,"aligned metadata block header");
#define RESOURCE_MARKER UINT64_C(0x5245534f55524345)
#if defined(GUEST_RESOURCE_TESTING)
static _Thread_local void *(*resource_test_map)(size_t);
static _Thread_local int (*resource_test_unmap)(void *,size_t);
void guest_context_resource_test_vm(void *(*map)(size_t),int (*unmap)(void *,size_t)) {
    resource_test_map=map;resource_test_unmap=unmap;
}
#endif
static void *map_resource(size_t bytes) {
#if defined(GUEST_RESOURCE_TESTING)
    if(resource_test_map)return resource_test_map(bytes);
#endif
    return mmap(NULL,bytes,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
}
static int unmap_resource(void *p,size_t bytes) {
#if defined(GUEST_RESOURCE_TESTING)
    if(resource_test_unmap)return resource_test_unmap(p,bytes);
#endif
    return munmap(p,bytes);
}
/* Context lock is terminal: never calls pages/region/Process. */
static struct guest_context_result metadata_allocate_locked(guest_context *c,
        enum guest_metadata_kind kind,size_t bytes,void **out) {
    *out=NULL;
    if(kind<GUEST_METADATA_TICKET || kind>=GUEST_METADATA_KIND_END || !bytes ||
       bytes>RESOURCE_BLOCK_BYTES-sizeof(struct resource_block))return result(GUEST_CONTEXT_UNSUPPORTED,EINVAL,0);
    if(c->quarantined)return result(GUEST_CONTEXT_QUARANTINED,0,0);
    if(!c->resource_mapping) {
        *out=calloc(1,bytes);return *out?success():result(GUEST_CONTEXT_NO_MEMORY,ENOMEM,0);
    }
    size_t index;
    if(c->resource_free) {
        index=c->resource_free-1;
        if(index>=c->resource_next)goto invalid;
        struct resource_block *h=(void *)((char *)c->resource_mapping+index*RESOURCE_BLOCK_BYTES);
        if(h->live || h->kind || h->marker!=RESOURCE_MARKER)goto invalid;
        c->resource_free=h->next;
    } else {
        if(c->resource_next>=c->resource_bytes/RESOURCE_BLOCK_BYTES)return result(GUEST_CONTEXT_LIMIT,ENOSPC,0);
        index=c->resource_next++;
    }
    struct resource_block *h=(void *)((char *)c->resource_mapping+index*RESOURCE_BLOCK_BYTES);
    *h=(struct resource_block){.bytes=bytes,.kind=(uint32_t)kind,.live=1,.marker=RESOURCE_MARKER};
    c->resource_blocks++;c->resource_counts[kind]++;
    *out=h+1;memset(*out,0,bytes);return success();
invalid:
    c->quarantined=1;return result(GUEST_CONTEXT_QUARANTINED,EFAULT,0);
}
static struct guest_context_result metadata_release_locked(guest_context *c,
        enum guest_metadata_kind kind,void *pointer) {
    if(!pointer || kind<GUEST_METADATA_TICKET || kind>=GUEST_METADATA_KIND_END)return result(GUEST_CONTEXT_INVALID,EINVAL,0);
    if(!c->resource_mapping) {free(pointer);return success();}
    uintptr_t base=(uintptr_t)c->resource_mapping,address=(uintptr_t)pointer;
    if(address<base+sizeof(struct resource_block))goto invalid;
    size_t delta=address-base-sizeof(struct resource_block);
    if(delta%RESOURCE_BLOCK_BYTES || delta/RESOURCE_BLOCK_BYTES>=c->resource_next)goto invalid;
    struct resource_block *h=(void *)((char *)c->resource_mapping+delta);
    if(!h->live || h->kind!=(uint32_t)kind || h->marker!=RESOURCE_MARKER ||
       !c->resource_blocks || !c->resource_counts[kind])goto invalid;
    h->live=0;h->kind=0;h->bytes=0;h->next=c->resource_free;
    c->resource_free=delta/RESOURCE_BLOCK_BYTES+1;
    c->resource_blocks--;c->resource_counts[kind]--;return success();
invalid:
    c->quarantined=1;return result(GUEST_CONTEXT_QUARANTINED,EFAULT,0);
}
struct guest_context_result guest_metadata_allocate(guest_context *c,enum guest_metadata_kind k,size_t n,void **out) {
    if(!c || !out)return result(GUEST_CONTEXT_INVALID,EINVAL,0);
    pthread_mutex_lock(&c->mutex);struct guest_context_result r=metadata_allocate_locked(c,k,n,out);
    pthread_mutex_unlock(&c->mutex);return r;
}
struct guest_context_result guest_metadata_release(guest_context *c,enum guest_metadata_kind k,void *p) {
    if(!c)return result(GUEST_CONTEXT_INVALID,EINVAL,0);
    pthread_mutex_lock(&c->mutex);struct guest_context_result r=metadata_release_locked(c,k,p);
    pthread_mutex_unlock(&c->mutex);return r;
}
int guest_metadata_is_owned(guest_context *c,enum guest_metadata_kind kind,const void *pointer,size_t bytes) {
    if(!c || !pointer)return 0;
    pthread_mutex_lock(&c->mutex);int valid=1;
    if(c->resource_mapping) {
        uintptr_t base=(uintptr_t)c->resource_mapping,address=(uintptr_t)pointer;
        valid=address>=base+sizeof(struct resource_block);
        if(valid) {
            size_t delta=address-base-sizeof(struct resource_block);
            valid=!(delta%RESOURCE_BLOCK_BYTES) && delta/RESOURCE_BLOCK_BYTES<c->resource_next;
            if(valid) {
                const struct resource_block *h=(const void *)((const char *)c->resource_mapping+delta);
                valid=h->marker==RESOURCE_MARKER && h->live && h->kind==(uint32_t)kind && h->bytes==bytes;
            }
        }
    }
    pthread_mutex_unlock(&c->mutex);return valid;
}
struct guest_context_result guest_context_resource_usage(guest_context *c,struct guest_resource_usage *out) {
    if(!c || !out)return result(GUEST_CONTEXT_INVALID,EINVAL,0);
    pthread_mutex_lock(&c->mutex);
    struct guest_resource_usage u={.total_bytes=c->resource_bytes,.valid=1};
    struct guest_context_result r=success();
    if(!c->resource_mapping) {r=result(GUEST_CONTEXT_UNSUPPORTED,0,0);u.valid=0;goto done;}
    if(c->quarantined || c->resource_next>c->resource_bytes/RESOURCE_BLOCK_BYTES)goto invalid;
    size_t ticket_pending=0,ticket_charged=0;
    for(size_t i=0;i<c->resource_next;i++) {
        struct resource_block *h=(void *)((char *)c->resource_mapping+i*RESOURCE_BLOCK_BYTES);
        if(h->marker!=RESOURCE_MARKER)goto invalid;
        if(h->live) {
            if(h->kind<GUEST_METADATA_TICKET || h->kind>=GUEST_METADATA_KIND_END ||
               !h->bytes || h->bytes>RESOURCE_BLOCK_BYTES-sizeof(*h))goto invalid;
            u.blocks++;u.counts[h->kind]++;
            if(h->kind==GUEST_METADATA_TICKET) {
                guest_charge_ticket *t=(void *)(h+1);
                if(h->bytes!=sizeof(*t) || t->owner!=c || (t->committed!=0 && t->committed!=1))goto invalid;
                size_t *sum=t->committed?&ticket_charged:&ticket_pending;
                if(t->bytes>SIZE_MAX-*sum)goto invalid;
                *sum+=t->bytes;
            }
        } else if(h->kind || h->bytes)goto invalid;
    }
    size_t free_count=0,next=c->resource_free;
    while(next) {
        if(++free_count>c->resource_next || next>c->resource_next)goto invalid;
        struct resource_block *h=(void *)((char *)c->resource_mapping+(next-1)*RESOURCE_BLOCK_BYTES);
        if(h->live || h->kind || h->marker!=RESOURCE_MARKER)goto invalid;
        next=h->next;
    }
    if(ticket_pending!=c->pending || ticket_charged!=c->charged || u.counts[GUEST_METADATA_TICKET]!=c->tickets ||
       u.blocks!=c->resource_blocks || free_count!=c->resource_next-u.blocks ||
       memcmp(u.counts,c->resource_counts,sizeof(u.counts)))goto invalid;
    u.used_bytes=u.blocks*RESOURCE_BLOCK_BYTES;goto done;
invalid:
    u.valid=0;c->quarantined=1;r=result(GUEST_CONTEXT_QUARANTINED,EFAULT,0);
done:
    *out=u;pthread_mutex_unlock(&c->mutex);return r;
}
static struct guest_context_result create_context(const struct guest_context_config *config,
        size_t resource_bytes,guest_context **out) {
    if(!out)return result(GUEST_CONTEXT_INVALID,0,0);
    *out=NULL;
    long page=sysconf(_SC_PAGESIZE);
    if(!config || !config->quota_bytes || !config->primary_stack_bytes || page<=0 ||
            ((size_t)page&((size_t)page-1)) || config->primary_stack_bytes%(size_t)page ||
            config->primary_stack_bytes<(size_t)PTHREAD_STACK_MIN ||
            (size_t)page>SIZE_MAX/2 || config->primary_stack_bytes>SIZE_MAX-2*(size_t)page)
        return result(GUEST_CONTEXT_INVALID,0,0);
    guest_context *context=calloc(1,sizeof(*context));
    if(!context)return result(GUEST_CONTEXT_NO_MEMORY,ENOMEM,0);
    int error=pthread_mutex_init(&context->mutex,NULL);
    if(error) {free(context);return result(GUEST_CONTEXT_SYSTEM_ERROR,error,0);}
    context->quota=config->quota_bytes;context->stack_bytes=config->primary_stack_bytes;
    context->page=(size_t)page;
    if(resource_bytes) {
        if(resource_bytes%(size_t)page || resource_bytes%RESOURCE_BLOCK_BYTES) {
            pthread_mutex_destroy(&context->mutex);free(context);return result(GUEST_CONTEXT_INVALID,EINVAL,0);
        }
        context->resource_mapping=map_resource(resource_bytes);
        if(context->resource_mapping==MAP_FAILED) {
            int e=errno;pthread_mutex_destroy(&context->mutex);free(context);return result(GUEST_CONTEXT_SYSTEM_ERROR,e,0);
        }
        context->resource_bytes=resource_bytes;
    }
    *out=context;
    return success();
}
struct guest_context_result guest_context_create(const struct guest_context_config *config,guest_context **out) {
    return create_context(config,0,out);
}
struct guest_context_result guest_context_create_with_resources(const struct guest_context_config *config,size_t bytes,guest_context **out) {
    if(!bytes) {if(out)*out=NULL;return result(GUEST_CONTEXT_INVALID,EINVAL,0);}
    return create_context(config,bytes,out);
}
struct guest_context_result guest_charge_reserve(guest_context *context,size_t bytes,guest_charge_ticket **out) {
    if(!out)return result(GUEST_CONTEXT_INVALID,0,0);
    *out=NULL;
    if(!context || !bytes)return result(GUEST_CONTEXT_INVALID,0,0);
    guest_charge_ticket *ticket=NULL;
    pthread_mutex_lock(&context->mutex);
    struct guest_context_result status=success();
    if(context->quarantined)status=result(GUEST_CONTEXT_QUARANTINED,0,0);
    else if(context->charged>context->quota || context->pending>context->quota-context->charged ||
            bytes>context->quota-context->charged-context->pending)
        status=result(GUEST_CONTEXT_LIMIT,0,0);
    else if((status=metadata_allocate_locked(context,GUEST_METADATA_TICKET,sizeof(*ticket),(void **)&ticket)).status==GUEST_CONTEXT_OK) {
        context->pending+=bytes;context->tickets++;
        *ticket=(guest_charge_ticket){context,bytes,0};*out=ticket;
    }
    pthread_mutex_unlock(&context->mutex);
    return status;
}
/* Partition an existing charge without reserving/refunding backing bytes.
 * Allocate metadata first; failures leave the original charge unchanged. */
struct guest_context_result guest_charge_split(guest_charge_ticket *ticket,size_t left_bytes,guest_charge_ticket **right) {
    if(!ticket || !right)return result(GUEST_CONTEXT_INVALID,0,0);
    *right=NULL;guest_context *context=ticket->owner;
    pthread_mutex_lock(&context->mutex);
    struct guest_context_result status=success();guest_charge_ticket *part=NULL;
    if(context->quarantined)status=result(GUEST_CONTEXT_QUARANTINED,0,0);
    else if(!left_bytes || left_bytes>=ticket->bytes)status=result(GUEST_CONTEXT_INVALID,0,0);
    else if((status=metadata_allocate_locked(context,GUEST_METADATA_TICKET,sizeof(*part),(void**)&part)).status==GUEST_CONTEXT_OK){
        *part=(guest_charge_ticket){context,ticket->bytes-left_bytes,ticket->committed};
        ticket->bytes=left_bytes;context->tickets++;*right=part;
    }
    pthread_mutex_unlock(&context->mutex);return status;
}
struct guest_context_result guest_charge_commit(guest_charge_ticket *ticket) {
    if(!ticket)return result(GUEST_CONTEXT_INVALID,0,0);
    guest_context *context=ticket->owner;
    pthread_mutex_lock(&context->mutex);
    struct guest_context_result status=success();
    if(context->quarantined)status=result(GUEST_CONTEXT_QUARANTINED,0,0);
    else if(ticket->committed)status=result(GUEST_CONTEXT_BAD_STATE,0,0);
    else {
        context->pending-=ticket->bytes;context->charged+=ticket->bytes;
        ticket->committed=1;
    }
    pthread_mutex_unlock(&context->mutex);
    return status;
}
static struct guest_context_result drop_ticket(guest_charge_ticket *ticket,int committed) {
    if(!ticket)return result(GUEST_CONTEXT_INVALID,0,0);
    guest_context *context=ticket->owner;
    pthread_mutex_lock(&context->mutex);
    if(ticket->committed!=committed) {
        pthread_mutex_unlock(&context->mutex);return result(GUEST_CONTEXT_BAD_STATE,0,0);
    }
    if(committed)context->charged-=ticket->bytes;
    else context->pending-=ticket->bytes;
    context->tickets--;
    struct guest_context_result released=metadata_release_locked(context,GUEST_METADATA_TICKET,ticket);
    pthread_mutex_unlock(&context->mutex);
    return released;
}
struct guest_context_result guest_charge_cancel(guest_charge_ticket *ticket) {return drop_ticket(ticket,0);}
struct guest_context_result guest_charge_release(guest_charge_ticket *ticket) {return drop_ticket(ticket,1);}
struct guest_context_result guest_context_usage(guest_context *context,struct guest_context_usage *out) {
    if(!context || !out)return result(GUEST_CONTEXT_INVALID,0,0);
    pthread_mutex_lock(&context->mutex);
    *out=(struct guest_context_usage){context->quota,context->pending,context->charged,context->tickets,
        context->primary_active,context->quarantined,context->memory_info_valid_fields};
    struct guest_context_result status=context->quarantined?result(GUEST_CONTEXT_QUARANTINED,0,0):success();
    pthread_mutex_unlock(&context->mutex);return status;
}
struct guest_context_result guest_context_primary_stack(guest_context *context,struct guest_primary_stack *out) {
    if(!context || !out)return result(GUEST_CONTEXT_INVALID,0,0);
    pthread_mutex_lock(&context->mutex);
    struct guest_context_result status=success();
    if(context->quarantined)status=result(GUEST_CONTEXT_QUARANTINED,0,0);
    else if(!context->primary_active || !context->primary.address || !context->primary.native_bytes)status=result(GUEST_CONTEXT_BAD_STATE,0,0);
    else *out=context->primary;
    pthread_mutex_unlock(&context->mutex);return status;
}
#if defined(GUEST_CONTEXT_TESTING)
void guest_context_test_fail_next_map(guest_context *context,int error) {
    pthread_mutex_lock(&context->mutex);context->fail_next_map=error;
    pthread_mutex_unlock(&context->mutex);
}
size_t guest_context_test_pending_before_map(guest_context *context) {
    pthread_mutex_lock(&context->mutex);size_t pending=context->pending_before_map;
    pthread_mutex_unlock(&context->mutex);return pending;
}
void guest_context_test_capacity(guest_context *context,size_t shortfall,unsigned reports,int query_error) {
    pthread_mutex_lock(&context->mutex);
    context->test_shortfall=shortfall;context->test_reports=reports;
    context->test_query_error=query_error;context->test_attempts=0;
    pthread_mutex_unlock(&context->mutex);
}
unsigned guest_context_test_capacity_attempts(guest_context *context) {
    pthread_mutex_lock(&context->mutex);unsigned attempts=context->test_attempts;
    pthread_mutex_unlock(&context->mutex);return attempts;
}
#endif
struct invocation {guest_context *context;guest_primary_callback callback;void *arg;};
static void *invoke(void *opaque) {
    struct invocation call=*(struct invocation *)opaque;
    free(opaque);
    guest_context *context=call.context;
    uintptr_t native=0;size_t bytes=0;int error=0;
#if defined(__APPLE__)
    bytes=pthread_get_stacksize_np(pthread_self());
    uintptr_t top=(uintptr_t)pthread_get_stackaddr_np(pthread_self());
    if(top>=bytes)native=top-bytes;
#elif defined(__linux__)
    pthread_attr_t attributes;
    error=pthread_getattr_np(pthread_self(),&attributes);
    if(!error) {
        void *address;
        error=pthread_attr_getstack(&attributes,&address,&bytes);
        int cleanup=pthread_attr_destroy(&attributes);
        if(!error) {native=(uintptr_t)address;error=cleanup;}
    }
#else
    error=ENOTSUP;
#endif
    uintptr_t sp;
#if defined(__aarch64__)
    __asm__ volatile("mov %0, sp":"=r"(sp));
#else
#error "owned primary capacity requires the selected AArch64 SP contract"
#endif
    pthread_mutex_lock(&context->mutex);
#if defined(GUEST_CONTEXT_TESTING)
    context->test_attempts++;
    if(context->test_reports) {
        context->test_reports--;
        if(context->test_query_error)error=context->test_query_error;
        else bytes=context->test_shortfall<context->stack_bytes?context->stack_bytes-context->test_shortfall:0;
    }
#endif
    struct guest_primary_stack *stack=&context->primary;
    uintptr_t raw=stack->allocation_address;
    if(error)context->capacity_error=error;
    else if(!native || !bytes || native<raw || native-raw>stack->allocation_bytes ||
            bytes>stack->allocation_bytes-(native-raw))context->capacity_unsupported=1;
    else if(bytes<context->stack_bytes)context->capacity_shortfall=context->stack_bytes-bytes;
    else {
        uintptr_t top=native+bytes,logical=top-context->stack_bytes;
        uintptr_t local=(uintptr_t)&call;
        if(sp<logical || sp>=top || local<logical || local>=top)context->capacity_unsupported=1;
        else {
            stack->native_address=native;stack->native_bytes=bytes;
            stack->address=logical;
        }
    }
    int ready=!context->capacity_error && !context->capacity_shortfall && !context->capacity_unsupported;
    size_t shortfall=context->capacity_shortfall;
    unsigned attempt=stack->capacity_attempts;
    size_t requested=stack->bytes,raw_bytes=stack->allocation_bytes,admin=stack->administrative_bytes;
    pthread_mutex_unlock(&context->mutex);
    fprintf(stderr,"[stack-capacity] attempt=%u logical=%zu raw_rw=%zu native_usable=%zu admin=%zu shortfall=%zu query_error=%d callback=%s\n",
        attempt,requested,raw_bytes,bytes,admin,shortfall,error,ready?"ready":"withheld");
    if(!ready)return NULL; /* No guest callback/ThreadRecord/Query on probes. */
    return call.callback(call.context,call.arg);
}
static struct guest_context_result finish_stack(guest_context *context,guest_charge_ticket *ticket,
        void *mapping,size_t bytes,int committed,struct guest_context_result status,int retry) {
    if(mapping && munmap(mapping,bytes)) {
        int cleanup=errno;
        pthread_mutex_lock(&context->mutex);
        context->retained_mapping=mapping;context->retained_size=bytes;context->retained_ticket=ticket;
        context->quarantined=1;context->primary_active=0;
        pthread_mutex_unlock(&context->mutex);
        return result(GUEST_CONTEXT_QUARANTINED,status.native_error,cleanup);
    }
    if(ticket) {
        if(committed)guest_charge_release(ticket);else guest_charge_cancel(ticket);
    }
    pthread_mutex_lock(&context->mutex);
    context->primary=(struct guest_primary_stack){0};context->primary_active=retry;
    pthread_mutex_unlock(&context->mutex);return status;
}
struct guest_context_result guest_context_run_primary(guest_context *context,guest_primary_callback callback,
        void *arg,void **value) {
    if(!context || !callback || !value)return result(GUEST_CONTEXT_INVALID,0,0);
    *value=NULL;
    pthread_mutex_lock(&context->mutex);
    if(context->primary_active || context->quarantined) {
        enum guest_context_status state=context->quarantined?GUEST_CONTEXT_QUARANTINED:GUEST_CONTEXT_BUSY;
        pthread_mutex_unlock(&context->mutex);return result(state,0,0);
    }
    context->primary_active=1;
    pthread_mutex_unlock(&context->mutex);
    size_t admin=0;
    /* Explicit native overhead ceiling, not a guessed pthread layout size. */
    size_t max_admin=context->page<=SIZE_MAX/4?4*context->page:0;
    for(unsigned attempt=1;attempt<=3;attempt++) {
        if(!max_admin || admin>max_admin || admin>SIZE_MAX-2*context->page-context->stack_bytes)
            return finish_stack(context,NULL,NULL,0,0,result(GUEST_CONTEXT_UNSUPPORTED,0,0),0);
        pthread_mutex_lock(&context->mutex);
        context->capacity_shortfall=0;context->capacity_error=0;context->capacity_unsupported=0;
        pthread_mutex_unlock(&context->mutex);
        struct invocation *call=malloc(sizeof(*call));
        if(!call)return finish_stack(context,NULL,NULL,0,0,result(GUEST_CONTEXT_NO_MEMORY,ENOMEM,0),0);
        *call=(struct invocation){context,callback,arg};
        guest_charge_ticket *ticket=NULL;
        struct guest_context_result status=guest_charge_reserve(context,context->stack_bytes,&ticket);
        if(status.status!=GUEST_CONTEXT_OK) {free(call);return finish_stack(context,NULL,NULL,0,0,status,0);}
        size_t raw_bytes=context->stack_bytes+admin,size=raw_bytes+2*context->page;
        void *mapping;
#if defined(GUEST_CONTEXT_TESTING)
        pthread_mutex_lock(&context->mutex);
        int injected=context->fail_next_map;context->fail_next_map=0;
        context->pending_before_map=context->pending;
        pthread_mutex_unlock(&context->mutex);
        if(injected) {errno=injected;mapping=MAP_FAILED;}
        else
#endif
        mapping=mmap(NULL,size,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        if(mapping==MAP_FAILED) {
            int error=errno;free(call);
            return finish_stack(context,ticket,NULL,0,0,result(GUEST_CONTEXT_SYSTEM_ERROR,error,0),0);
        }
        void *stack=(char *)mapping+context->page;
        if(mprotect(stack,raw_bytes,PROT_READ|PROT_WRITE)) {
            int error=errno;free(call);
            return finish_stack(context,ticket,mapping,size,0,result(GUEST_CONTEXT_SYSTEM_ERROR,error,0),0);
        }
        status=guest_charge_commit(ticket);
        if(status.status!=GUEST_CONTEXT_OK) {free(call);return finish_stack(context,ticket,mapping,size,0,status,0);}
        pthread_mutex_lock(&context->mutex);
        context->primary=(struct guest_primary_stack){.bytes=context->stack_bytes,.guard_bytes=context->page,
            .allocation_address=(uintptr_t)stack,.allocation_bytes=raw_bytes,
            .administrative_bytes=admin,.capacity_attempts=attempt};
        pthread_mutex_unlock(&context->mutex);
        pthread_attr_t attributes;
        int error=pthread_attr_init(&attributes);
        if(error) {free(call);return finish_stack(context,ticket,mapping,size,1,result(GUEST_CONTEXT_SYSTEM_ERROR,error,0),0);}
        error=pthread_attr_setstack(&attributes,stack,raw_bytes);
        if(!error)error=pthread_attr_setguardsize(&attributes,0); /* Explicit owned guards. */
        pthread_t thread;
        if(!error)error=pthread_create(&thread,&attributes,invoke,call);
        int attr_error=pthread_attr_destroy(&attributes);
        if(error) {free(call);return finish_stack(context,ticket,mapping,size,1,result(GUEST_CONTEXT_SYSTEM_ERROR,error,attr_error),0);}
        void *returned=NULL;
        error=pthread_join(thread,&returned);
        if(error) {
            /* Never unmap a stack whose thread might still be running. Retain all
             * ownership/quota; this context cannot be destroyed or reused. */
            pthread_mutex_lock(&context->mutex);
            context->quarantined=1;context->join_pending=1;
            context->retained_mapping=mapping;context->retained_size=size;context->retained_ticket=ticket;
            pthread_mutex_unlock(&context->mutex);
            return result(GUEST_CONTEXT_QUARANTINED,error,0);
        }
        pthread_mutex_lock(&context->mutex);
        size_t shortfall=context->capacity_shortfall;
        int capacity_error=context->capacity_error,unsupported=context->capacity_unsupported;
        pthread_mutex_unlock(&context->mutex);
        if(attr_error || capacity_error || unsupported)
            return finish_stack(context,ticket,mapping,size,1,
                unsupported?result(GUEST_CONTEXT_UNSUPPORTED,0,0):result(GUEST_CONTEXT_SYSTEM_ERROR,capacity_error?capacity_error:attr_error,0),0);
        if(shortfall) {
            size_t next_admin=0;
            int bounded=attempt<3 && shortfall<=max_admin-admin;
            if(bounded) {
                size_t sum=admin+shortfall;
                bounded=sum<=SIZE_MAX-(context->page-1);
                if(bounded) {next_admin=(sum+context->page-1)&~(context->page-1);bounded=next_admin<=max_admin;}
            }
            status=finish_stack(context,ticket,mapping,size,1,
                bounded?success():result(GUEST_CONTEXT_UNSUPPORTED,0,0),bounded);
            if(status.status!=GUEST_CONTEXT_OK)return status;
            admin=next_admin;
            continue; /* Joined -> real munmap -> refund -> next precharge. */
        }
        status=finish_stack(context,ticket,mapping,size,1,success(),0);
        if(status.status==GUEST_CONTEXT_OK)*value=returned;
        return status;
    }
    return result(GUEST_CONTEXT_UNSUPPORTED,0,0); /* Loop is explicitly bounded. */
}
struct guest_context_result guest_context_destroy(guest_context *context) {
    if(!context)return result(GUEST_CONTEXT_INVALID,0,0);
    pthread_mutex_lock(&context->mutex);
    size_t internal=context->retained_ticket?1:0;
    if(context->primary_active || context->join_pending || context->tickets>internal) {
        pthread_mutex_unlock(&context->mutex);return result(GUEST_CONTEXT_BUSY,0,0);
    }
    if(context->retained_mapping && munmap(context->retained_mapping,context->retained_size)) {
        int error=errno;pthread_mutex_unlock(&context->mutex);
        return result(GUEST_CONTEXT_QUARANTINED,error,0);
    }
    /* Only a retained internal ticket can remain, and its real mapping has now
     * been released. External callers must release/cancel their own tickets. */
    if(context->retained_ticket) {
        guest_charge_ticket *ticket=context->retained_ticket;
        if(ticket->committed)context->charged-=ticket->bytes;
        else context->pending-=ticket->bytes;
        context->tickets--;
        struct guest_context_result freed=metadata_release_locked(context,GUEST_METADATA_TICKET,ticket);
        if(freed.status!=GUEST_CONTEXT_OK) {pthread_mutex_unlock(&context->mutex);return freed;}
    }
    context->retained_ticket=NULL;context->retained_mapping=NULL;context->retained_size=0;
    context->primary=(struct guest_primary_stack){0};
    if(context->resource_mapping) {
        if(context->resource_blocks) {pthread_mutex_unlock(&context->mutex);return result(GUEST_CONTEXT_BUSY,0,0);}
        if(unmap_resource(context->resource_mapping,context->resource_bytes)) {
            int e=errno;context->quarantined=1;pthread_mutex_unlock(&context->mutex);return result(GUEST_CONTEXT_QUARANTINED,e,0);
        }
        context->resource_mapping=NULL;context->resource_bytes=0;
    }
    pthread_mutex_unlock(&context->mutex);
    int error=pthread_mutex_destroy(&context->mutex);
    if(error)return result(GUEST_CONTEXT_SYSTEM_ERROR,error,0);
    free(context);return success();
}
