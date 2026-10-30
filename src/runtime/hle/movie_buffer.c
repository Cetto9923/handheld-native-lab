/* Input SHA50c488fb55d497b2a50b75fb8dee6ef6e8eccc31cd45510b580011490014a058.
 * Original Buffer methods VA127f6c..128190. No original bytes in this source.
 * The object is directly readable by game code; no pointer-keyed shadow state.
 */
#include "movie_buffer.h"
#include "nso_host.h"
#include "guest_process.h"
#include "../nso/native_boundary.h"
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdatomic.h>
/* Registration applies to movie workers, not the game thread making this call.
 * No claim that the platform MediaCodec's private driver threads inherit it. */
static _Atomic uint64_t core_mask=7;
static _Atomic int thread_priority=16;
uint64_t ffrs_movie_core_mask(void){return atomic_load(&core_mask);}
int ffrs_movie_thread_priority(void){return atomic_load(&thread_priority);}
static void set_core_mask(uint64_t mask){atomic_store(&core_mask,mask);}
static void set_thread_priority(int priority){atomic_store(&thread_priority,priority);}
static int get_thread_priority(void){return ffrs_movie_thread_priority()&255;}
typedef struct {
    int32_t offset, size, data32; uint32_t udata32;
    uint8_t owned, padding[7]; uint64_t capacity, data64;
    void *base, *device, *sync;
} movie_buffer;
_Static_assert(sizeof(movie_buffer)==64 && offsetof(movie_buffer,base)==0x28 &&
               offsetof(movie_buffer,owned)==0x10 && offsetof(movie_buffer,sync)==0x38,
               "movie Buffer ABI");
static void construct(movie_buffer *b) {
    /* Match stores, including the original's untouched alignment padding. */
    memset(b,0,16); b->owned=0;
    memset((char*)b+0x18,0,0x28);
}
static void release_owned(movie_buffer *b) {
    if(!b->owned || !b->base)return;
    movie_free_fn fn=nso_host_movie_get_deallocator();
    nb_scope *scope=guest_process_current_boundary();
    nb_registers p={.x={(uintptr_t)b->base,(uintptr_t)nso_host_movie_get_allocator_userdata()}};
    if(!fn || !scope || nb_call_from_host(scope,(void(*)(void))fn,&p)!=NB_OK)abort();
    b->base=NULL;
}
static void destroy(movie_buffer *b) {release_owned(b);}
static void set_range(movie_buffer *b,int32_t offset,int32_t size) {b->offset=offset;b->size=size;}
static void set_data(movie_buffer *b,void *data,uint64_t capacity) {
    if(b->owned){release_owned(b);b->owned=0;}
    b->base=data;b->capacity=capacity;
}
static void *base(const movie_buffer *b) {return b->base;}
static uint64_t capacity(const movie_buffer *b) {return b->capacity;}
static int32_t size(const movie_buffer *b) {return b->size;}
static int32_t offset(const movie_buffer *b) {return b->offset;}
static void device(movie_buffer *b,void *p) {b->device=p;}
static void sync_value(movie_buffer *b,void *p) {b->sync=p;}
uint64_t ffrs_movie_buffer_lookup(const char *name,unsigned *args,unsigned *returns) {
    if(!name)return 0;
    struct entry {const char *name;void (*fn)(void);unsigned args,returns;};
    static const struct entry entries[]={
        {"_ZN5movie11SetCoreMaskEm",(void(*)(void))set_core_mask,1,0},
        {"_ZN5movie17SetThreadPriorityEi",(void(*)(void))set_thread_priority,1,0},
        {"_ZN5movie17GetThreadPriorityEv",(void(*)(void))get_thread_priority,0,1},
        {"_ZN5movie6BufferC1Ev",(void(*)(void))construct,1,0},
        {"_ZN5movie6BufferD1Ev",(void(*)(void))destroy,1,0},
        {"_ZN5movie6Buffer8SetRangeEii",(void(*)(void))set_range,3,0},
        {"_ZN5movie6Buffer18SetDataAndCapacityEPvm",(void(*)(void))set_data,3,0},
        {"_ZNK5movie6Buffer4BaseEv",(void(*)(void))base,1,1},
        {"_ZNK5movie6Buffer8CapacityEv",(void(*)(void))capacity,1,1},
        {"_ZNK5movie6Buffer4SizeEv",(void(*)(void))size,1,1},
        {"_ZNK5movie6Buffer6OffsetEv",(void(*)(void))offset,1,1},
        {"_ZN5movie6Buffer9SetNvnDevEPv",(void(*)(void))device,2,0},
        {"_ZN5movie6Buffer10SetNvnSyncEPv",(void(*)(void))sync_value,2,0}
    };
    for(size_t i=0;i<sizeof(entries)/sizeof(entries[0]);i++)if(!strcmp(name,entries[i].name)){
        if(args)*args=entries[i].args;if(returns)*returns=entries[i].returns;
        return (uintptr_t)entries[i].fn;
    }
    return 0;
}
