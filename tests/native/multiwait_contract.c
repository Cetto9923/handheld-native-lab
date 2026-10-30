#include "../../src/runtime/hle/multiwait.h"
#include "../../src/runtime/hle/nso_host.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line%d %s\n",__LINE__,#x);errors++;}}while(0)
#define GET(type,name) (type)(uintptr_t)ffrs_multiwait_lookup(name,NULL,NULL)
static void *signal_later(void *event){struct timespec delay={0,20000000};nanosleep(&delay,NULL);nso_host_os_signal_event(event);return NULL;}
static int64_t now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (int64_t)t.tv_sec*1000000000+t.tv_nsec;}
static void *wait_without_signal(void *set){
 void*(*timed)(void*,int64_t)=GET(void*(*)(void*,int64_t),"_ZN2nn2os12TimedWaitAnyEPNS0_13MultiWaitTypeENS_8TimeSpanE");
 return timed(set,1000000000);
}
int main(int argc,char **argv){int errors=0;
 _Alignas(8) unsigned char set[64]={0},event1[40]={0},event2[40]={0};
 struct {uint64_t pre;unsigned char bytes[64];uint64_t post;} h1,h2;
 memset(&h1,0xa5,sizeof h1);memset(&h2,0xa5,sizeof h2);uint64_t guard=h1.pre;
 void(*init)(void*)=GET(void(*)(void*),"_ZN2nn2os19InitializeMultiWaitEPNS0_13MultiWaitTypeE");
 void(*ih)(void*,void*)=GET(void(*)(void*,void*),"_ZN2nn2os25InitializeMultiWaitHolderEPNS0_19MultiWaitHolderTypeEPNS0_9EventTypeE");
 void(*link)(void*,void*)=GET(void(*)(void*,void*),"_ZN2nn2os19LinkMultiWaitHolderEPNS0_13MultiWaitTypeEPNS0_19MultiWaitHolderTypeE");
 void(*unlink)(void*)=GET(void(*)(void*),"_ZN2nn2os21UnlinkMultiWaitHolderEPNS0_19MultiWaitHolderTypeE");
 void(*fh)(void*)=GET(void(*)(void*),"_ZN2nn2os23FinalizeMultiWaitHolderEPNS0_19MultiWaitHolderTypeE");
 void(*fini)(void*)=GET(void(*)(void*),"_ZN2nn2os17FinalizeMultiWaitEPNS0_13MultiWaitTypeE");
 void*(*wait)(void*)=GET(void*(*)(void*),"_ZN2nn2os7WaitAnyEPNS0_13MultiWaitTypeE");
 void*(*timed)(void*,int64_t)=GET(void*(*)(void*,int64_t),"_ZN2nn2os12TimedWaitAnyEPNS0_13MultiWaitTypeENS_8TimeSpanE");
 CHECK(init&&ih&&link&&unlink&&fh&&fini&&wait&&timed);if(errors)return 1;
 nso_host_os_initialize_event(event1,false,0);nso_host_os_initialize_event(event2,false,1);
 init(set);ih(h1.bytes,event1);ih(h2.bytes,event2);link(set,h1.bytes);link(set,h2.bytes);
 if(argc==2&&!strcmp(argv[1],"--active-finalize")){
  pthread_t thread;if(pthread_create(&thread,NULL,wait_without_signal,set))return 2;
  int64_t deadline=now()+500000000;
  while(!ffrs_multiwait_event_waiting(event1)&&now()<deadline){struct timespec delay={0,1000000};nanosleep(&delay,NULL);}
  if(!ffrs_multiwait_event_waiting(event1)){pthread_join(thread,NULL);return 2;}
  /* Separate-process negative case: this must abort before destroying the
   * predicate storage still used by the other thread. */
  nso_host_os_finalize_event(event1);return 3;
 }
 CHECK(h1.pre==guard&&h1.post==guard&&h2.pre==guard&&h2.post==guard);
 void *saved=NULL;memcpy(&saved,h1.bytes+0x30,8);CHECK(saved==event1);
 CHECK(!timed(set,0));int64_t start=now();CHECK(!timed(set,20000000));CHECK(now()-start>=15000000);
 pthread_t thread;CHECK(pthread_create(&thread,NULL,signal_later,event2)==0);CHECK(wait(set)==h2.bytes);pthread_join(thread,NULL);
 /* MultiWait is a non-consuming predicate even for an AutoClear Event. */
 CHECK(nso_host_os_is_event_signaled(event2)&&timed(set,0)==h2.bytes);nso_host_os_clear_event(event2);CHECK(!timed(set,0));
 nso_host_os_signal_event(event1);CHECK(timed(set,0)==h1.bytes);CHECK(nso_host_os_is_event_signaled(event1));nso_host_os_clear_event(event1);
 /* Actual movie teardown joins its watch thread, finalizes the Events, and
  * only then unlinks/finalizes the holders. Registration is not an active
  * Event wait; the quiescent holder cleanup must not dereference it. */
 CHECK(ffrs_multiwait_event_referenced(event1)&&!ffrs_multiwait_event_waiting(event1));
 nso_host_os_finalize_event(event1);nso_host_os_finalize_event(event2);
 CHECK(event1[0x13]==0&&event2[0x13]==0);
 unlink(h2.bytes);unlink(h1.bytes);CHECK(ffrs_multiwait_event_referenced(event1));fh(h1.bytes);fh(h2.bytes);fini(set);
 CHECK(!ffrs_multiwait_event_referenced(event1));
 printf("multiwait_contract errors=%d actual_events=2 cross_thread_wake=1 non_consuming=1 timeout=1\n",errors);return errors!=0;
}
