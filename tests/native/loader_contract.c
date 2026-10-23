#include "../../src/runtime/nso/nso_loader.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned resolutions;
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
    nso_module *m=nso_load_file(argv[1],resolver,err,sizeof(err));
    if(!m){fprintf(stderr,"%s\n",err);return 1;}
    nso_install_fault_trap(m);
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
    nso_free(m);return ok?0:1;
}
