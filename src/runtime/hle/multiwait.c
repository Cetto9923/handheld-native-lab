#include "multiwait.h"
#include "nso_host.h"
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include <errno.h>
typedef struct wait_set wait_set;
typedef struct holder {void *object,*event;wait_set *owner;struct holder *next,*linked_next;uint64_t notifications;} holder;
struct wait_set {void *object;holder *first,*last;unsigned waiting;struct wait_set *next;uint64_t waits,ready;} ;
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed;
static pthread_once_t once=PTHREAD_ONCE_INIT;
static wait_set *sets;static holder *holders;
static unsigned set_count,holder_count;
static void init_cond(void){pthread_condattr_t a;if(pthread_condattr_init(&a))abort();
#if !defined(__APPLE__)
    if(pthread_condattr_setclock(&a,CLOCK_MONOTONIC))abort();
#endif
    if(pthread_cond_init(&changed,&a))abort();pthread_condattr_destroy(&a);}
static void lock(void){pthread_once(&once,init_cond);pthread_mutex_lock(&mutex);}
static void invalid(const char *why){fprintf(stderr,"[multiwait] invalid lifecycle: %s\n",why);abort();}
static wait_set *find_set(void *p){for(wait_set *s=sets;s;s=s->next)if(s->object==p)return s;return NULL;}
static holder *find_holder(void *p){for(holder *h=holders;h;h=h->next)if(h->object==p)return h;return NULL;}
static void initialize(void *p){lock();if(!p||find_set(p)||set_count>=256)invalid("initialize");
    wait_set *s=calloc(1,sizeof(*s));if(!s)abort();s->object=p;s->next=sets;sets=s;set_count++;pthread_mutex_unlock(&mutex);}
static void initialize_holder(void *p,void *event){lock();if(!p||!event||find_holder(p)||holder_count>=4096)invalid("initialize holder");
    holder *h=calloc(1,sizeof(*h));if(!h)abort();h->object=p;h->event=event;h->next=holders;holders=h;holder_count++;
    /* SDK VA13c050 writes event at+30 and initializes public userdata+38. */
    memcpy((char*)p+0x30,&event,8);uint64_t zero=0;memcpy((char*)p+0x38,&zero,8);pthread_mutex_unlock(&mutex);}
static void link_holder(void *p,void *q){lock();wait_set *s=find_set(p);holder *h=find_holder(q);
    if(!s||!h||h->owner||s->waiting)invalid("link holder");h->owner=s;
    if(s->last)s->last->linked_next=h;else s->first=h;s->last=h;
    fprintf(stderr,"[multiwait] link set=%p holder=%p event=%p\n",p,q,h->event);pthread_mutex_unlock(&mutex);}
static void unlink_holder(void *q){lock();holder *h=find_holder(q);if(!h||!h->owner||h->owner->waiting)invalid("unlink holder");
    wait_set *s=h->owner;holder **at=&s->first;while(*at&&*at!=h)at=&(*at)->linked_next;
    if(!*at)invalid("owner list");*at=h->linked_next;if(s->last==h){s->last=s->first;while(s->last&&s->last->linked_next)s->last=s->last->linked_next;}
    h->owner=NULL;h->linked_next=NULL;pthread_mutex_unlock(&mutex);}
static void finalize_holder(void *q){lock();holder **at=&holders;while(*at&&(*at)->object!=q)at=&(*at)->next;
    if(!*at||(*at)->owner)invalid("finalize linked/unknown holder");holder *h=*at;*at=h->next;free(h);holder_count--;pthread_mutex_unlock(&mutex);}
static void finalize(void *p){lock();wait_set **at=&sets;while(*at&&(*at)->object!=p)at=&(*at)->next;
    if(!*at||(*at)->first||(*at)->waiting)invalid("finalize active/unknown wait set");wait_set *s=*at;*at=s->next;free(s);set_count--;pthread_mutex_unlock(&mutex);}
void ffrs_multiwait_notify(void){lock();pthread_cond_broadcast(&changed);pthread_mutex_unlock(&mutex);}
void ffrs_multiwait_notify_event(void *event){lock();
    for(holder *h=holders;h;h=h->next)if(h->event==event){
        uint64_t n=++h->notifications;
        if(n<=3||(n&(n-1))==0)fprintf(stderr,"[multiwait] signal event=%p holder=%p set=%p count=%llu\n",event,h->object,h->owner?h->owner->object:NULL,(unsigned long long)n);
    }
    pthread_cond_broadcast(&changed);pthread_mutex_unlock(&mutex);}
int ffrs_multiwait_event_referenced(void *event){lock();int found=0;
    for(holder *h=holders;h;h=h->next)if(h->event==event){found=1;break;}
    pthread_mutex_unlock(&mutex);return found;}
