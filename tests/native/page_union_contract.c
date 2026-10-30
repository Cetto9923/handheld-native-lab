#include "../../src/runtime/hle/guest_pages.h"
#include <stdio.h>
#include <stdint.h>
static int errors;
#define CHECK(c) do{if(!(c)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);errors++;}}while(0)
int main(void){guest_context *context=0;guest_pages *p=0;struct guest_context_config c={262144,65536};
 CHECK(guest_context_create(&c,&context).status==GUEST_CONTEXT_OK);CHECK(guest_pages_create(context,&p).status==GUEST_PAGES_OK);
 uintptr_t base=0;CHECK(guest_pages_reserve(p,65536,&base).status==GUEST_PAGES_OK);
 for(int i=0;i<3;i++){CHECK(guest_pages_map(p,base+i*4096,4096).status==GUEST_PAGES_OK);*(volatile int*)(base+i*4096)=i+17;}
 struct guest_pages_usage before,after;CHECK(guest_pages_usage(p,&before).status==GUEST_PAGES_OK);
 CHECK(guest_pages_free_allocations(p,base,16384).status!=GUEST_PAGES_OK);
 CHECK(guest_pages_usage(p,&after).status==GUEST_PAGES_OK);CHECK(before.charged_bytes==after.charged_bytes&&after.allocation_count==3);
 for(int i=0;i<3;i++)CHECK(*(volatile int*)(base+i*4096)==i+17);
 CHECK(guest_pages_free_exact(p,base,12288).status!=GUEST_PAGES_OK);
 CHECK(guest_pages_free_allocations(p,base,12288).status==GUEST_PAGES_OK);
 CHECK(guest_pages_usage(p,&after).status==GUEST_PAGES_OK);CHECK(after.charged_bytes==0&&after.backing_bytes==0&&after.allocation_count==0&&after.ledger_valid);
 CHECK(guest_pages_free_allocations(p,base,12288).status!=GUEST_PAGES_OK);
 CHECK(guest_pages_map(p,base,12288).status==GUEST_PAGES_OK);
 *(volatile int*)base=71;*(volatile int*)(base+8192)=73;
 CHECK(guest_pages_free_allocations(p,base+4096,4096).status==GUEST_PAGES_OK);
 CHECK(guest_pages_usage(p,&after).status==GUEST_PAGES_OK);CHECK(after.charged_bytes==8192&&after.backing_bytes==8192&&after.allocation_count==2&&after.ledger_valid);
 CHECK(*(volatile int*)base==71&&*(volatile int*)(base+8192)==73);
 CHECK(guest_pages_free_allocations(p,base,12288).status!=GUEST_PAGES_OK);
 CHECK(guest_pages_map(p,base+4096,4096).status==GUEST_PAGES_OK);CHECK(*(volatile int*)(base+4096)==0);
 CHECK(guest_pages_free_allocations(p,base,4096).status==GUEST_PAGES_OK);
 CHECK(guest_pages_free_allocations(p,base+4096,8192).status==GUEST_PAGES_OK);
 CHECK(guest_pages_release_region(p,base).status==GUEST_PAGES_OK);CHECK(guest_pages_destroy(p).status==GUEST_PAGES_OK);CHECK(guest_context_destroy(context).status==GUEST_CONTEXT_OK);
 printf("page_union_contract errors=%d gap_rejected_before_mutation=1 union_and_middle_cut_refunded=12288\n",errors);return errors!=0;}
