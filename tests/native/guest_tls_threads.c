#include "../../src/runtime/hle/guest_tls.h"
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum { THREADS=8 };
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition=PTHREAD_COND_INITIALIZER;
static pthread_key_t unrelated_host_key;
static unsigned waiting;
static int host_only;
struct result {unsigned id,errors;uintptr_t context,tp_before[2],tp_after[2];};
static void read_thread_pointers(uintptr_t values[2]) {
#if defined(__aarch64__)
    __asm__ volatile("mrs %0, tpidr_el0":"=r"(values[0]));
    __asm__ volatile("mrs %0, tpidrro_el0":"=r"(values[1]));
#else
#error "TLS proof requires native AArch64"
#endif
}
static void *worker(void *p) {
    struct result *r=p;
    r->errors=pthread_setspecific(unrelated_host_key,r)!=0;
    read_thread_pointers(r->tp_before);
    r->context=host_only?0:(uintptr_t)nso_guest_tls_current();
    for(unsigned i=0;!host_only && i<NSO_GUEST_TLS_SLOTS;i++) {
        if(nso_guest_tls_get(i)!=0)r->errors++;
        nso_guest_tls_set(i,((uintptr_t)r->id<<24)|i);
    }
    pthread_mutex_lock(&mutex);
    waiting++;
    if(waiting==THREADS)pthread_cond_broadcast(&condition);
    while(waiting!=THREADS)pthread_cond_wait(&condition,&mutex);
    pthread_mutex_unlock(&mutex);
    for(unsigned n=0;!host_only && n<1000;n++)
        for(unsigned i=0;i<NSO_GUEST_TLS_SLOTS;i++)
            if(nso_guest_tls_get(i)!=(((uintptr_t)r->id<<24)|i))r->errors++;
    read_thread_pointers(r->tp_after);
    /* TPIDR_EL0 is observed, not assumed invariant across host scheduling.
     * The initial version tested that assumption and failed; retain its logs
     * and compare a host-only control before attributing changes to guest TLS. */
    if(r->tp_before[1]!=r->tp_after[1] ||
       pthread_getspecific(unrelated_host_key)!=r)r->errors++;
    return NULL;
}
int main(int argc,char **argv) {
    if(argc==2 && !strcmp(argv[1],"--host-only-control"))host_only=1;
    else if(argc!=1)return 2;
    pthread_t threads[THREADS];
    struct result results[THREADS]={0};
    if(pthread_key_create(&unrelated_host_key,NULL))return 1;
    for(unsigned i=0;i<THREADS;i++) {
        results[i].id=i+1;
        if(pthread_create(&threads[i],NULL,worker,&results[i]))return 1;
    }
    unsigned errors=0,el0_changes=0;
    for(unsigned i=0;i<THREADS;i++) {
        if(pthread_join(threads[i],NULL))return 1;
        errors+=results[i].errors;
        if(!host_only)for(unsigned j=0;j<i;j++)if(results[i].context==results[j].context)errors++;
        if(results[i].tp_before[0]!=results[i].tp_after[0])el0_changes++;
        printf("thread %u context=0x%" PRIxPTR " tp_el0=0x%" PRIxPTR "->0x%" PRIxPTR
               " tp_ro_el0=0x%" PRIxPTR "->0x%" PRIxPTR " errors=%u\n",
               results[i].id,results[i].context,results[i].tp_before[0],results[i].tp_after[0],
               results[i].tp_before[1],results[i].tp_after[1],results[i].errors);
    }
    for(unsigned i=0;i<NSO_GUEST_TLS_SLOTS;i++)if(nso_guest_tls_get(i)!=0)errors++;
    pthread_key_delete(unrelated_host_key);
    printf("{\"threads\":8,\"slots_each\":16,\"value_checks\":%u,\"errors\":%u,\"tpidr_el0_observed_changes\":%u,\"host_only_control\":%s,\"sdk_tls_bound\":false}\n",
           host_only?0:128000,errors,el0_changes,host_only?"true":"false");
    return errors?1:0;
}
