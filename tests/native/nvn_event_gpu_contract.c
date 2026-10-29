/* Production event-family contract with a controlled completion provider.
 * The provider models deferred GPU publication into the actual pool word;
 * it does not claim execution on graphics hardware. No other event-family
 * implementation is substituted. Run on host and compile for RG477M. */
#include "../../src/graphics/nvn_device/nvn_event.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static unsigned errors,drains,reentrant_queries;
#define CHECK(c) do { if(!(c)){fprintf(stderr,"FAIL line=%d %s\n",__LINE__,#c);++errors;} } while(0)
struct Pool {uint8_t *mem;uint64_t size;int live;};
static _Alignas(16) uint8_t memory[128];
static struct Pool pool={memory,sizeof memory,1},invalid={0};
/* This provider is the explicit pool boundary of this component test. */
int nvn_memory_pool_info_of(const void *p,nvn_memory_pool_info *out) {
    if(p!=&pool&&p!=&invalid)return 0;const struct Pool *value=p;
    if(!value->live)return 0;*out=(nvn_memory_pool_info){value->mem,value->size,0,1};return 1;
}
int nvn_engine_ready(void) {return 1;}
static struct {const volatile uint32_t *address;uint32_t value;} pending[4];
static unsigned pending_count;
static void defer(uint32_t *word,uint32_t value) {
    CHECK(pending_count<4);pending[pending_count].address=word;pending[pending_count++].value=value;
}
void mtl_event_read(const volatile uint32_t *address) {
    ++drains;
    /* Deadlocks if the event family calls the completion provider while its
     * table lock is held; this is a deliberate reentrant read of that table. */
    nvn_event_stats stats;nvn_event_get_stats(&stats);++reentrant_queries;
    for(unsigned i=0;i<pending_count;){
        if(pending[i].address==address){__atomic_store_n((uint32_t *)(uintptr_t)address,pending[i].value,__ATOMIC_SEQ_CST);pending[i]=pending[--pending_count];}
        else ++i;
    }
}
static void *entry(const char *name) {
    for(size_t i=0;i<nvn_event_family_count();++i){const nvn_event_family_entry *e=nvn_event_family_entry_at(i);if(!strcmp(e->name,name))return e->fn;}
    fprintf(stderr,"missing production event entry %s\n",name);return NULL;
}
static int canaries(void) {
    for(size_t i=0;i<sizeof memory;i++)if((i<16||i>=20)&&(i<32||i>=36)&&memory[i]!=0xcc)return 0;return 1;
}
int main(void) {
    void (*defaults)(void *)=entry("nvnEventBuilderSetDefaults");
    void (*storage)(void *,void *,int64_t)=entry("nvnEventBuilderSetStorage");
    int (*initialize)(void *,const void *)=entry("nvnEventInitialize");
    void (*finalize)(void *)=entry("nvnEventFinalize");
    void (*signal)(void *,int,uint32_t)=entry("nvnEventSignal");
    uint32_t (*get)(const void *)=entry("nvnEventGetValue");
    if(!defaults||!storage||!initialize||!finalize||!signal||!get)return 2;
    unsigned char builder[64],event[64],alias[64],unknown[64];
    memset(builder,0xaa,sizeof builder);memset(event,0xaa,sizeof event);memset(alias,0xaa,sizeof alias);memset(unknown,0xaa,sizeof unknown);memset(memory,0xcc,sizeof memory);
    uint32_t *first=(uint32_t *)(void *)(memory+16),*second=(uint32_t *)(void *)(memory+32);
    __atomic_store_n(first,0x31415926u,__ATOMIC_SEQ_CST);
    defaults(builder);storage(builder,&pool,16);
    unsigned before=drains;CHECK(initialize(event,builder)==1);CHECK(drains==before+1);CHECK(*first==0x31415926u);
    uint64_t address=0;CHECK(nvn_event_value_address_of(event,&address));CHECK(address==(uintptr_t)first);
    defer(first,99);CHECK(get(event)==99);CHECK(!pending_count);
    defer(first,100);signal(event,1,7);CHECK(*first==107);CHECK(get(event)==107);CHECK(!pending_count);
    defer(first,300);signal(event,0,12);CHECK(*first==12);CHECK(!pending_count);
    CHECK(initialize(alias,builder)==1);signal(alias,1,8);CHECK(get(event)==20);
    finalize(alias);
    /* A rebind drains both old and new storage, without overwriting either. */
    defer(first,111);defer(second,222);storage(builder,&pool,32);
    before=drains;CHECK(initialize(event,builder)==1);CHECK(drains==before+2);CHECK(*first==111&&get(event)==222);CHECK(!pending_count);
    defer(second,0xdeadbeefu);before=drains;finalize(event);CHECK(drains==before+1);CHECK(*second==0xdeadbeefu&&pending_count==0);
    address=0x1234;CHECK(!nvn_event_value_address_of(event,&address));CHECK(address==0x1234);CHECK(get(event)==0);
    const int64_t offsets[]={-4,1,2,126,128,132,INT64_MAX};
    for(size_t i=0;i<sizeof offsets/sizeof offsets[0];i++){storage(builder,&pool,offsets[i]);before=drains;CHECK(!initialize(unknown,builder));CHECK(drains==before);}
    invalid=(struct Pool){(uint8_t *)(uintptr_t)(UINTPTR_MAX-3),16,1};storage(builder,&invalid,4);CHECK(!initialize(unknown,builder));
    invalid=(struct Pool){memory+1,16,1};storage(builder,&invalid,0);CHECK(!initialize(unknown,builder));
    invalid=(struct Pool){memory,3,1};storage(builder,&invalid,0);CHECK(!initialize(unknown,builder));
    storage(builder,&pool,16);CHECK(initialize(event,builder));pool.live=0;before=drains;CHECK(get(event)==0);signal(event,0,42);CHECK(drains==before);CHECK(*first==111);pool.live=1;finalize(event);
    CHECK(canaries());for(size_t i=0;i<sizeof event;i++)CHECK(event[i]==0xaa&&alias[i]==0xaa&&unknown[i]==0xaa&&builder[i]==0xaa);
    CHECK(drains==reentrant_queries&&drains>10);
    printf("nvn_event_gpu_contract errors=%u completion_drains=%u reentrant_queries=%u real_GPU_executed=0\n",errors,drains,reentrant_queries);
    return errors!=0;
}
