#include "../../src/runtime/hle/mutex.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static _Alignas(8) unsigned char mutex[32], condition[8];
static int value, waiting, errors;
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);errors++; } } while(0)
static void *producer(void *p) {
    (void)p;
    for (;;) {
        nso_host_lock_mutex(mutex);
        if(waiting)break;
        nso_host_unlock_mutex(mutex);usleep(1000);
    }
    value=41;nso_host_signal_condition_variable(condition);nso_host_unlock_mutex(mutex);return NULL;
}
int main(void) {
    alarm(4);memset(mutex,0xaa,sizeof(mutex));memset(condition,0xaa,sizeof(condition));
    nso_host_initialize_mutex(mutex,false,0);nso_host_initialize_condition_variable(condition);
    CHECK(condition[0]==1 && condition[1]==0xaa && condition[2]==0xaa && condition[3]==0xaa);
    nso_host_lock_mutex(mutex);
    CHECK(!nso_host_timed_wait_condition_variable(condition,mutex,1000000));
    CHECK(nso_host_is_mutex_locked_by_current_thread(mutex));
    pthread_t thread;pthread_create(&thread,NULL,producer,NULL);waiting=1;
    while(!value)CHECK(nso_host_timed_wait_condition_variable(condition,mutex,1000000000));
    CHECK(value==41 && nso_host_is_mutex_locked_by_current_thread(mutex));
    nso_host_unlock_mutex(mutex);pthread_join(thread,NULL);
    nso_host_finalize_condition_variable(condition);CHECK(!condition[0]);nso_host_finalize_mutex(mutex);
    printf("os_condition_contract errors=%d value=%d\n",errors,value);return errors?1:0;
}
