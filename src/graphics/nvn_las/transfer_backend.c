/* SPDX-License-Identifier: MIT — FFRS Vulkan adapter, not LAS's CPU-copy case. */
#include "transfer_backend.h"
#include <stdatomic.h>
#include <string.h>

#define LAS_MAGIC UINT64_C(0x464652534c415331)
static atomic_uint_fast64_t next_thread=1;
static _Thread_local uint64_t this_thread;
uint64_t ffrs_las_thread_id(void) {
    if(!this_thread) {
        uint_fast64_t next=atomic_load_explicit(&next_thread,memory_order_relaxed);
        do {
            if(!next || next==UINT64_MAX)return 0;
        } while(!atomic_compare_exchange_weak_explicit(&next_thread,&next,next+1,
                    memory_order_relaxed,memory_order_relaxed));
        this_thread=next;
    }
    return this_thread;
}
FfrsLasStatus ffrs_las_backend_check(const FfrsLasBackend *b) {
    if(!b||b->magic!=LAS_MAGIC)return FFRS_LAS_INVALID;
    return b->owner==ffrs_las_thread_id()?FFRS_LAS_OK:FFRS_LAS_WRONG_THREAD;
}
FfrsLasStatus ffrs_las_backend_init(FfrsLasBackend *b,VkDevice device) {
    if(!b||!device)return FFRS_LAS_INVALID;
    if(b->magic)return FFRS_LAS_BUSY;
    uint64_t owner=ffrs_las_thread_id();if(!owner)return FFRS_LAS_LIMIT;
    *b=(FfrsLasBackend){.magic=LAS_MAGIC,.owner=owner,.device=device};return FFRS_LAS_OK;
}
static FfrsLasStatus idle(FfrsLasBackend *b) {
    FfrsLasStatus s=ffrs_las_backend_check(b);if(s)return s;
    if(b->phase==FFRS_LAS_UNCERTAIN)return FFRS_LAS_QUARANTINED;
    return b->phase==FFRS_LAS_IDLE?FFRS_LAS_OK:FFRS_LAS_BUSY;
}
FfrsLasStatus ffrs_las_backend_bind(FfrsLasBackend *b,uint64_t address,uint64_t bytes,
    FfrsVkBufferResource *r,VkDeviceSize offset,VkBufferUsageFlags usage) {
    FfrsLasStatus s=idle(b);if(s)return s;
    if(!address||!bytes||address>UINT64_MAX-bytes||!r||!r->buffer||!r->memory||!r->mapped||
       !(r->memory_flags&VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)||r->allocation_bytes<r->bytes||
       r->device!=b->device||offset>r->bytes||bytes>r->bytes-offset||
       !usage||(usage&~(VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT)))
        return FFRS_LAS_INVALID;
    unsigned slot=FFRS_LAS_MAX_BUFFERS;
    for(unsigned i=0;i<FFRS_LAS_MAX_BUFFERS;++i) {
        FfrsLasBufferBinding *v=&b->buffers[i];
        if(!v->resource){if(slot==FFRS_LAS_MAX_BUFFERS)slot=i;continue;}
        if(v->resource==r||v->resource->buffer==r->buffer||v->resource->memory==r->memory||
           (address<v->address+v->bytes&&v->address<address+bytes))return FFRS_LAS_INVALID;
    }
    if(slot==FFRS_LAS_MAX_BUFFERS)return FFRS_LAS_LIMIT;
    b->buffers[slot]=(FfrsLasBufferBinding){address,bytes,offset,r,usage};return FFRS_LAS_OK;
}
FfrsLasStatus ffrs_las_backend_unbind(FfrsLasBackend *b,uint64_t address) {
    FfrsLasStatus s=idle(b);if(s)return s;
    for(unsigned i=0;i<FFRS_LAS_MAX_BUFFERS;++i)if(b->buffers[i].resource&&b->buffers[i].address==address){
        b->buffers[i]=(FfrsLasBufferBinding){0};return FFRS_LAS_OK;
    }
    return FFRS_LAS_INVALID;
}
FfrsLasStatus ffrs_las_backend_close(FfrsLasBackend *b) {
    FfrsLasStatus s=idle(b);if(s)return s;
    for(unsigned i=0;i<FFRS_LAS_MAX_BUFFERS;++i)if(b->buffers[i].resource)return FFRS_LAS_BUSY;
    *b=(FfrsLasBackend){0};return FFRS_LAS_OK;
}
typedef struct CopyPlan {VkBuffer source,destination;VkBufferCopy region;} CopyPlan;
typedef struct RecordSnapshot {uint32_t op,size;uint64_t x[7];float f[4];} RecordSnapshot;
_Static_assert(sizeof(RecordSnapshot)==sizeof(CmdRec),"snapshot record ABI");
static void transfer_barrier(VkCommandBuffer command) {
    VkMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask=VK_ACCESS_HOST_WRITE_BIT|VK_ACCESS_MEMORY_WRITE_BIT,
        .dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT};
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT|VK_PIPELINE_STAGE_HOST_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&barrier,0,NULL,0,NULL);
}
static const FfrsLasBufferBinding *range(FfrsLasBackend *b,uint64_t address,uint64_t bytes,
                                       VkBufferUsageFlags usage) {
    for(unsigned i=0;i<FFRS_LAS_MAX_BUFFERS;++i) {
        const FfrsLasBufferBinding *v=&b->buffers[i];
        if(v->resource&&(v->usage&usage)&&address>=v->address&&
           address-v->address<=v->bytes&&bytes<=v->bytes-(address-v->address))return v;
    }
    return NULL;
}
FfrsLasStatus ffrs_las_backend_encode(FfrsLasBackend *b,VkCommandBuffer command,
    const void *records,size_t bytes) {
    FfrsLasStatus s=idle(b);if(s)return s;
    if(!command||!records||!bytes||bytes%sizeof(CmdRec))return FFRS_LAS_INVALID;
    if(bytes/sizeof(CmdRec)>FFRS_LAS_MAX_RECORDS)return FFRS_LAS_LIMIT;
    RecordSnapshot snapshot[FFRS_LAS_MAX_RECORDS];CopyPlan plan[FFRS_LAS_MAX_RECORDS];
    memcpy(snapshot,records,bytes);size_t count=bytes/sizeof(CmdRec);
    uint32_t copies=0,barriers=0;uint64_t total=0;
    for(size_t i=0;i<count;++i) {
        const RecordSnapshot *r=&snapshot[i];
        if(r->size!=sizeof(CmdRec))return FFRS_LAS_INVALID;
        if(r->op==CMD_END) {if(i+1!=count)return FFRS_LAS_INVALID;continue;}
        if(i+1==count)return FFRS_LAS_INVALID;
        if(r->op==CMD_BARRIER) {++barriers;continue;}
        if(r->op!=CMD_COPY_BUFFER_TO_BUFFER)return FFRS_LAS_UNSUPPORTED;
        if(r->x[3])return FFRS_LAS_UNSUPPORTED;
        uint64_t n=r->x[2];if(!n)return FFRS_LAS_INVALID;
        const FfrsLasBufferBinding *src=range(b,r->x[0],n,VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        const FfrsLasBufferBinding *dst=range(b,r->x[1],n,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        if(!src||!dst)return FFRS_LAS_INVALID;
        VkDeviceSize so=src->offset+(r->x[0]-src->address), d=dst->offset+(r->x[1]-dst->address);
        if((so|d|n)&3)return FFRS_LAS_UNSUPPORTED;
        if(src->resource->buffer==dst->resource->buffer&&so<d+n&&d<so+n)
            return FFRS_LAS_UNSUPPORTED;
        if(total>UINT64_MAX-n)return FFRS_LAS_LIMIT;
        total+=n;++copies;
        plan[i]=(CopyPlan){src->resource->buffer,dst->resource->buffer,{so,d,n}};
    }
    if(snapshot[count-1].op!=CMD_END)return FFRS_LAS_INVALID;
    /* Full validation precedes the first Vulkan command; copied records cannot
     * change between preflight and encode. No CPU emulation of COPY occurs. */
    for(size_t i=0;i<count;++i) {
        if(snapshot[i].op==CMD_BARRIER) {
            transfer_barrier(command);
        } else if(snapshot[i].op==CMD_COPY_BUFFER_TO_BUFFER) {
            /* Consecutive copies may depend on earlier writes even without an
             * explicit record; conservative serialization preserves that order. */
            transfer_barrier(command);
            CopyPlan *p=&plan[i];vkCmdCopyBuffer(command,p->source,p->destination,1,&p->region);
        }
    }
    VkMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,
        0,1,&host,0,NULL,0,NULL);
    b->phase=FFRS_LAS_ENCODED;b->copy_count=copies;b->copied_bytes=total;b->barrier_count=barriers;
    b->fatal=false;b->submit_accepted=false;return FFRS_LAS_OK;
}
FfrsLasStatus ffrs_las_backend_submitted(FfrsLasBackend *b,VkFence fence,VkResult value) {
    FfrsLasStatus s=ffrs_las_backend_check(b);if(s)return s;
    if(b->phase!=FFRS_LAS_ENCODED)return FFRS_LAS_BUSY;
    b->fence=fence;b->last_vk_result=value;b->submit_accepted=value==VK_SUCCESS&&fence;
    b->fatal=!b->submit_accepted;b->phase=b->submit_accepted?FFRS_LAS_PENDING:FFRS_LAS_UNCERTAIN;
    return b->submit_accepted?FFRS_LAS_OK:FFRS_LAS_QUARANTINED;
}
FfrsLasStatus ffrs_las_backend_waited(FfrsLasBackend *b,VkFence fence,VkResult value) {
    FfrsLasStatus s=ffrs_las_backend_check(b);if(s)return s;
    if(b->phase!=FFRS_LAS_PENDING&&b->phase!=FFRS_LAS_UNCERTAIN)return FFRS_LAS_BUSY;
    if(!fence||fence!=b->fence)return FFRS_LAS_INVALID;
    b->last_vk_result=value;
    if(!b->submit_accepted||b->fatal)return FFRS_LAS_QUARANTINED;
    if(value==VK_SUCCESS){b->phase=FFRS_LAS_COMPLETE;return FFRS_LAS_OK;}
    b->phase=FFRS_LAS_UNCERTAIN;if(value!=VK_TIMEOUT&&value!=VK_NOT_READY)b->fatal=true;
    return FFRS_LAS_QUARANTINED;
}
FfrsLasStatus ffrs_las_backend_retire(FfrsLasBackend *b) {
    FfrsLasStatus s=ffrs_las_backend_check(b);if(s)return s;
    if(b->phase==FFRS_LAS_UNCERTAIN)return FFRS_LAS_QUARANTINED;
    if(b->phase!=FFRS_LAS_COMPLETE)return FFRS_LAS_BUSY;
    b->phase=FFRS_LAS_IDLE;return FFRS_LAS_OK;
}
FfrsLasStatus ffrs_las_backend_cancel(FfrsLasBackend *b,bool reset) {
    FfrsLasStatus s=ffrs_las_backend_check(b);if(s)return s;
    if(b->phase==FFRS_LAS_UNCERTAIN)return FFRS_LAS_QUARANTINED;
    if(b->phase!=FFRS_LAS_ENCODED||!reset)return FFRS_LAS_BUSY;
    b->phase=FFRS_LAS_IDLE;return FFRS_LAS_OK;
}
