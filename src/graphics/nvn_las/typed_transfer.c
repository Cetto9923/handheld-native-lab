/* SPDX-License-Identifier: MIT
 * Record construction/Begin/End/Copy semantics adapted from LASRecomp
 * fe66f31a6dbcaee1faa1e6718805cb2dbcc67a93 runtime/nvn/cmdbuf.c.
 * Copyright (c) 2026 Peiikai. See third_party/lasrecomp/LICENSE/UPSTREAM.json.
 * Side tables, strict errors and typed adapters are FFRS code. No Cpu emulator.
 */
#include "typed_transfer.h"
#include <stdatomic.h>
#include <string.h>
#define TYPED_MAGIC UINT64_C(0x464652534c415354)
static _Thread_local FfrsLasTyped *active;
static _Thread_local FfrsLasStatus inactive_error;
static atomic_uint_fast64_t next_handle=1;
static FfrsLasStatus check(const FfrsLasTyped *c) {
    if(!c||c->magic!=TYPED_MAGIC)return FFRS_LAS_INVALID;
    if(c->owner!=ffrs_las_thread_id())return FFRS_LAS_WRONG_THREAD;
    return ffrs_las_backend_check(c->backend);
}
static FfrsLasStatus editable(FfrsLasTyped *c) {
    FfrsLasStatus s=check(c);if(s)return s;
    if(c->backend->phase==FFRS_LAS_UNCERTAIN)return FFRS_LAS_QUARANTINED;
    return c->backend->phase==FFRS_LAS_IDLE?FFRS_LAS_OK:FFRS_LAS_BUSY;
}
FfrsLasStatus ffrs_las_typed_init(FfrsLasTyped *c,FfrsLasBackend *b,const NVNdevice *device) {
    FfrsLasStatus s=ffrs_las_backend_check(b);if(s)return s;
    if(!c||!device)return FFRS_LAS_INVALID;if(c->magic)return FFRS_LAS_BUSY;
    *c=(FfrsLasTyped){.magic=TYPED_MAGIC,.owner=ffrs_las_thread_id(),.device_key=device,.backend=b};
    return FFRS_LAS_OK;
}
FfrsLasStatus ffrs_las_typed_enter(FfrsLasTyped *c) {
    FfrsLasStatus s=check(c);if(s)return s;if(active)return FFRS_LAS_BUSY;
    active=c;inactive_error=FFRS_LAS_OK;return FFRS_LAS_OK;
}
FfrsLasStatus ffrs_las_typed_leave(FfrsLasTyped *c) {
    FfrsLasStatus s=check(c);if(s)return s;if(active!=c)return FFRS_LAS_NO_ACTIVE;
    active=NULL;return FFRS_LAS_OK;
}
FfrsLasStatus ffrs_las_typed_status(const FfrsLasTyped *c) {
    if(!c)return inactive_error;
    FfrsLasStatus s=check(c);return s?s:c->error;
}
FfrsLasStatus ffrs_las_typed_clear_error(FfrsLasTyped *c) {
    FfrsLasStatus s=editable(c);if(s)return s;c->error=FFRS_LAS_OK;return FFRS_LAS_OK;
}
FfrsLasStatus ffrs_las_typed_attach_pool(FfrsLasTyped *c,const NVNmemoryPool *key,
                                      void *memory,size_t bytes) {
    FfrsLasStatus s=editable(c);if(s)return s;
    if(!key||!memory||((uintptr_t)memory&7)||bytes<2*sizeof(CmdRec)||
       bytes>FFRS_LAS_MAX_RECORDS*sizeof(CmdRec)||(uintptr_t)memory>UINTPTR_MAX-bytes)
        return FFRS_LAS_INVALID;
    unsigned slot=FFRS_LAS_MAX_OBJECTS;
    for(unsigned i=0;i<FFRS_LAS_MAX_OBJECTS;++i) {
        FfrsLasPoolSpan *p=&c->pools[i];if(!p->key){if(slot==FFRS_LAS_MAX_OBJECTS)slot=i;continue;}
        if(p->key==key||((uintptr_t)memory<(uintptr_t)p->memory+p->bytes&&
                       (uintptr_t)p->memory<(uintptr_t)memory+bytes))return FFRS_LAS_INVALID;
    }
    if(slot==FFRS_LAS_MAX_OBJECTS)return FFRS_LAS_LIMIT;
    c->pools[slot]=(FfrsLasPoolSpan){key,memory,bytes};return FFRS_LAS_OK;
}
FfrsLasStatus ffrs_las_typed_close(FfrsLasTyped *c) {
    FfrsLasStatus s=editable(c);if(s)return s;if(active==c)return FFRS_LAS_BUSY;
    /* Only borrowed side-table metadata is dropped; no resource is destroyed. */
    *c=(FfrsLasTyped){0};return FFRS_LAS_OK;
}
static void latch(FfrsLasTyped *c,FfrsLasStatus s) {
    if(!c){inactive_error=s;return;}
    if(!c->error)c->error=s;
    for(unsigned i=0;i<FFRS_LAS_MAX_OBJECTS;++i) {
        c->commands[i].recording=false;c->commands[i].sealed=false;c->commands[i].handle=0;
    }
}
static FfrsLasTyped *current(void) {
    if(!active){inactive_error=FFRS_LAS_NO_ACTIVE;return NULL;}
    FfrsLasStatus s=editable(active);if(s){latch(active,s);return NULL;}
    if(active->error)return NULL;return active;
}
static FfrsLasCommandObject *object(FfrsLasTyped *c,const NVNcommandBuffer *key) {
    for(unsigned i=0;i<FFRS_LAS_MAX_OBJECTS;++i)if(c->commands[i].key==key&&key)return &c->commands[i];
    latch(c,FFRS_LAS_INVALID);return NULL;
}
static NVNboolean command_initialize(NVNcommandBuffer *key,NVNdevice *device) {
    FfrsLasTyped *c=current();if(!c)return 0;
    if(!key||device!=c->device_key){latch(c,FFRS_LAS_INVALID);return 0;}
    unsigned slot=FFRS_LAS_MAX_OBJECTS;
    for(unsigned i=0;i<FFRS_LAS_MAX_OBJECTS;++i) {
        if(c->commands[i].key==key){latch(c,FFRS_LAS_INVALID);return 0;}
        if(!c->commands[i].key&&slot==FFRS_LAS_MAX_OBJECTS)slot=i;
    }
    if(slot==FFRS_LAS_MAX_OBJECTS){latch(c,FFRS_LAS_LIMIT);return 0;}
    c->commands[slot]=(FfrsLasCommandObject){.key=key};return 1;
}
static void command_finalize(NVNcommandBuffer *key) {
    FfrsLasTyped *c=current();if(!c)return;FfrsLasCommandObject *o=object(c,key);if(!o)return;
    if(o->recording){latch(c,FFRS_LAS_BUSY);return;}*o=(FfrsLasCommandObject){0};
}
static void command_memory(NVNcommandBuffer *key,const NVNmemoryPool *pool,ptrdiff_t offset,size_t bytes) {
    FfrsLasTyped *c=current();if(!c)return;FfrsLasCommandObject *o=object(c,key);if(!o)return;
    if(o->recording){latch(c,FFRS_LAS_UNSUPPORTED);return;}
    FfrsLasPoolSpan *p=NULL;
    for(unsigned i=0;i<FFRS_LAS_MAX_OBJECTS;++i)if(c->pools[i].key==pool&&pool)p=&c->pools[i];
    if(!p||offset<0||(uint64_t)offset>p->bytes||bytes>p->bytes-(size_t)offset||
       bytes<2*sizeof(CmdRec)||((uintptr_t)p->memory+(size_t)offset)&7) {
        latch(c,FFRS_LAS_INVALID);return;
    }
    /* Aliasing command-memory spans between live command objects is refused. */
    uintptr_t start=(uintptr_t)p->memory+(size_t)offset;
    for(unsigned i=0;i<FFRS_LAS_MAX_OBJECTS;++i) {
        FfrsLasCommandObject *q=&c->commands[i];
        if(q!=o&&q->memory&&start<(uintptr_t)q->memory+q->capacity&&
           (uintptr_t)q->memory<start+bytes){latch(c,FFRS_LAS_INVALID);return;}
    }
    o->pool_key=pool;o->memory=(unsigned char *)start;o->capacity=bytes;o->used=0;
    o->sealed=false;o->handle=0;
}
/* LAS raw_rec/record: zero 80-byte record, op/size header and x1..x7 snapshot.
 * Typed declarations know exactly which arguments are live, so unused bits zero.
 * Reserve END capacity instead of invoking an unsupported guest OOM callback. */
