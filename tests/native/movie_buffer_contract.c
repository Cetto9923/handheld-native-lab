#include "../../src/runtime/hle/movie_buffer.h"
#include "../../src/runtime/hle/nso_host.h"
#include "../../src/runtime/hle/guest_process.h"
#include "../../src/runtime/nso/native_boundary.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static unsigned frees;static int token;static void *last;
static void free_movie(void *p,void *ctx){if(ctx!=&token)abort();last=p;frees++;}
movie_free_fn nso_host_movie_get_deallocator(void){return free_movie;}
void *nso_host_movie_get_allocator_userdata(void){return &token;}
nb_scope *guest_process_current_boundary(void){return (nb_scope*)&token;}
nb_status nb_call_from_host(nb_scope *s,void (*fn)(void),nb_registers *p){
 if(s!=(nb_scope*)&token)return NB_INVALID;
 ((void(*)(void*,void*))fn)((void*)(uintptr_t)p->x[0],(void*)(uintptr_t)p->x[1]);return NB_OK;
}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line%d %s\n",__LINE__,#x);errors++;}}while(0)
int main(void){
 int errors=0;struct {uint64_t pre;unsigned char b[64];uint64_t post;} a;
 memset(&a,0xa5,sizeof a);uint64_t canary=a.pre;
 unsigned args=99,ret=99;
 void(*ctor)(void*)=(void*)ffrs_movie_buffer_lookup("_ZN5movie6BufferC1Ev",&args,&ret);
 CHECK(ctor&&args==1&&ret==0);ctor(a.b);
 CHECK(a.pre==canary&&a.post==canary);
 for(int i=0;i<64;i++)CHECK(a.b[i]==(i>=17&&i<24?0xa5:0));
 unsigned char data[16];memset(data,0x47,sizeof data);
 void(*set)(void*,void*,uint64_t)=(void*)ffrs_movie_buffer_lookup("_ZN5movie6Buffer18SetDataAndCapacityEPvm",0,0);
 void*(*base)(void*)=(void*)ffrs_movie_buffer_lookup("_ZNK5movie6Buffer4BaseEv",0,0);
 uint64_t(*cap)(void*)=(void*)ffrs_movie_buffer_lookup("_ZNK5movie6Buffer8CapacityEv",0,0);
 void(*range)(void*,int,int)=(void*)ffrs_movie_buffer_lookup("_ZN5movie6Buffer8SetRangeEii",0,0);
 int(*size)(void*)=(void*)ffrs_movie_buffer_lookup("_ZNK5movie6Buffer4SizeEv",0,0);
 int(*off)(void*)=(void*)ffrs_movie_buffer_lookup("_ZNK5movie6Buffer6OffsetEv",0,0);
 void(*dtor)(void*)=(void*)ffrs_movie_buffer_lookup("_ZN5movie6BufferD1Ev",0,0);
 set(a.b,data,16);range(a.b,3,9);CHECK(base(a.b)==data&&cap(a.b)==16&&off(a.b)==3&&size(a.b)==9);
 dtor(a.b);CHECK(!frees&&base(a.b)==data&&data[0]==0x47);
 /* Owned-object ABI path must use the registered game callback and context. */
 a.b[16]=1;set(a.b,data+4,12);CHECK(frees==1&&last==data&&a.b[16]==0&&base(a.b)==data+4);
 a.b[16]=1;dtor(a.b);CHECK(frees==2&&last==data+4&&!base(a.b));
 unsigned char copied[64];memcpy(copied,a.b,64);CHECK(cap(copied)==12&&off(copied)==3&&size(copied)==9);
 CHECK(a.pre==canary&&a.post==canary&&!ffrs_movie_buffer_lookup("movie::unknown",0,0));
 void(*set_priority)(int)=(void*)ffrs_movie_buffer_lookup("_ZN5movie17SetThreadPriorityEi",0,0);
 int(*priority)(void)=(void*)ffrs_movie_buffer_lookup("_ZN5movie17GetThreadPriorityEv",0,0);
 void(*set_mask)(uint64_t)=(void*)ffrs_movie_buffer_lookup("_ZN5movie11SetCoreMaskEm",0,0);
 set_priority(23);set_mask(7);CHECK(priority()==23&&ffrs_movie_thread_priority()==23&&ffrs_movie_core_mask()==7);
 printf("movie_buffer_contract errors=%d callback_frees=%u\n",errors,frees);return errors!=0;
}
