#include "../../src/runtime/hle/nso_host.h"
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
static _Alignas(64) unsigned char owned[128];
static int calls,fail;
int guest_process_interposed_aligned_alloc(size_t alignment,size_t bytes,void **out){
 calls++;if(alignment!=64||bytes!=40)return -1;*out=fail?0:owned;return 1;}
int main(void){int errors=0;void *p=(void*)0x1234;memset(owned,0x6b,sizeof owned);errno=EINTR;
 if(nso_host_posix_memalign(&p,64,40)||p!=owned||errno!=EINTR||owned[0]!=0x6b)errors++;
 p=(void*)0x1234;if(nso_host_posix_memalign(&p,3,40)!=EINVAL||p!=(void*)0x1234||calls!=1)errors++;
 fail=1;p=(void*)0x1234;if(nso_host_posix_memalign(&p,64,40)!=ENOMEM||p!=(void*)0x1234||errno!=EINTR)errors++;
 fail=0;if(nso_host_memalign(64,40)!=owned||calls!=3)errors++;
 printf("aligned_allocator_contract errors=%d calls=%d\n",errors,calls);return errors!=0;}
