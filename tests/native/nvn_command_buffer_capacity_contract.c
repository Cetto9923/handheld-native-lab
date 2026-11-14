/* CPU recorder ownership contract. The included production implementation
 * exposes stable slot addresses to this test. Platform dependencies below
 * refuse unused operations. No GPU, fence or guest execution is simulated. */
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail_chunk_allocation;
static size_t chunk_allocations;
static void *capacity_calloc(size_t count,size_t size);
#define calloc capacity_calloc
#ifndef NVN_CAPACITY_SOURCE
#define NVN_CAPACITY_SOURCE "../../src/graphics/nvn_device/nvn_command_buffer.c"
#endif
#include NVN_CAPACITY_SOURCE
#undef calloc

static void *capacity_calloc(size_t count,size_t size){
    if(count==1 && size==sizeof(nvn_cb_buffer_chunk)){
        if(fail_chunk_allocation){fail_chunk_allocation=0;return NULL;}
        chunk_allocations++;
    }
    return calloc(count,size);
}

int nvn_trace_enabled(void){return 0;}
const ffrs_tex *nvn_bridge_texture(const void *object){(void)object;abort();}
const ffrs_prog *nvn_bridge_program(const void *object){(void)object;abort();}
const ffrs_dpool *nvn_bridge_texture_pool(const void *object){(void)object;abort();}
const ffrs_dpool *nvn_bridge_sampler_pool(const void *object){(void)object;abort();}
int nvn_texture_pool_base(const void *object){(void)object;abort();}
int nvn_sampler_pool_base(const void *object){(void)object;abort();}
int nvn_texture_view_info_of(const void *object,ffrs_view *out,uint64_t *serial){
    (void)object;(void)out;(void)serial;abort();
}
int nvn_memory_pool_info_of(const void *object,nvn_memory_pool_info *out){(void)object;(void)out;abort();}
int nvn_event_value_address_of(const void *object,uint64_t *out){(void)object;(void)out;abort();}
ffrs_sync *nvn_sync_family_managed_acquire(const void *object,void **token){(void)object;(void)token;abort();}
void nvn_sync_family_managed_release(void *token){(void)token;abort();}
void nvn_engine_pool_del(void *object){(void)object;}

enum { INITIAL_BUFFERS=96, CALLBACK_BUFFERS=64, THREAD_COUNT=8, PER_THREAD=32 };
static unsigned char initial[INITIAL_BUFFERS][64];
static unsigned char replacements[2][64];
static unsigned char callback_objects[CALLBACK_BUFFERS][64];
static unsigned char concurrent_objects[THREAD_COUNT][PER_THREAD][64];
static unsigned char common_object[64];
static uint64_t handles[INITIAL_BUFFERS];
static nvn_cb_buffer *original_slots[INITIAL_BUFFERS];
static nvn_command_package original_packages[INITIAL_BUFFERS];
static uint8_t *original_bytes[INITIAL_BUFFERS];
static void *callback_object;
static nvn_cb_buffer *callback_slot;
static unsigned callback_count;

static size_t capacity(void){
    size_t result=0;
    for(nvn_cb_buffer_chunk *chunk=&s_buffer_head;chunk;chunk=chunk->next)result+=NVN_CB_BUFFERS_PER_CHUNK;
    return result;
}

static void assert_package(size_t index){
    nvn_command_package package;
    assert(nvn_command_buffer_get_package(handles[index],&package));
    assert(package.handle==original_packages[index].handle);
    assert(package.records==original_packages[index].records);
    assert(package.bytes==original_packages[index].bytes);
    assert(package.record_count==original_packages[index].record_count);
    assert(package.segment_count==original_packages[index].segment_count);
    assert(!memcmp(package.records,original_bytes[index],package.bytes));
}

