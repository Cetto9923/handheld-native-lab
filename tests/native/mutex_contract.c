#include "../../src/runtime/hle/mutex.h"
#include <inttypes.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
enum {THREADS=8,ITERATIONS=10000};
typedef struct object { _Alignas(8) unsigned char bytes[32];} object;
static object shared;
static uint64_t count;
static atomic_uint inside,overlaps;
static pthread_mutex_t control=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition=PTHREAD_COND_INITIALIZER;
static int ready,finished;
static int contender_result;
static uint32_t u32(const object *o,unsigned off) {uint32_t n;memcpy(&n,o->bytes+off,4);return n;}
static uintptr_t pointer(const object *o,unsigned off) {uintptr_t n;memcpy(&n,o->bytes+off,8);return n;}
static unsigned padding_errors(const object *o) {
    unsigned errors=0;unsigned offsets[]={2,3,12,13,14,15,28,29,30,31};
    for(unsigned i=0;i<sizeof(offsets)/sizeof(*offsets);i++)if(o->bytes[offsets[i]]!=0xa5)errors++;
    return errors;
}
static void *worker(void *unused) {
    (void)unused;
    uintptr_t before,after;
    __asm__ volatile("mrs %0, tpidrro_el0":"=r"(before));
    unsigned errors=0;
    for(unsigned i=0;i<ITERATIONS;i++) {
        nso_host_lock_mutex(&shared);
        if(atomic_fetch_add_explicit(&inside,1,memory_order_relaxed))atomic_fetch_add(&overlaps,1);
        if(!nso_host_is_mutex_locked_by_current_thread(&shared) ||
           pointer(&shared,16)!=nso_host_mutex_thread_token() || u32(&shared,8)!=1)errors++;
        count++;
        atomic_fetch_sub_explicit(&inside,1,memory_order_relaxed);
        nso_host_unlock_mutex(&shared);
    }
    __asm__ volatile("mrs %0, tpidrro_el0":"=r"(after));
    if(before!=after)errors++;
    return (void*)(uintptr_t)errors;
}
static void *contender(void *p) {
    pthread_mutex_lock(&control);
    ready=1;pthread_cond_signal(&condition);
    pthread_mutex_unlock(&control);
    contender_result=nso_host_try_lock_mutex(p);
    if(contender_result)nso_host_unlock_mutex(p);
    pthread_mutex_lock(&control);
    finished=1;pthread_cond_signal(&condition);
    pthread_mutex_unlock(&control);
    return NULL;
}
int main(void) {
    unsigned errors=0;
    memset(&shared,0xa5,sizeof(shared));
    nso_host_initialize_mutex(&shared,false,0x608);
    if(shared.bytes[0]!=1 || shared.bytes[1]!=0 || u32(&shared,4)!=0x608 ||
       u32(&shared,8) || pointer(&shared,16) || u32(&shared,24))errors++;
    errors+=padding_errors(&shared);
    nso_host_lock_mutex(&shared);
    if(nso_host_try_lock_mutex(&shared))errors++;
    pthread_t other;
    if(pthread_create(&other,NULL,contender,&shared))return 1;
    pthread_mutex_lock(&control);
    while(!ready || !finished)pthread_cond_wait(&condition,&control);
    pthread_mutex_unlock(&control);
    pthread_join(other,NULL);
    if(contender_result)errors++;
    nso_host_unlock_mutex(&shared);
    if(!nso_host_try_lock_mutex(&shared))errors++;
    nso_host_unlock_mutex(&shared);
    pthread_t threads[THREADS];
    for(unsigned i=0;i<THREADS;i++)if(pthread_create(&threads[i],NULL,worker,NULL))return 1;
    for(unsigned i=0;i<THREADS;i++) {
        void *result;if(pthread_join(threads[i],&result))return 1;
        errors+=(unsigned)(uintptr_t)result;
    }
    if(count!=(uint64_t)THREADS*ITERATIONS || atomic_load(&overlaps) || pointer(&shared,16) ||
       u32(&shared,8) || u32(&shared,24))errors++;
    errors+=padding_errors(&shared);
    nso_host_finalize_mutex(&shared);
    if(shared.bytes[0])errors++;
    object recursive;
    memset(&recursive,0xa5,sizeof(recursive));
    nso_host_initialize_mutex(&recursive,true,-7);
    nso_host_lock_mutex(&recursive);nso_host_lock_mutex(&recursive);
    if(!nso_host_try_lock_mutex(&recursive) || u32(&recursive,8)!=3 ||
       pointer(&recursive,16)!=nso_host_mutex_thread_token())errors++;
    nso_host_unlock_mutex(&recursive);nso_host_unlock_mutex(&recursive);
    if(u32(&recursive,8)!=1 || !nso_host_is_mutex_locked_by_current_thread(&recursive))errors++;
    nso_host_unlock_mutex(&recursive);
    if(nso_host_is_mutex_locked_by_current_thread(&recursive))errors++;
    errors+=padding_errors(&recursive);
    nso_host_finalize_mutex(&recursive);
    for(unsigned i=0;i<100;i++) {
        nso_host_initialize_mutex(&recursive,(i&1)!=0,(int)i);
        nso_host_lock_mutex(&recursive);nso_host_unlock_mutex(&recursive);
        nso_host_finalize_mutex(&recursive);
    }
    printf("{\"threads\":8,\"increments\":%" PRIu64 ",\"overlaps\":%u,\"recursive_depth_tested\":3,"
           "\"trylock_contended\":%d,\"lifecycle_reuses\":100,\"errors\":%u}\n",
           count,atomic_load(&overlaps),contender_result,errors);
    return errors?1:0;
}
