#include "../../src/runtime/nso/nso_loader.h"
#include <stdio.h>
#include <string.h>
static unsigned count,events[4];
static void report(unsigned event) {
    if(count<4)events[count]=event;
    count++;
}
static uint64_t resolve(uint32_t index,const char *name) {
    (void)index;
    return !strcmp(name,"report_init")?(uintptr_t)&report:0;
}
int main(int argc,char **argv) {
    if(argc!=4)return 2;
    char err[256];
    nso_module *m=nso_load_file(argv[1],resolve,err,sizeof(err));
    if(!m) {fprintf(stderr,"%s\n",err);return 1;}
    nso_install_fault_trap(m);
    if(!strcmp(argv[2],"legacy"))nso_run_init(m);
    else nso_run_init_policy(m,!strcmp(argv[2],"wrapper")?NSO_INIT_DT_INIT_OWNS_ARRAY:NSO_INIT_ELF);
    unsigned expected=argv[3][0]-'0';
    int ok=count==expected;
    for(unsigned i=0;i<count && i<4;i++) {
        printf("event[%u]=%u\n",i,events[i]);
        if(events[i]!=(count==1?2:(i?2:1)))ok=0;
    }
    printf("initialization count=%u expected=%u ok=%d\n",count,expected,ok);
    nso_free(m);return ok?0:1;
}
