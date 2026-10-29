#include "../../src/runtime/hle/exit_registry.h"
#include <pthread.h>
#include <stdio.h>
static int errors,trace,scope_live;
static void late(void *arg){if(!scope_live||arg!=(void*)3)errors++;trace=trace*10+3;}
static void first(void *arg){if(!scope_live||arg!=(void*)2)errors++;trace=trace*10+2;if(nso_host_cxa_thread_atexit(late,(void*)3,0))errors++;}
static void last(void *arg){if(!scope_live||arg!=(void*)1)errors++;trace=trace*10+1;}
static void *worker(void *unused){(void)unused;scope_live=1;
 if(nso_host_cxa_thread_atexit(last,(void*)1,0)||nso_host_cxa_thread_atexit(first,(void*)2,0))errors++;
 nso_exit_registry_thread_finalize();
 if(nso_exit_registry_thread_pending()||trace!=231)errors++;
 nso_exit_registry_thread_finalize();scope_live=0;return 0;}
int main(void){pthread_t t;if(pthread_create(&t,0,worker,0)||pthread_join(t,0))return 2;
 printf("thread_exit_contract errors=%d destructor_order=%d\n",errors,trace);return errors!=0;}
