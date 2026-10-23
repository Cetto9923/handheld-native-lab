#include "../../src/runtime/nso/nso_loader.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
static unsigned resolutions;
static unsigned unloads;
static void before_unload(void *user) {
    nso_module *m=user;
    uint64_t value;
    memcpy(&value,(void*)(uintptr_t)nso_addr(m,0x8208),8);
    if(value!=nso_base(m)+0x1234)abort();
    unloads++;
}
static uint64_t sum(uint64_t a,uint64_t b) {return a+b;}
static uint64_t resolver(uint32_t i,const char *name) {
    resolutions++;
    return i==1 && !strcmp(name,"host_sum")?(uintptr_t)&sum:0;
}
static uint64_t word(nso_module *m,uint64_t off) {
    uint64_t v;memcpy(&v,(void*)(uintptr_t)nso_addr(m,off),8);return v;
}
int main(int argc,char **argv) {
    char err[256];
    if(argc<2)return 2;
    stack_t before,after;
    if(sigaltstack(NULL,&before))return 1;
    nso_module *m=nso_load_file(argv[1],resolver,err,sizeof(err));
    if(!m){fprintf(stderr,"%s\n",err);return 1;}
    nso_install_fault_trap(m);
    nso_set_unload_observer(m,before_unload,m);
    if(argc==3 && !strcmp(argv[2],"--unknown")) {
        nso_call(m,0x40,0,0,0,0,0,0);
        return 1;
    }
    uint64_t base=nso_base(m);
    int ok=resolutions==2 && nso_import_count(m)==2 && nso_unresolved_count(m)==1 &&
        nso_reloc_count(m)==3 && nso_relr_count(m)==3 &&
        word(m,0x8208)==base+0x1234 &&
        word(m,0x8210)==base+0x100 && word(m,0x8218)==base+0x200 &&
        word(m,0x8228)==base+0x300 &&
        nso_call(m,0x20,20,22,0,0,0,0)==42;
    printf("contract ok=%d resolver_calls=%u imports=%u unresolved=%u rela=%" PRIu64 " relr=%" PRIu64 "\n",
        ok,resolutions,nso_import_count(m),nso_unresolved_count(m),nso_reloc_count(m),nso_relr_count(m));
    nso_free(m);
    if(unloads!=1)ok=0;
    if(sigaltstack(NULL,&after))return 1;
    printf("signal stack before=(%p,%zu,%d) after=(%p,%zu,%d)\n",
           before.ss_sp,before.ss_size,before.ss_flags,after.ss_sp,after.ss_size,after.ss_flags);
    if((before.ss_flags&SS_DISABLE)!=(after.ss_flags&SS_DISABLE) ||
       (!(before.ss_flags&SS_DISABLE) && (before.ss_sp!=after.ss_sp || before.ss_size!=after.ss_size)))ok=0;
    printf("alternate signal stack restored=%d\n",ok);
    return ok?0:1;
}