static bool append(FfrsLasTyped *c,FfrsLasCommandObject *o,const CmdRec *r,bool terminal) {
    size_t need=terminal?sizeof(CmdRec):2*sizeof(CmdRec);
    if(!o->memory||o->used>o->capacity||need>o->capacity-o->used){latch(c,FFRS_LAS_LIMIT);return false;}
    memcpy(o->memory+o->used,r,sizeof(*r));o->used+=sizeof(*r);return true;
}
static void command_begin(NVNcommandBuffer *key) {
    FfrsLasTyped *c=current();if(!c)return;FfrsLasCommandObject *o=object(c,key);if(!o)return;
    if(o->recording){latch(c,FFRS_LAS_BUSY);return;}
    o->used=0;o->handle=0;o->sealed=false;
    CmdRec r={.op=CMD_BARRIER,.size=sizeof(CmdRec)};
    if(append(c,o,&r,false))o->recording=true;
}
static NVNcommandHandle command_end(NVNcommandBuffer *key) {
    FfrsLasTyped *c=current();if(!c)return 0;FfrsLasCommandObject *o=object(c,key);if(!o)return 0;
    if(!o->recording){latch(c,FFRS_LAS_INVALID);return 0;}
    CmdRec r={.op=CMD_END,.size=sizeof(CmdRec)};if(!append(c,o,&r,true))return 0;
    uint_fast64_t next=atomic_load_explicit(&next_handle,memory_order_relaxed);
    do {if(!next||next==UINT64_MAX){latch(c,FFRS_LAS_LIMIT);return 0;}}
    while(!atomic_compare_exchange_weak_explicit(&next_handle,&next,next+1,memory_order_relaxed,memory_order_relaxed));
    o->recording=false;o->sealed=true;o->handle=next;return next;
}
static void command_copy(NVNcommandBuffer *key,NVNbufferAddress src,NVNbufferAddress dst,size_t bytes,int flags) {
    FfrsLasTyped *c=current();if(!c)return;FfrsLasCommandObject *o=object(c,key);if(!o)return;
    if(!o->recording){latch(c,FFRS_LAS_INVALID);return;}
    if(flags){latch(c,FFRS_LAS_UNSUPPORTED);return;}
    CmdRec r={.op=CMD_COPY_BUFFER_TO_BUFFER,.size=sizeof(CmdRec),.x={src,dst,bytes,0}};
    (void)append(c,o,&r,false);
}
static void command_barrier(NVNcommandBuffer *key,int bits) {
    FfrsLasTyped *c=current();if(!c)return;FfrsLasCommandObject *o=object(c,key);if(!o)return;
    if(!o->recording){latch(c,FFRS_LAS_INVALID);return;}
    if(bits){latch(c,FFRS_LAS_UNSUPPORTED);return;}
    CmdRec r={.op=CMD_BARRIER,.size=sizeof(CmdRec)};(void)append(c,o,&r,false);
}
static NVNboolean command_is_recording(const NVNcommandBuffer *key) {
    FfrsLasTyped *c=current();if(!c)return 0;FfrsLasCommandObject *o=object(c,key);
    return o?o->recording:0;
}
FfrsLasStatus ffrs_las_typed_encode(FfrsLasTyped *c,VkCommandBuffer command,NVNcommandHandle handle) {
    FfrsLasStatus s=editable(c);if(s)return s;if(c->error)return c->error;
    for(unsigned i=0;i<FFRS_LAS_MAX_OBJECTS;++i) {
        FfrsLasCommandObject *o=&c->commands[i];
        if(o->key&&o->sealed&&o->handle==handle&&handle)
            return ffrs_las_backend_encode(c->backend,command,o->memory,o->used);
    }
    return FFRS_LAS_INVALID;
}
#define TARGET(name,fn,args,returns) {name,(void(*)(void))fn,{.version=1,.flags=NB_SIG_REGISTER_ONLY,.gp_arguments=args,.gp_returns=returns}}
static const FfrsLasTypedTarget targets[]={
    TARGET("nvnCommandBufferInitialize",command_initialize,2,1),
    TARGET("nvnCommandBufferFinalize",command_finalize,1,0),
    TARGET("nvnCommandBufferAddCommandMemory",command_memory,4,0),
    TARGET("nvnCommandBufferBeginRecording",command_begin,1,0),
    TARGET("nvnCommandBufferEndRecording",command_end,1,1),
    TARGET("nvnCommandBufferCopyBufferToBuffer",command_copy,5,0),
    TARGET("nvnCommandBufferBarrier",command_barrier,2,0),
    TARGET("nvnCommandBufferIsRecording",command_is_recording,1,1)
};
const FfrsLasTypedTarget *ffrs_las_typed_targets(size_t *count) {
    if(count)*count=sizeof(targets)/sizeof(targets[0]);return targets;
}
