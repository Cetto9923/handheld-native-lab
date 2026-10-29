/* CPU ownership contract, no GPU work is replaced or declared complete.
 * Source pool bytes are canaries: the translated format lives on the host. */
#include "../../src/graphics/nvn_device/nvn_command_buffer.h"
#include "../../src/graphics/nvn_device/nvn_obj_ffrs.h"
#include "../../src/graphics/nvn_device/nvn_queue.h"
#include "../../src/graphics/nvn_device/nvn_command_replay.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static unsigned char memory[4096];static int pool_key;static unsigned removed;
static int queue_check,queue_errors;static unsigned queue_seen,queue_batches,registered;
int nvn_trace_enabled(void){return 0;}
int nvn_memory_pool_info_of(const void *pool,nvn_memory_pool_info *out){
    if(pool!=&pool_key)return 0;*out=(nvn_memory_pool_info){memory,sizeof(memory),0,1};return 1;
}
void nvn_engine_pool_del(void *p){if(!p)abort();removed++;}
void nvn_engine_pool_add(void *p,size_t bytes){if(!p||!bytes)abort();registered++;}
/* Controlled synchronous CPU consumer at the real queue seam. This does not
 * report GPU completion: it checks that retirement during translation cannot
 * release command bytes and that >64 handles are all delivered in order. */
