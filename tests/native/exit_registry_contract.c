#include "../../src/runtime/hle/exit_registry.h"
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
enum {THREADS=8,REGISTRATIONS=500};
static nso_exit_registry *registry;
static unsigned events[THREADS*REGISTRATIONS+32],used,errors;
static void record(void *value) {events[used++]=(unsigned)(uintptr_t)value;}
static void plain(void) {record((void*)1);}
static void append_new(void *unused) {
    (void)unused;record((void*)8);
    if(nso_exit_registry_cxa_atexit(registry,record,(void*)9,NULL))errors++;
}
static void recursive_finalize(void *dso) {
    record((void*)6);
    nso_exit_registry_finalize(registry,dso);
}
struct item {unsigned thread,index;};
static struct item items[THREADS][REGISTRATIONS];
static unsigned next_expected[THREADS],total_seen;
static void parallel_callback(void *arg) {
    struct item *i=arg;
    if(next_expected[i->thread]!=i->index)errors++;
    if(next_expected[i->thread])next_expected[i->thread]--;
    total_seen++;
}
static void *registrar(void *arg) {
    unsigned t=(unsigned)(uintptr_t)arg;
    for(unsigned i=0;i<REGISTRATIONS;i++) {
        items[t][i]=(struct item){t,i};
        if(nso_exit_registry_cxa_atexit(registry,parallel_callback,&items[t][i],NULL))
            return (void*)1;
    }
    return NULL;
}
static void *drainer(void *unused) {
    (void)unused;nso_exit_registry_finalize(registry,NULL);return NULL;
}
static void normal_exit_report(void) {puts("guest normal-exit callback executed");}
int main(int argc,char **argv) {
    if(argc==2 && (!strcmp(argv[1],"--normal-exit") || !strcmp(argv[1],"--immediate-exit"))) {
        if(nso_host_atexit(normal_exit_report))return 1;
        puts("registration returned before any callback");fflush(stdout);
        if(!strcmp(argv[1],"--normal-exit"))nso_host_exit(23);
        nso_host_immediate_exit(24);
    }
    if(argc!=1)return 2;
    registry=nso_exit_registry_create();
    if(!registry)return 1;
    void *dso_a=(void*)0x100,*dso_b=(void*)0x200;
    errors+=nso_exit_registry_atexit(registry,plain)!=0;
    errors+=nso_exit_registry_cxa_atexit(registry,record,(void*)2,dso_a)!=0;
    errors+=nso_exit_registry_cxa_atexit(registry,record,(void*)3,dso_b)!=0;
    errors+=nso_exit_registry_cxa_atexit(registry,record,(void*)4,dso_a)!=0;
    if(used || nso_exit_registry_pending(registry)!=4)errors++;
    nso_exit_registry_finalize(registry,dso_a);
    if(used!=2 || events[0]!=4 || events[1]!=2 || nso_exit_registry_pending(registry)!=2)errors++;
    nso_exit_registry_finalize(registry,dso_a);
    if(used!=2)errors++;
    pthread_t drains[2];
    for(unsigned i=0;i<2;i++)if(pthread_create(&drains[i],NULL,drainer,NULL))return 1;
    for(unsigned i=0;i<2;i++)if(pthread_join(drains[i],NULL))return 1;
    nso_exit_registry_finalize(registry,NULL);
    if(used!=4 || events[2]!=3 || events[3]!=1 || nso_exit_registry_pending(registry))errors++;
    errors+=nso_exit_registry_cxa_atexit(registry,record,(void*)5,dso_a)!=0;
    errors+=nso_exit_registry_cxa_atexit(registry,recursive_finalize,dso_a,dso_a)!=0;
    nso_exit_registry_finalize(registry,dso_a);
    if(used!=6 || events[4]!=6 || events[5]!=5)errors++;
    errors+=nso_exit_registry_cxa_atexit(registry,record,(void*)7,NULL)!=0;
    errors+=nso_exit_registry_cxa_atexit(registry,append_new,NULL,NULL)!=0;
    nso_exit_registry_finalize(registry,NULL);
    if(used!=9 || events[6]!=8 || events[7]!=9 || events[8]!=7)errors++;
    pthread_t threads[THREADS];
    for(unsigned t=0;t<THREADS;t++) {
        next_expected[t]=REGISTRATIONS-1;
        if(pthread_create(&threads[t],NULL,registrar,(void*)(uintptr_t)t))return 1;
    }
    for(unsigned t=0;t<THREADS;t++) {
        void *r;if(pthread_join(threads[t],&r) || r)return 1;
    }
    if(total_seen || nso_exit_registry_pending(registry)!=THREADS*REGISTRATIONS)errors++;
    nso_exit_registry_finalize(registry,NULL);nso_exit_registry_finalize(registry,NULL);
    if(total_seen!=THREADS*REGISTRATIONS || nso_exit_registry_pending(registry))errors++;
    for(unsigned t=0;t<THREADS;t++)if(next_expected[t])errors++;
    errors+=nso_exit_registry_atexit(registry,plain)!=0;
    nso_exit_registry_destroy(registry); /* explicit discard; no callback here */
    if(used!=9)errors++;
    if(nso_host_exit_scope_begin((void*)0x1000))errors++;
    if(nso_host_exit_scope_begin((void*)0x2000)!=-1)errors++;
    if(nso_host_atexit(plain))errors++;
    nso_host_exit_scope_end((void*)0x1000);
    if(used!=9)errors++;
    if(nso_host_exit_scope_begin((void*)0x2000))errors++;
    nso_host_exit_scope_end((void*)0x2000);
    printf("{\"parallel_registrations\":%u,\"concurrent_drainers\":2,\"executed_once\":%u,\"lifo\":true,\"dso_filter\":true,"
           "\"recursive_finalize\":true,\"new_registration_during_finalize\":true,\"discard_does_not_execute\":true,\"errors\":%u}\n",
           THREADS*REGISTRATIONS,total_seen,errors);
    return errors?1:0;
}
