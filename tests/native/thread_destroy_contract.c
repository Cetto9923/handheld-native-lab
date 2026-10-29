/* Ordinary native C workers through the real nso_host thread functions.
 * No NSO, binary analysis, guest gateway, device, or fake worker completion.
 * Compile ONLY nso_host.c with pthread_join=contract_pthread_join and
 * pthread_cond_wait=contract_pthread_cond_wait. Fixture wrappers preserve the
 * real blocking calls and expose deterministic entry/return barriers. Link
 * actual guest_tp.c/module_tls.c/exit_registry.c for native TLS cleanup. */
#define _POSIX_C_SOURCE 200809L
#include "../../src/runtime/hle/nso_host.h"
#include "../../src/runtime/hle/guest_tp.h"
#include <stdatomic.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
/* Native fixture bodies use their actual pthread stacks, not a guest image. */
void nso_fault_scope_set_thread_stack(uintptr_t low,uintptr_t high) {(void)low;(void)high;}
/* No module callbacks are registered by this ordinary C fixture. */
int guest_process_invoke_thread_exit(void (*callback)(void*),void *argument) {(void)callback;(void)argument;return 0;}
static pthread_mutex_t gate=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed=PTHREAD_COND_INITIALIZER;
static int blocking_entered,hold_return,actual_join_done,release_return;
static _Thread_local int lifecycle_caller;
static _Atomic unsigned actual_joins,worker_done,wait_done,destroy_done;
static _Alignas(16) unsigned char other_key[0x1c0];
static void entered(void) {pthread_mutex_lock(&gate);blocking_entered=1;pthread_cond_broadcast(&changed);pthread_mutex_unlock(&gate);}
int contract_pthread_cond_wait(pthread_cond_t *condition,pthread_mutex_t *mutex) {
    if(lifecycle_caller)entered();return pthread_cond_wait(condition,mutex);
}
int contract_pthread_join(pthread_t thread,void **value) {
    if(lifecycle_caller)entered();atomic_fetch_add(&actual_joins,1);
    int result=pthread_join(thread,value);
    pthread_mutex_lock(&gate);actual_join_done=1;pthread_cond_broadcast(&changed);
    while(hold_return&&!release_return)pthread_cond_wait(&changed,&gate);
    pthread_mutex_unlock(&gate);return result;
}
static void registry_worker(void *unused) {
    (void)unused;pthread_mutex_lock(&gate);while(!blocking_entered)pthread_cond_wait(&changed,&gate);pthread_mutex_unlock(&gate);
    /* This must acquire the actual production global thread registry. */
    nso_host_change_thread_priority(other_key,7);atomic_store(&worker_done,1);
}
static void *destroyer(void *key) {lifecycle_caller=1;nso_host_destroy_thread(key);atomic_fetch_add(&destroy_done,1);return NULL;}
static void *waiter(void *key) {lifecycle_caller=1;nso_host_wait_thread(key);atomic_fetch_add(&wait_done,1);return NULL;}
static int until(_Atomic unsigned *value,unsigned expected) {
    for(unsigned i=0;i<2000;i++){if(atomic_load(value)>=expected)return 1;struct timespec delay={0,1000000};nanosleep(&delay,NULL);}return 0;
}
static int until_join(void) {
    for(unsigned i=0;i<2000;i++){pthread_mutex_lock(&gate);int done=actual_join_done;pthread_mutex_unlock(&gate);if(done)return 1;struct timespec delay={0,1000000};nanosleep(&delay,NULL);}return 0;
}
static int make_thread(unsigned char *key,void *stack) {
    memset(key,0,0x1c0);return nso_host_create_thread(key,registry_worker,NULL,stack,65536,16)==0&&key[0x40]==1;
}
static void reset(void) {
    pthread_mutex_lock(&gate);blocking_entered=hold_return=actual_join_done=release_return=0;pthread_mutex_unlock(&gate);
    atomic_store(&actual_joins,0);atomic_store(&worker_done,0);atomic_store(&wait_done,0);atomic_store(&destroy_done,0);
}
int main(void) {
    alarm(12);int errors=0;_Alignas(16) unsigned char key[0x1c0];void *stack=NULL;
    if(posix_memalign(&stack,16,65536))return 2;
    reset();if(!make_thread(key,stack))return 2;nso_host_start_thread(key);
    if(key[0x40]!=3)errors++;
    pthread_t destroy;if(pthread_create(&destroy,NULL,destroyer,key))return 2;
    if(!until(&destroy_done,1)){
        fprintf(stderr,"DETERMINISTIC BLOCK: Destroy entered wait/join; worker cannot acquire production thread registry before return\n");return 1;
    }
    pthread_join(destroy,NULL);if(!atomic_load(&worker_done)||atomic_load(&actual_joins)!=1||key[0x40]!=0)errors++;
    /* One real join is held just before its wrapper returns. Bionic may now
     * recycle the joined pthread_t for the following lifecycle callers.
     * Another waiter/Destroy is not a self wait merely because its pthread_t
     * has that old value; it must neither reclaim the pinned record nor be
     * refused. The real join call still executes exactly once. */
    reset();hold_return=1;if(!make_thread(key,stack))return 2;nso_host_start_thread(key);
    pthread_t first,second;if(pthread_create(&first,NULL,waiter,key))return 2;
    if(!until_join()){fprintf(stderr,"first real join did not finish\n");return 1;}
    if(key[0x40]!=4)errors++;
    unsigned char saved[sizeof key];memcpy(saved,key,sizeof key);
    if(nso_host_create_thread(key,registry_worker,NULL,stack,65536,16)==0||memcmp(saved,key,sizeof key))errors++;
    if(pthread_create(&second,NULL,waiter,key)||pthread_create(&destroy,NULL,destroyer,key))return 2;
    struct timespec delay={0,30000000};nanosleep(&delay,NULL);
    if(atomic_load(&wait_done)||atomic_load(&destroy_done)||key[0x40]==0){fprintf(stderr,"premature joined publication or record cleanup\n");errors++;}
    pthread_mutex_lock(&gate);release_return=1;pthread_cond_broadcast(&changed);pthread_mutex_unlock(&gate);
    if(!until(&wait_done,2)||!until(&destroy_done,1)){fprintf(stderr,"concurrent Wait/Destroy did not finish\n");return 1;}
    pthread_join(first,NULL);pthread_join(second,NULL);pthread_join(destroy,NULL);
    if(atomic_load(&actual_joins)!=1||key[0x40]!=0)errors++;
    /* Reuse only after all callers return. More than the host's slot count
     * proves reserved records are eventually retired instead of leaking. */
    for(unsigned i=0;i<80;i++){
        reset();blocking_entered=1;if(!make_thread(key,stack))return 2;nso_host_start_thread(key);
        nso_host_wait_thread(key);nso_host_destroy_thread(key);
        if(!atomic_load(&worker_done)||atomic_load(&actual_joins)!=1||key[0x40]!=0)errors++;
    }
    if(guest_tp_live_mirrors()!=0)errors++;free(stack);
    printf("thread_destroy_contract errors=%d state_bytes=1/3/4/0 registry_progress=1 concurrent_wait_destroy=1 one_real_join=1 reserved_create_refused=1 reuse_cycles=80 live_tls=%zu\n",errors,guest_tp_live_mirrors());return errors!=0;
}