int nvn_engine_submit(const uint64_t *handles,int count){
    if(!queue_check)return 0;
    unsigned first=queue_seen;nvn_command_buffer_retire_source_memory(memory+first*32,(size_t)count*32);
    for(int i=0;i<count;i++){uint32_t value;memcpy(&value,(const void*)(uintptr_t)(handles[i]+20),4);
        if(value!=queue_seen)queue_errors++;queue_seen++;}
    queue_batches++;return 1;
}
void nvn_command_replay_get_stats(nvn_replay_stats *out){memset(out,0,sizeof(*out));}
int nvn_command_replay_run(uint64_t handle){(void)handle;return 1;}
#define CHECK(e) do{if(!(e)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#e);return 1;}}while(0)
static uint64_t make(void *buffer,size_t offset,size_t bytes,uint32_t value,uint32_t *output){
    nvn_command_buffer_add_command_memory_storage(buffer,&pool_key,(int64_t)offset,bytes);
    nvn_command_buffer_begin_recording(buffer);nvn_command_buffer_rec_report_value(buffer,value,(uintptr_t)output);
    return nvn_command_buffer_end_recording(buffer);
}
static int value_is(const nvn_command_package *p,uint32_t value){
    if(p->bytes<32)return 0;nvn_cb_op_hdr h;memcpy(&h,p->records,8);uint32_t got;
    memcpy(&got,p->records+20,4);return h.op==NVN_CB_OP_SIGNAL_EVENT&&h.size==24&&got==value;
}
int main(void){
    int buffer=0,other=0;uint32_t output=0;memset(memory,0x6b,sizeof memory);
    CHECK(nvn_command_buffer_initialize(&buffer));CHECK(nvn_command_buffer_initialize(&other));
    uint64_t first=make(&buffer,0,1024,0x17,&output);CHECK(first);
    CHECK(nvn_command_buffer_get_command_memory_used(&buffer)==32);
    nvn_command_package_lease one={0};CHECK(nvn_command_buffer_package_acquire(first,&one));
    nvn_command_package snapshot;CHECK(nvn_command_buffer_package_lease_at(&one,0,&snapshot));CHECK(value_is(&snapshot,0x17));
    /* BeginRecording preserves the cursor since the last AddCommandMemory. */
    nvn_command_buffer_begin_recording(&buffer);nvn_command_buffer_rec_report_value(&buffer,0x29,(uintptr_t)&output);
    uint64_t second=nvn_command_buffer_end_recording(&buffer);CHECK(second&&second!=first);
    CHECK(nvn_command_buffer_get_command_memory_used(&buffer)==64);
    nvn_command_buffer_finalize(&buffer);
    nvn_command_package_lease two={0};CHECK(nvn_command_buffer_package_acquire(second,&two));
    nvn_command_buffer_package_release(&two);
    /* Adjacent packed grant must not retire earlier consumed ranges. */
    uint64_t adjacent=make(&other,64,1024,0x35,&output);CHECK(adjacent);
    CHECK(nvn_command_buffer_package_acquire(second,&two));nvn_command_buffer_package_release(&two);
    nvn_command_buffer_add_command_memory_storage(&other,&pool_key,0,32);
    nvn_command_package_lease invalid={0};CHECK(!nvn_command_buffer_package_acquire(first,&invalid));
    CHECK(nvn_command_buffer_package_lease_at(&one,0,&snapshot)&&value_is(&snapshot,0x17));
    nvn_command_buffer_package_release(&one);CHECK(!nvn_command_buffer_get_package(first,&snapshot));
    CHECK(nvn_command_buffer_package_acquire(second,&two));nvn_command_buffer_package_release(&two);
    /* CALL target remains valid after its recorder was finalized. */
    nvn_command_buffer_add_command_memory_storage(&other,&pool_key,256,512);
    nvn_command_buffer_begin_recording(&other);nvn_command_buffer_rec_call_commands(&other,1,&second);
    uint64_t parent=nvn_command_buffer_end_recording(&other);CHECK(parent);
    nvn_command_package_lease tree={0};CHECK(nvn_command_buffer_package_acquire(parent,&tree));
    CHECK(nvn_command_buffer_package_lease_count(&tree)==2);
    nvn_command_buffer_add_command_memory_storage(&other,&pool_key,32,32);
    CHECK(!nvn_command_buffer_package_acquire(parent,&invalid));
    CHECK(nvn_command_buffer_package_lease_at(&tree,1,&snapshot)&&value_is(&snapshot,0x29));
    nvn_command_buffer_package_release(&tree);
    /* A reused malloc address must never turn an old CALL generation into a
     * different new recording. The child source is explicitly dead now. */
    for(unsigned i=0;i<100;i++){
        CHECK(make(&other,32,32,i,&output));CHECK(!nvn_command_buffer_package_acquire(parent,&invalid));
    }
    nvn_command_buffer_add_command_memory_storage(&other,&pool_key,256,512);
    nvn_command_buffer_stats before,after;nvn_command_buffer_get_stats(&before);
    /* Exceed the old 65536-package run limit with one reusable source range. */
    uint64_t last=0;for(unsigned i=0;i<80000;i++){last=make(&other,1024,64,i,&output);CHECK(last);}
    nvn_command_buffer_get_stats(&after);
    CHECK(after.live_packages<=before.live_packages+1);
    CHECK(after.package_bytes<=before.package_bytes+32);
    CHECK(after.packages_reclaimed-before.packages_reclaimed>=79999);
    for(size_t i=0;i<sizeof(memory);i++)CHECK(memory[i]==0x6b);
    CHECK(output==0); /* This ownership test does not execute guest commands. */
    CHECK(!nvn_command_buffer_get_package(UINT64_C(0x123456789),&snapshot));
    CHECK(!nvn_command_buffer_package_acquire(UINT64_C(0x123456789),&invalid));
    CHECK(nvn_command_buffer_package_acquire(last,&one));
    nvn_command_buffer_retire_source_memory(memory,sizeof memory);
    CHECK(!nvn_command_buffer_package_acquire(last,&invalid));
    CHECK(nvn_command_buffer_package_lease_at(&one,0,&snapshot)&&value_is(&snapshot,79999));
    nvn_command_buffer_package_release(&one);
    nvn_command_buffer_stats empty;nvn_command_buffer_get_stats(&empty);CHECK(!empty.live_packages&&!empty.package_bytes);
    uint64_t handles[105];for(unsigned i=0;i<105;i++){handles[i]=make(&other,i*32,32,i,&output);CHECK(handles[i]);}
    queue_check=1;nvn_queue_submit_commands(NULL,105,handles);
    CHECK(!queue_errors&&queue_seen==105&&queue_batches==2&&registered==105);
    nvn_command_buffer_get_stats(&empty);CHECK(!empty.live_packages&&!empty.package_bytes);
    printf("command_lifetime_contract errors=0 iterations=80000 peak_live_packages=%llu peak_package_bytes=%llu reclaimed=%llu engine_ranges_removed=%u nested_lease=1 generation_guard=1 pool_canaries=1 queued=105 batches=2 final_live=0\n",
           (unsigned long long)after.live_packages,(unsigned long long)after.package_bytes,
           (unsigned long long)after.packages_reclaimed,removed);return 0;
}