int ffrs_multiwait_event_waiting(void *event){lock();int found=0;
    for(holder *h=holders;h;h=h->next)if(h->event==event&&h->owner&&h->owner->waiting){found=1;break;}
    pthread_mutex_unlock(&mutex);return found;}
static void *wait_impl(void *p,int64_t ns,int timed){
    struct timespec until={0};if(timed){if(ns<0)invalid("negative timeout");clock_gettime(CLOCK_MONOTONIC,&until);
        int64_t seconds=ns/1000000000;long rem=(long)(ns%1000000000);if(seconds>INT64_MAX-until.tv_sec-1)until.tv_sec=INT64_MAX;else until.tv_sec+=seconds;
        until.tv_nsec+=rem;if(until.tv_nsec>=1000000000){until.tv_nsec-=1000000000;if(until.tv_sec<INT64_MAX)until.tv_sec++;}}
    lock();wait_set *s=find_set(p);if(!s||!s->first||s->waiting)invalid("wait empty/unknown/already waiting");s->waiting=1;
    if(++s->waits==1)fprintf(stderr,"[multiwait] first wait set=%p timed=%d\n",p,timed);
    void *ready=NULL;int result=0;
    for(;;){for(holder *h=s->first;h;h=h->linked_next)if(nso_host_os_is_event_signaled(h->event)){ready=h->object;break;}
        if(ready||result==ETIMEDOUT)break;
        if(result)invalid("condition wait failed");
#if defined(__APPLE__)
        if(timed){struct timespec now,relative;clock_gettime(CLOCK_MONOTONIC,&now);
            relative.tv_sec=until.tv_sec-now.tv_sec;relative.tv_nsec=until.tv_nsec-now.tv_nsec;
            if(relative.tv_nsec<0){relative.tv_nsec+=1000000000;relative.tv_sec--;}
            result=relative.tv_sec<0?ETIMEDOUT:pthread_cond_timedwait_relative_np(&changed,&mutex,&relative);
        }else result=pthread_cond_wait(&changed,&mutex);
#else
        result=timed?pthread_cond_timedwait(&changed,&mutex,&until):pthread_cond_wait(&changed,&mutex);
#endif
    }
    if(ready){uint64_t n=++s->ready;if(n<=3||(n&(n-1))==0){uint64_t userdata;memcpy(&userdata,(char*)ready+0x38,8);
        fprintf(stderr,"[multiwait] ready set=%p holder=%p userdata=%llu count=%llu\n",p,ready,(unsigned long long)userdata,(unsigned long long)n);}}
    s->waiting=0;pthread_mutex_unlock(&mutex);return ready;
}
static void *wait_any(void *p){return wait_impl(p,0,0);}
static void *timed_wait_any(void *p,int64_t ns){return wait_impl(p,ns,1);}
uint64_t ffrs_multiwait_lookup(const char *name,unsigned *args,unsigned *returns){
    if(!name)return 0;struct entry{const char *name;void(*fn)(void);unsigned args,ret;};
    static const struct entry entries[]={
        {"_ZN2nn2os19InitializeMultiWaitEPNS0_13MultiWaitTypeE",(void(*)(void))initialize,1,0},
        {"_ZN2nn2os25InitializeMultiWaitHolderEPNS0_19MultiWaitHolderTypeEPNS0_9EventTypeE",(void(*)(void))initialize_holder,2,0},
        {"_ZN2nn2os19LinkMultiWaitHolderEPNS0_13MultiWaitTypeEPNS0_19MultiWaitHolderTypeE",(void(*)(void))link_holder,2,0},
        {"_ZN2nn2os21UnlinkMultiWaitHolderEPNS0_19MultiWaitHolderTypeE",(void(*)(void))unlink_holder,1,0},
        {"_ZN2nn2os23FinalizeMultiWaitHolderEPNS0_19MultiWaitHolderTypeE",(void(*)(void))finalize_holder,1,0},
        {"_ZN2nn2os17FinalizeMultiWaitEPNS0_13MultiWaitTypeE",(void(*)(void))finalize,1,0},
        {"_ZN2nn2os7WaitAnyEPNS0_13MultiWaitTypeE",(void(*)(void))wait_any,1,1},
        {"_ZN2nn2os12TimedWaitAnyEPNS0_13MultiWaitTypeENS_8TimeSpanE",(void(*)(void))timed_wait_any,2,1}
    };
    for(size_t i=0;i<sizeof(entries)/sizeof(entries[0]);i++)if(!strcmp(name,entries[i].name)){
        if(args)*args=entries[i].args;if(returns)*returns=entries[i].ret;return (uintptr_t)entries[i].fn;}
    return 0;
}