static int grow_from_memory_callback(void *entry,const uint64_t *arguments,uint64_t *result){
    assert(entry==(void *)(uintptr_t)0x1234);
    assert(arguments[0]==(uint64_t)(uintptr_t)callback_object);
    assert(arguments[1]==0 && arguments[2]>0 && arguments[3]==0xabcdef);
    assert(callback_count++==0);
    size_t before=capacity();
    for(size_t i=0;i<CALLBACK_BUFFERS;++i)assert(nvn_command_buffer_initialize(callback_objects[i]));
    assert(capacity()>before);
    assert(buffer_find(callback_object,0)==callback_slot);
    assert(callback_slot->recording && callback_slot->memory_callback_data==0xabcdef);
    nvn_command_buffer_add_command_memory(callback_object,1024);
    if(result)*result=0;
    return NVN_CB_GUEST_CALL_OK;
}

static void *initialize_concurrently(void *argument){
    size_t thread=(size_t)(uintptr_t)argument;
    for(size_t i=0;i<PER_THREAD;++i){
        assert(nvn_command_buffer_initialize(common_object));
        assert(nvn_command_buffer_initialize(concurrent_objects[thread][i]));
        nvn_command_buffer_set_copy_row_stride(concurrent_objects[thread][i],(int64_t)(thread*PER_THREAD+i+1));
    }
    return NULL;
}

