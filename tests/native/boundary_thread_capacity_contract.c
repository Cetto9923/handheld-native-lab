#define _GNU_SOURCE
#include "../../src/runtime/nso/native_boundary.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
static nb_scope scope;
static nb_import binding;
static pthread_barrier_t barrier;
static _Atomic unsigned arrived;
static int errors;
static uint64_t host_target(uint64_t value) {
    atomic_fetch_add(&arrived,1);pthread_barrier_wait(&barrier);return value+7;
}
__attribute__((naked)) static void guest_entry(void) {
    __asm__("stp x29,x30,[sp,#-16]!\nmov x29,sp\nmov x18,#0x1234\nblr x1\nldp x29,x30,[sp],#16\nret\n");
}
static void *thread_main(void *arg) {
    void *stack=mmap(NULL,65536,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if(stack==MAP_FAILED)return (void*)1;
    nb_registers registers={.x={(uintptr_t)arg,(uintptr_t)nb_import_entry(&binding)}};
    nb_status status=nb_call_child(&scope,guest_entry,&registers,(uintptr_t)stack+65536);
    munmap(stack,65536);
    return (void*)(uintptr_t)(status!=NB_OK || registers.x[0]!=(uintptr_t)arg+7);
}
int main(void) {
    alarm(8);
    if(nb_scope_init(&scope)!=NB_OK)return 2;
    nb_signature signature={.version=1,.flags=NB_SIG_REGISTER_ONLY,.gp_arguments=1,.gp_returns=1};
    if(nb_import_bind(&binding,&scope,(void(*)(void))host_target,&signature)!=NB_OK)return 2;
    pthread_barrier_init(&barrier,NULL,NB_MAX_GUEST_THREADS+1);
    pthread_t threads[NB_MAX_GUEST_THREADS];
    for(unsigned i=0;i<NB_MAX_GUEST_THREADS;i++)if(pthread_create(&threads[i],NULL,thread_main,(void*)(uintptr_t)i))return 2;
    for(unsigned wait=0;atomic_load(&arrived)<NB_MAX_GUEST_THREADS && wait<3000;wait++)usleep(1000);
    if(atomic_load(&arrived)!=NB_MAX_GUEST_THREADS){fprintf(stderr,"Only %u child gateways arrived\n",atomic_load(&arrived));return 1;}
    /* All 64 slots are live inside their real host gateways. The next
     * registration must refuse capacity, not run beyond the table. */
    if(nb_register_current_thread(&scope,0)!=NULL)errors++;
    pthread_barrier_wait(&barrier);
    for(unsigned i=0;i<NB_MAX_GUEST_THREADS;i++){void *value=NULL;pthread_join(threads[i],&value);if(value)errors++;}
    if(nb_import_unbind(&binding)!=NB_OK || nb_scope_close(&scope)!=NB_OK)errors++;
    pthread_barrier_destroy(&barrier);
    printf("boundary_thread_capacity_contract errors=%d simultaneous_gateways=%u\n",errors,atomic_load(&arrived));
    return errors?1:0;
}
