/* Integration contract: real recorder package and sync ownership; no GPU claim. */
#include "../../src/graphics/nvn_device/nvn_command_buffer.h"
#include "../../src/graphics/nvn_device/nvn_sync.h"
#include "../../src/graphics/nvn_device/nvn_obj_ffrs.h"
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
static unsigned char memory[4096];static int pool_key;
int nvn_trace_enabled(void){return 0;}
void nvn_engine_pool_del(void *p){(void)p;}
int nvn_engine_ready(void){return 0;}
uint64_t nvn_engine_last_fence(void){return 0;}
int nvn_engine_wait(uint64_t f,uint64_t t){(void)f;(void)t;return 1;}
int nvn_memory_pool_info_of(const void *pool,nvn_memory_pool_info *out){
 if(pool!=&pool_key)return 0;*out=(nvn_memory_pool_info){memory,sizeof(memory),0,1};return 1;
}
extern int nvnSyncInitialize(void*,void*);
extern void nvnSyncFinalize(void*);
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d %s\n",__LINE__,#x);return 1;}}while(0)
static uint64_t make(void *buf,void *key){
 nvn_command_buffer_add_command_memory_storage(buf,&pool_key,0,128);
 nvn_command_buffer_begin_recording(buf);
 nvn_command_buffer_rec_fence_sync(buf,key);
 nvn_command_buffer_rec_fence_sync(buf,key); /* two refs to one generation */
 return nvn_command_buffer_end_recording(buf);
}
int main(void){
 int buf=0,key=0;CHECK(nvn_command_buffer_initialize(&buf));CHECK(nvnSyncInitialize(&key,(void*)1));
 uint64_t first=make(&buf,&key);CHECK(first);nvn_command_package_lease lease={0};
 CHECK(nvn_command_buffer_package_acquire(first,&lease));nvn_command_package package;
 CHECK(nvn_command_buffer_package_lease_at(&lease,0,&package));
 nvn_cb_op_hdr header;memcpy(&header,package.records,8);CHECK(header.op==NVN_CB_OP_FENCE_SYNC);
 uint64_t raw=0,raw2=0;memcpy(&raw,package.records+8,8);memcpy(&raw2,package.records+24,8);
 ffrs_sync *old=(ffrs_sync *)(uintptr_t)raw;CHECK(old&&raw==raw2);
 /* Parent's leased dependency pins the child even after both source ranges retire. */
 nvn_command_buffer_add_command_memory_storage(&buf,&pool_key,256,128);
 nvn_command_buffer_begin_recording(&buf);nvn_command_buffer_rec_call_commands(&buf,1,&first);
 uint64_t parent=nvn_command_buffer_end_recording(&buf);CHECK(parent);
 nvn_command_package_lease tree={0};CHECK(nvn_command_buffer_package_acquire(parent,&tree));
 CHECK(nvn_command_buffer_package_lease_count(&tree)==2);
 nvnSyncFinalize(&key);nvn_command_buffer_retire_source_memory(memory,384);
 nvn_command_buffer_package_release(&lease); /* tree is now sole live reader */
 nvn_sync_stats before,after;nvn_sync_get_stats(&before);
 for(unsigned i=0;i<1100000;i++){
   CHECK(nvnSyncInitialize(&key,(void*)1));CHECK(make(&buf,&key));nvnSyncFinalize(&key);
 }
 nvn_command_buffer_retire_source_memory(memory,128);
 nvn_sync_get_stats(&after);CHECK(after.retained_generations-before.retained_generations<16);
 nvn_sync_family_fence_object(old);CHECK(old->fenced);
 nvn_command_buffer_package_release(&tree);
 /* Abandoned recording releases its token; no unbounded retained generations. */
 nvn_sync_get_stats(&before);
 for(unsigned i=0;i<10000;i++){
   CHECK(nvnSyncInitialize(&key,(void*)1));nvn_command_buffer_add_command_memory_storage(&buf,&pool_key,0,128);
   nvn_command_buffer_begin_recording(&buf);nvn_command_buffer_rec_fence_sync(&buf,&key);
   nvnSyncFinalize(&key);nvn_command_buffer_begin_recording(&buf);
 }
 nvn_sync_get_stats(&after);CHECK(after.retained_generations-before.retained_generations<16);
 /* A rejected FenceSync outside recording must release its acquired token. */
 nvn_command_buffer_finalize(&buf);nvn_sync_get_stats(&before);
 for(unsigned i=0;i<10000;i++){
   CHECK(nvnSyncInitialize(&key,(void*)1));
   nvn_command_buffer_rec_fence_sync(&buf,&key);nvnSyncFinalize(&key);
 }
 nvn_sync_get_stats(&after);CHECK(after.retained_generations-before.retained_generations<16);
 printf("sync_package_contract cycles=1100000 held_retired_lease=preserved abandoned=10000 rejected=10000 retained=%" PRIu64 "\n",after.retained_generations);
 return 0;
}
