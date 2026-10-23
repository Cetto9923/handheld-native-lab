#define _GNU_SOURCE
#include "guest_context.h"
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdlib.h>
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
    unsigned memory_info_valid_fields;
    struct guest_primary_stack primary;
    void *retained_mapping;
    size_t retained_size;
    guest_charge_ticket *retained_ticket;
#if defined(GUEST_CONTEXT_TESTING)
    int fail_next_map;
    size_t pending_before_map;
#endif
};
static struct guest_context_result result(enum guest_context_status status,int error,int cleanup) {
    return (struct guest_context_result){status,error,cleanup};
}
static struct guest_context_result success(void) {return result(GUEST_CONTEXT_OK,0,0);}

struct guest_context_result guest_context_create(const struct guest_context_config *config,guest_context **out) {
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
    context->page=(size_t)page;*out=context;
    return success();
}
struct guest_context_result guest_charge_reserve(guest_context *context,size_t bytes,guest_charge_ticket **out) {
    if(!out)return result(GUEST_CONTEXT_INVALID,0,0);
    *out=NULL;
    if(!context || !bytes)return result(GUEST_CONTEXT_INVALID,0,0);
    guest_charge_ticket *ticket=malloc(sizeof(*ticket));
    if(!ticket)return result(GUEST_CONTEXT_NO_MEMORY,ENOMEM,0);
    pthread_mutex_lock(&context->mutex);
    struct guest_context_result status=success();
    if(context->quarantined)status=result(GUEST_CONTEXT_QUARANTINED,0,0);
    else if(context->charged>context->quota || context->pending>context->quota-context->charged ||
            bytes>context->quota-context->charged-context->pending)
        status=result(GUEST_CONTEXT_LIMIT,0,0);
    else {
        context->pending+=bytes;context->tickets++;
        *ticket=(guest_charge_ticket){context,bytes,0};*out=ticket;
    }
    pthread_mutex_unlock(&context->mutex);
    if(status.status!=GUEST_CONTEXT_OK)free(ticket);
    return status;
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
    pthread_mutex_unlock(&context->mutex);
    free(ticket);return success();
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
    else if(!context->primary_active || !context->primary.address)status=result(GUEST_CONTEXT_BAD_STATE,0,0);
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
#endif
struct invocation {guest_context *context;guest_primary_callback callback;void *arg;};
static void *invoke(void *opaque) {
    struct invocation call=*(struct invocation *)opaque;
    free(opaque);
    return call.callback(call.context,call.arg);
}
static struct guest_context_result finish_stack(guest_context *context,guest_charge_ticket *ticket,
        void *mapping,size_t bytes,int committed,struct guest_context_result status) {
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
    context->primary=(struct guest_primary_stack){0};context->primary_active=0;
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
    struct invocation *call=malloc(sizeof(*call));
    if(!call)return finish_stack(context,NULL,NULL,0,0,result(GUEST_CONTEXT_NO_MEMORY,ENOMEM,0));
    *call=(struct invocation){context,callback,arg};
    guest_charge_ticket *ticket=NULL;
    struct guest_context_result status=guest_charge_reserve(context,context->stack_bytes,&ticket);
    if(status.status!=GUEST_CONTEXT_OK) {free(call);return finish_stack(context,NULL,NULL,0,0,status);}
    size_t size=context->stack_bytes+2*context->page;
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
        return finish_stack(context,ticket,NULL,0,0,result(GUEST_CONTEXT_SYSTEM_ERROR,error,0));
    }
    void *stack=(char *)mapping+context->page;
    if(mprotect(stack,context->stack_bytes,PROT_READ|PROT_WRITE)) {
        int error=errno;free(call);
        return finish_stack(context,ticket,mapping,size,0,result(GUEST_CONTEXT_SYSTEM_ERROR,error,0));
    }
    status=guest_charge_commit(ticket);
    if(status.status!=GUEST_CONTEXT_OK) {free(call);return finish_stack(context,ticket,mapping,size,0,status);}
    pthread_mutex_lock(&context->mutex);
    context->primary=(struct guest_primary_stack){(uintptr_t)stack,context->stack_bytes,context->page};
    pthread_mutex_unlock(&context->mutex);
    pthread_attr_t attributes;
    int error=pthread_attr_init(&attributes);
    if(error) {free(call);return finish_stack(context,ticket,mapping,size,1,result(GUEST_CONTEXT_SYSTEM_ERROR,error,0));}
    error=pthread_attr_setstack(&attributes,stack,context->stack_bytes);
    if(!error)error=pthread_attr_setguardsize(&attributes,0); /* Explicit owned guards. */
    pthread_t thread;
    if(!error)error=pthread_create(&thread,&attributes,invoke,call);
    int attr_error=pthread_attr_destroy(&attributes);
    if(error) {free(call);return finish_stack(context,ticket,mapping,size,1,result(GUEST_CONTEXT_SYSTEM_ERROR,error,attr_error));}
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
    status=finish_stack(context,ticket,mapping,size,1,
                        attr_error?result(GUEST_CONTEXT_SYSTEM_ERROR,attr_error,0):success());
    if(status.status==GUEST_CONTEXT_OK)*value=returned;
    return status;
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
        context->tickets--;free(ticket);
    }
    context->retained_ticket=NULL;context->retained_mapping=NULL;context->retained_size=0;
    context->primary=(struct guest_primary_stack){0};
    pthread_mutex_unlock(&context->mutex);
    int error=pthread_mutex_destroy(&context->mutex);
    if(error)return result(GUEST_CONTEXT_SYSTEM_ERROR,error,0);
    free(context);return success();
}
