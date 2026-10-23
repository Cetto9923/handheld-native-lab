#include "nso_loader.h"
#include <dlfcn.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef NSO_WITH_HOST_HLE
#include "../hle/nso_host.h"
#include "../hle/exit_registry.h"
#endif

/* Keep host binding deliberately small. Matching a name alone does not establish
 * C++ layout, Switch OS semantics, or macOS variadic ABI compatibility. */
static uint64_t resolve_import(uint32_t index, const char *name) {
    (void)index;
#ifdef NSO_WITH_HOST_HLE
    /* Typed process-exit handling must precede the host dlsym allowlist. */
    uint64_t adapted=nso_host_resolve(index,name);
    if(adapted)return adapted;
#endif
    static const char *const compatible[] = {
        "__cxa_guard_acquire", "__cxa_guard_release", "__cxa_guard_abort",
        "memset", "memcpy", "memmove", "memcmp", "memchr",
        "strlen", "strnlen", "strcmp", "strncmp", "strcpy", "strncpy",
        "strchr", "strrchr", "strstr", "exit", "abort",
        "nvnBootstrapLoader",
        NULL
    };
    for(size_t i=0;compatible[i];i++) if(!strcmp(name,compatible[i]))
        return (uintptr_t)dlsym(RTLD_DEFAULT,name);
    return 0;
}
static int number(const char *s,uint64_t *out) {
    char *end;errno=0;
    if(!s || !*s || *s=='-')return 0;
    unsigned long long v=strtoull(s,&end,0);
    if(errno || *end)return 0;
    *out=v;return 1;
}
static int usage(const char *program) {
    fprintf(stderr,"usage: %s <uncompressed.nso> [--init] --info|--call <offset> [arg0 ... arg5]|--symbol <name> [args]\n",program);
    return 2;
}
int main(int argc,char **argv) {
    if(argc<3)return usage(argv[0]);
    int next=2,init=0;
    if(!strcmp(argv[next],"--init")) {init=1;next++;}
    if(next>=argc)return usage(argv[0]);
    const char *mode=argv[next++];
    int info=!strcmp(mode,"--info"),by_symbol=!strcmp(mode,"--symbol"),by_offset=!strcmp(mode,"--call");
    if(!info && !by_symbol && !by_offset)return usage(argv[0]);
    const char *target=NULL;
    uint64_t off=0,args[6]={0};
    if(info) {if(next!=argc)return usage(argv[0]);}
    else {
        if(next>=argc)return usage(argv[0]);
        target=argv[next++];
        if(by_offset && !number(target,&off))return usage(argv[0]);
        if(argc-next>6)return usage(argv[0]);
        for(int i=0;next<argc;i++,next++)if(!number(argv[next],&args[i]))return usage(argv[0]);
    }
    char err[256]={0};
    nso_module *m=nso_load_file(argv[1],resolve_import,err,sizeof(err));
    if(!m){fprintf(stderr,"load failed: %s\n",err);return 1;}
#ifdef NSO_WITH_HOST_HLE
    if(nso_host_exit_scope_begin(m)) {
        fprintf(stderr,"only one quiescent guest module exit scope is supported\n");
        nso_free(m);return 1;
    }
    nso_set_unload_observer(m,nso_host_exit_scope_end,m);
#endif
    nso_install_fault_trap(m);
    char bid[33];nso_build_id(m,bid);
    printf("build id      : %s\nbase          : 0x%" PRIx64 "\nimports       : %u\nunresolved    : %u\nrelocations   : %" PRIu64 "\nrelr          : %" PRIu64 "\n",
           bid,nso_base(m),nso_import_count(m),nso_unresolved_count(m),nso_reloc_count(m),nso_relr_count(m));
    fflush(stdout);
    if(init)nso_run_init(m);
    int rc=0;
    if(!info) {
        if(by_symbol) {
            uint32_t index=nso_find_symbol(m,target);
            if(!index || (off=nso_symbol_value(m,index))==UINT64_MAX) {
                fprintf(stderr,"defined symbol not found: %s\n",target);nso_free(m);return 1;
            }
        }
        printf("[call] offset=0x%" PRIx64 "\n",off);fflush(stdout);
        uint64_t result=nso_call(m,off,args[0],args[1],args[2],args[3],args[4],args[5]);
        printf("result        : 0x%" PRIx64 "\n",result);
    }
    nso_free(m);return rc;
}
