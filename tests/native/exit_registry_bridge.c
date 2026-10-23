/* Exercise the actual CLI resolver/order and unload observer with valid,
 * synthetic native code. No game bytes or generated game code are included. */
#define main nso_cli_main
#include "../../src/runtime/nso/nso_call.c"
#undef main
static unsigned callbacks;
static void report_callback(void) {callbacks++;puts("native guest callback executed");}
static uint64_t extension(uint32_t index,const char *name) {
    (void)index;
    return !strcmp(name,"test_report_callback")?(uintptr_t)&report_callback:0;
}
int main(int argc,char **argv) {
    if(argc!=3)return 2;
    if(resolve_import(70,"exit")!=(uintptr_t)&nso_host_exit) {
        fprintf(stderr,"CLI exit resolution bypassed the typed guest exit adapter\n");return 1;
    }
    nso_host_set_extension_resolver(extension);
    const char *offset=!strcmp(argv[2],"normal")?"0x20":!strcmp(argv[2],"immediate")?"0x60":"0xd0";
    char *args[]={argv[0],argv[1],"--call",(char*)offset};
    int result=nso_cli_main(4,args);
    /* A returning probe call unloads its module; callbacks must be discarded. */
    if(callbacks)return 1;
    puts("module probe unloaded with zero callbacks executed");
    return result;
}
