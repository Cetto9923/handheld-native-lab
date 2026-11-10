/* CPU ownership contract for the managed sync API. No GPU completion claim. */
#include "../../src/graphics/nvn_device/nvn_sync.h"
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
extern int nvnSyncInitialize(void *,void *);
extern void nvnSyncFinalize(void *);
int nvn_engine_ready(void){return 0;}
uint64_t nvn_engine_last_fence(void){return 0;}
int nvn_engine_wait(uint64_t fence,uint64_t timeout){(void)fence;(void)timeout;return 1;}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(void){
 uint64_t key=0,legacy_key=0;void *held=NULL,*held2=NULL,*token=NULL;
 CHECK(nvnSyncInitialize(&key,(void*)1));
 ffrs_sync *old=nvn_sync_family_managed_acquire(&key,&held);CHECK(old&&held);
 CHECK(nvn_sync_family_managed_acquire(&key,&held2)==old&&held2==held);
 old->fence=77;old->fenced=1;
 CHECK(nvnSyncInitialize(&key,(void*)1));CHECK(!nvn_sync_family_is_fenced(&key));
 nvnSyncFinalize(&key);CHECK(old->fence==77&&old->fenced);
 nvn_sync_family_managed_release(held);CHECK(old->fence==77);
 nvn_sync_stats before,after;nvn_sync_get_stats(&before);
 for(unsigned i=0;i<1100000;i++){
   CHECK(nvnSyncInitialize(&key,(void*)1));
   ffrs_sync *current=nvn_sync_family_managed_acquire(&key,&token);CHECK(current&&token&&current!=old);
   nvn_sync_family_fence_object(current);nvnSyncFinalize(&key);
   nvn_sync_family_managed_release(token);token=NULL;
 }
 nvn_sync_get_stats(&after);CHECK(after.retained_generations-before.retained_generations<8);
 CHECK(old->fence==77&&old->fenced);nvn_sync_family_managed_release(held2);
 CHECK(nvnSyncInitialize(&legacy_key,(void*)1));
 ffrs_sync *legacy=nvn_sync_family_object(&legacy_key);CHECK(legacy);
 CHECK(nvn_sync_family_managed_acquire(&legacy_key,&token)==legacy);
 legacy->fence=111;legacy->fenced=1;nvnSyncFinalize(&legacy_key);
 nvn_sync_family_managed_release(token);token=NULL;
 for(unsigned i=0;i<100;i++){
   CHECK(nvnSyncInitialize(&key,(void*)1));CHECK(nvn_sync_family_managed_acquire(&key,&token));
   nvnSyncFinalize(&key);nvn_sync_family_managed_release(token);token=NULL;
 }
 CHECK(legacy->fence==111&&legacy->fenced);
 printf("sync_managed_contract cycles=1100000 held_reference=preserved legacy_pointer=preserved generations_before=%" PRIu64 " after=%" PRIu64 "\n",before.retained_generations,after.retained_generations);
 return 0;
}
