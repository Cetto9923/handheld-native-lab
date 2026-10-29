/* Actual sync-family pointers escape into FenceSync's 8-byte wire payload
 * (nvn_command_buffer.c:1794). Exercise the deferred engine write after growth,
 * reinitialization and finalization. The engine-wait fixture below owns a real
 * condition variable; this is a host lifetime/dispatch contract, not GPU QA. */
#define _POSIX_C_SOURCE 200809L
#include "../../src/graphics/nvn_device/nvn_sync.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <stdatomic.h>
extern int nvnSyncInitialize(void*,void*);
extern void nvnSyncFinalize(void*);
extern int nvnSyncWait(const void*,uint64_t);
static int errors,engine=1;
static uint64_t completed,last_waited,last_timeout;
static unsigned wait_calls;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ready=PTHREAD_COND_INITIALIZER;
int nvn_engine_ready(void) {return engine;}
uint64_t nvn_engine_last_fence(void) {return 88;}
int nvn_engine_wait(uint64_t fence,uint64_t timeout) {
    pthread_mutex_lock(&lock);wait_calls++;last_waited=fence;last_timeout=timeout;
    while(completed<fence&&timeout==UINT64_MAX)pthread_cond_wait(&ready,&lock);
    int result=completed>=fence;pthread_mutex_unlock(&lock);return result;
}
#define CHECK(c) do {if(!(c)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);errors++;}}while(0)
static void record(uint8_t payload[8],ffrs_sync *object) {uint64_t value=(uintptr_t)object;memcpy(payload,&value,8);}
static void replay(const uint8_t payload[8],uint64_t fence) {ffrs_sync *object=NULL;memcpy(&object,payload,8);object->fence=fence;__atomic_store_n(&object->fenced,1u,__ATOMIC_RELEASE);}
static void *complete_later(void *unused) {(void)unused;struct timespec delay={0,10000000};nanosleep(&delay,NULL);pthread_mutex_lock(&lock);completed=88;pthread_cond_broadcast(&ready);pthread_mutex_unlock(&lock);return NULL;}
int main(int argc,char **argv) {
#if defined(NVN_SYNC_MAX_GENERATIONS)
    if(argc==2&&!strcmp(argv[1],"--budget")){
        uint64_t key=0,another_key=0;CHECK(nvnSyncInitialize(&key,(void*)1)==1);
        ffrs_sync *first=nvn_sync_family_object(&key);if(!first)return 2;
        first->fence=101;first->fenced=1;
        for(unsigned i=1;i<NVN_SYNC_MAX_GENERATIONS;i++)CHECK(nvnSyncInitialize(&key,(void*)1)==1);
        ffrs_sync *current=nvn_sync_family_object(&key);if(!current)return 2;
        current->fence=202;current->fenced=1;
        CHECK(nvnSyncInitialize(&key,(void*)2)==0);CHECK(nvn_sync_family_object(&key)==current);
        CHECK(nvn_sync_family_fence_value(&key)==202&&nvn_sync_family_is_fenced(&key));
        CHECK(first->fence==101&&first->fenced==1);CHECK(nvnSyncInitialize(&another_key,(void*)1)==0);
        nvn_sync_stats stats;nvn_sync_get_stats(&stats);CHECK(stats.retained_generations==NVN_SYNC_MAX_GENERATIONS);
        CHECK(stats.snapshot_bytes==UINT64_C(16)*1024*1024);
        printf("sync_snapshot_budget errors=%d retained=%llu payload_bytes=%llu preserved_old=1\n",errors,(unsigned long long)stats.retained_generations,(unsigned long long)stats.snapshot_bytes);
        return errors!=0;
    }
#else
    (void)argc;(void)argv;
#endif
    uint64_t keys[1100]={0};uint8_t payload[8];
    CHECK(nvnSyncInitialize(&keys[0],(void*)1)==1);ffrs_sync *first=nvn_sync_family_object(&keys[0]);if(!first)return 2;
    record(payload,first);replay(payload,17);
    for(unsigned i=1;i<1100;i++)CHECK(nvnSyncInitialize(&keys[i],(void*)1)==1);
    /* ASan fails here on the prior table-embedded object implementation. */
    CHECK(first->fence==17&&first->fenced==1);CHECK(nvn_sync_family_object(&keys[0])==first);replay(payload,19);CHECK(nvn_sync_family_fence_value(&keys[0])==19);
    CHECK(nvnSyncInitialize(&keys[0],(void*)1)==1);ffrs_sync *second=nvn_sync_family_object(&keys[0]);CHECK(second&&second!=first);
    CHECK(first->fence==19&&first->fenced==1);CHECK(second->fence==0&&second->fenced==0);
    replay(payload,37);CHECK(second->fence==0&&second->fenced==0);CHECK(first->fence==37);
    uint8_t second_payload[8];record(second_payload,second);replay(second_payload,53);nvnSyncFinalize(&keys[0]);
    CHECK(second->fence==53&&second->fenced==1);CHECK(!nvn_sync_family_is_fenced(&keys[0]));CHECK(nvnSyncWait(&keys[0],0)==2);
    CHECK(nvnSyncInitialize(&keys[0],(void*)1)==1);ffrs_sync *third=nvn_sync_family_object(&keys[0]);CHECK(third&&third!=second&&third!=first);
    replay(second_payload,67);CHECK(third->fence==0&&!third->fenced);CHECK(second->fence==67&&first->fence==37);
    /* Cross the stable allocation slab boundary while old wire payloads live. */
    for(unsigned i=0;i<4200;i++)CHECK(nvnSyncInitialize(&keys[1099],(void*)1)==1);
    CHECK(first->fence==37&&second->fence==67);CHECK(nvn_sync_family_object(&keys[0])==third);
    uint8_t third_payload[8];record(third_payload,third);replay(third_payload,88);
    CHECK(nvnSyncWait(&keys[0],0)==2);CHECK(last_waited==88&&last_timeout==0&&wait_calls>=2);
    pthread_t thread;CHECK(pthread_create(&thread,NULL,complete_later,NULL)==0);
    CHECK(nvnSyncWait(&keys[0],UINT64_MAX)==1);CHECK(last_waited==88&&last_timeout==UINT64_MAX);pthread_join(thread,NULL);
    CHECK(nvnSyncWait(&keys[0],0)==0);
#if defined(NVN_SYNC_MAX_GENERATIONS)
    nvn_sync_stats stats;nvn_sync_get_stats(&stats);CHECK(stats.retained_generations>=5302);CHECK(stats.generation_limit==NVN_SYNC_MAX_GENERATIONS);
    CHECK(stats.snapshot_bytes>=stats.retained_generations*sizeof(ffrs_sync));
    printf("retained_generations=%llu allocated_snapshot_bytes=%llu limit=%llu\n",(unsigned long long)stats.retained_generations,(unsigned long long)stats.snapshot_bytes,(unsigned long long)stats.generation_limit);
#endif
    for(unsigned i=0;i<1100;i++)CHECK(keys[i]==0);
    printf("sync_snapshot_contract errors=%d live_keys=1100 old_generations_preserved=1 engine_wait_calls=%u\n",errors,wait_calls);return errors!=0;
}