int main(void){
    nvn_command_buffer_stats stats;
    memset(initial,0xaa,sizeof(initial));
    for(size_t i=0;i<INITIAL_BUFFERS;++i){
        assert(nvn_command_buffer_initialize(initial[i]));
        original_slots[i]=buffer_find(initial[i],0);
        assert(original_slots[i]);
        nvn_command_buffer_add_command_memory(initial[i],1024);
        nvn_command_buffer_begin_recording(initial[i]);
        float color[4]={(float)i,0.25f,0.5f,1.0f};
        nvn_command_buffer_rec_clear_color(initial[i],(int)i,color,15,0);
        memset(color,0,sizeof(color));
        handles[i]=nvn_command_buffer_end_recording(initial[i]);
        assert(handles[i] && nvn_command_buffer_get_package(handles[i],&original_packages[i]));
        assert(original_packages[i].record_count==2);
        original_bytes[i]=malloc(original_packages[i].bytes);assert(original_bytes[i]);
        memcpy(original_bytes[i],original_packages[i].records,original_packages[i].bytes);
        const nvn_cb_p_clear_color *clear=(const nvn_cb_p_clear_color *)(original_packages[i].records+sizeof(nvn_cb_op_hdr));
        assert(clear->index==(int)i && clear->rgba[0]==(float)i && clear->rgba[3]==1.0f);
    }
    assert(capacity()==INITIAL_BUFFERS && chunk_allocations==2);
    nvn_command_buffer_get_stats(&stats);
    assert(stats.live_buffers==INITIAL_BUFFERS && stats.records==INITIAL_BUFFERS*2);
    for(size_t i=0;i<sizeof(initial);++i)assert(((unsigned char *)initial)[i]==0xaa);

    /* A match in a later chunk wins over an earlier finalized slot. */
    nvn_command_buffer_finalize(initial[0]);
    assert(nvn_command_buffer_initialize(initial[64]));
    assert(buffer_find(initial[64],0)==original_slots[64]);
    assert(buffer_find(initial[0],0)==original_slots[0] && original_slots[0]->finalized);
    assert(nvn_command_buffer_initialize(initial[0]));
    for(size_t i=0;i<INITIAL_BUFFERS;++i)assert_package(i);

    /* A failed allocation leaves all live state and immutable packages intact. */
    unsigned char failure_object[64];
    fail_chunk_allocation=1;
    assert(!nvn_command_buffer_initialize(failure_object));
    assert(!fail_chunk_allocation && capacity()==INITIAL_BUFFERS && chunk_allocations==2);
    assert(!buffer_find(failure_object,0));
    for(size_t i=0;i<INITIAL_BUFFERS;++i){
        assert(buffer_find(initial[i],0)==original_slots[i]);
        assert_package(i);
    }
    assert(!nvn_command_buffer_initialize(NULL));

    /* Reuse the oldest finalized slots without growing or invalidating handles. */
    nvn_command_buffer_finalize(initial[0]);
    nvn_command_buffer_finalize(initial[1]);
    assert(nvn_command_buffer_initialize(replacements[0]));
    assert(nvn_command_buffer_initialize(replacements[1]));
    assert(buffer_find(replacements[0],0)==original_slots[0]);
    assert(buffer_find(replacements[1],0)==original_slots[1]);
    assert(!buffer_find(initial[0],0) && !buffer_find(initial[1],0));
    assert(capacity()==INITIAL_BUFFERS && chunk_allocations==2);
    for(size_t i=0;i<INITIAL_BUFFERS;++i)assert_package(i);

    /* Trigger the real out-of-memory callback, which reenters Initialize and
     * expands the registry while the outer recording is suspended. */
    callback_object=initial[2];callback_slot=original_slots[2];
    assert(nvn_command_buffer_initialize(callback_object));
    nvn_command_buffer_add_command_memory(callback_object,8);
    nvn_command_buffer_set_memory_callback(callback_object,0x1234);
    nvn_command_buffer_set_memory_callback_data(callback_object,0xabcdef);
    nvn_command_buffer_set_guest_call(grow_from_memory_callback);
    nvn_command_buffer_begin_recording(callback_object);
    float color[4]={7,8,9,10};
    nvn_command_buffer_rec_clear_color(callback_object,7,color,15,0);
    uint64_t callback_handle=nvn_command_buffer_end_recording(callback_object);
    nvn_command_package callback_package;
    assert(callback_count==1 && callback_handle);
    assert(nvn_command_buffer_get_package(callback_handle,&callback_package));
    assert(callback_package.record_count==2);
    const nvn_cb_p_clear_color *clear=(const nvn_cb_p_clear_color *)(callback_package.records+sizeof(nvn_cb_op_hdr));
    assert(clear->index==7 && !memcmp(clear->rgba,color,sizeof(color)));
    assert(buffer_find(callback_object,0)==callback_slot);
    nvn_command_buffer_set_guest_call(NULL);

    pthread_t threads[THREAD_COUNT];
    for(size_t i=0;i<THREAD_COUNT;++i)assert(!pthread_create(&threads[i],NULL,initialize_concurrently,(void *)(uintptr_t)i));
    for(size_t i=0;i<THREAD_COUNT;++i)assert(!pthread_join(threads[i],NULL));
    nvn_command_buffer_get_stats(&stats);
    assert(stats.live_buffers==INITIAL_BUFFERS+CALLBACK_BUFFERS+THREAD_COUNT*PER_THREAD+1);
    for(size_t thread=0;thread<THREAD_COUNT;++thread)
        for(size_t i=0;i<PER_THREAD;++i)
            assert(nvn_command_buffer_get_copy_row_stride(concurrent_objects[thread][i])==(int64_t)(thread*PER_THREAD+i+1));
    size_t common_count=0;
    for(nvn_cb_buffer_chunk *chunk=&s_buffer_head;chunk;chunk=chunk->next)
        for(size_t i=0;i<NVN_CB_BUFFERS_PER_CHUNK;++i)
            common_count+=chunk->slots[i].in_use && chunk->slots[i].object==common_object;
    assert(common_count==1);
    for(size_t i=0;i<INITIAL_BUFFERS;++i){assert_package(i);free(original_bytes[i]);}
    for(size_t i=2;i<INITIAL_BUFFERS;++i)assert(buffer_find(initial[i],0)==original_slots[i]);
    printf("command buffer capacity PASS live=%d capacity=%zu packages=%zu callback=%u\n",
           stats.live_buffers,capacity(),s_package_count,callback_count);
    return 0;
}
