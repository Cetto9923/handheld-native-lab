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
#ifdef NSO_WITH_GUEST_PROFILE
#if !defined(NSO_WITH_HOST_HLE)
#error "the owned startup profile requires the typed HLE runtime"
#endif
#include "../hle/guest_process.h"
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
#if !defined(__ANDROID__)
        /* Android uses only the explicitly linked typed HLE guard runtime. */
        "__cxa_guard_acquire", "__cxa_guard_release", "__cxa_guard_abort",
#endif
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
    fprintf(stderr,"usage: %s <uncompressed.nso> [--init-wrapper|--init-elf] --info|--call <offset> [arg0 ... arg5]|--symbol <name> [args]\n",program);
#ifdef NSO_WITH_GUEST_PROFILE
    fputs("owned experiment: after path, supply --guest-quota <bytes> --guest-stack <bytes> [--guest-resource-quota <bytes>] [--guest-arg <literal> ...]; resource provider has no default; complete guest argv including argv0, default explicitly empty; no original SVC measurement\n",stderr);
#endif
    return 2;
}
#ifdef NSO_WITH_GUEST_PROFILE
struct profile_entry {int info,init,by_symbol,rc;unsigned argument_count;nso_init_policy policy;const char *target;uint64_t off,args[6];};
static void *run_profile_entry(guest_process *p,void *opaque) {
    struct profile_entry *entry=opaque;
    nso_module *m=guest_process_module(p);
    if(entry->init) {
        struct nso_result r=guest_process_initialize(p,entry->policy);
        if(r.status!=NSO_OK) {fprintf(stderr,"[profile-executor] initializer status=%d errno=%d; no raw fallback\n",r.status,r.native_error);entry->rc=1;return NULL;}
    }
    if(!entry->info) {
        uint64_t off=entry->off;
        if(entry->by_symbol) {
            uint32_t index=nso_find_symbol(m,entry->target);
            if(!index || (off=nso_symbol_value(m,index))==UINT64_MAX) {
                fprintf(stderr,"defined symbol not found: %s\n",entry->target);entry->rc=1;return NULL;
            }
        }
        unsigned expected=UINT32_MAX;
        if(entry->by_symbol) {
            if(!strcmp(entry->target,"nnMain") || !strcmp(entry->target,"nninitStartup"))expected=0;
            else if(!strcmp(entry->target,"malloc") || !strcmp(entry->target,"free"))expected=1;
            else if(!strcmp(entry->target,"calloc") || !strcmp(entry->target,"realloc") || !strcmp(entry->target,"aligned_alloc"))expected=2;
        }
        if(expected!=entry->argument_count) {fputs("[profile-executor] entry signature unsupported; named reviewed scalar ABI required\n",stderr);entry->rc=1;return NULL;}
        printf("[call] original offset=0x%" PRIx64 " native owned primary boundary\n",off);fflush(stdout);
        uint64_t value=0;
        struct nso_result r=guest_process_call(p,off,entry->args,expected,&value);
        if(r.status!=NSO_OK) {fprintf(stderr,"[profile-executor] call status=%d errno=%d; no raw fallback\n",r.status,r.native_error);entry->rc=1;return NULL;}
        /* Preserve return evidence before primary/TLS teardown can failstop. */
        printf("result        : 0x%" PRIx64 "\n",value);fflush(stdout);
    }
    return NULL;
}
static int owned_profile(const char *path,size_t quota,size_t stack,size_t resources,size_t guest_argc,const char *const guest_argv[],struct profile_entry *entry) {
    struct guest_process_config config={quota,stack};
    guest_process *p=NULL;
    struct nso_result r=resources?guest_process_create_with_resources(&config,resources,guest_argc,guest_argv,&p):guest_process_create_with_arguments(&config,guest_argc,guest_argv,&p);
    char err[256]={0};
    if(r.status==NSO_OK)r=guest_process_load(p,path,err,sizeof(err));
    if(r.status==NSO_OK) {
        nso_module *m=guest_process_module(p);
        char bid[33];nso_build_id(m,bid);
        printf("[profile] explicit diagnostic port-policy quota=%zu stack=%zu image=%zu resource_quota=%zu guest_argc=%zu build=%s imports=%u unresolved=%u; CRT/TLS not initialized\n",
            quota,stack,nso_image_size(m),resources,guest_argc,bid,nso_import_count(m),nso_unresolved_count(m));fflush(stdout);
        void *returned=NULL;r=guest_process_run(p,run_profile_entry,entry,&returned);
    }
    if(r.status!=NSO_OK)fprintf(stderr,"[profile-error] status=%d native=%d cleanup=%d %s\n",r.status,r.native_error,r.cleanup_error,err);
    struct nso_result cleanup=guest_process_destroy(&p);
    if(cleanup.status!=NSO_OK) {
        fprintf(stderr,"[profile-retained] owner=%p status=%d native=%d cleanup=%d; retained until isolated process ends\n",
            (void *)p,cleanup.status,cleanup.native_error,cleanup.cleanup_error);
        return 1;
    }
    return r.status==NSO_OK?entry->rc:1;
}
#endif
int main(int argc,char **argv) {
    if(argc<3)return usage(argv[0]);
    int next=2,init=0;
    nso_init_policy policy=NSO_INIT_ELF;
#ifdef NSO_WITH_GUEST_PROFILE
    uint64_t quota=0,stack=0,resources=0;
    size_t guest_argc=0;const char **guest_argv=calloc((size_t)argc,sizeof(*guest_argv));
    if(!guest_argv) {fputs("guest argument metadata allocation failed\n",stderr);return 1;}
    int owned=0;
    while(next<argc && (!strcmp(argv[next],"--guest-quota") || !strcmp(argv[next],"--guest-stack") || !strcmp(argv[next],"--guest-arg") || !strcmp(argv[next],"--guest-resource-quota"))) {
        const char *option=argv[next++];
        if(next>=argc)goto invalid_cli;
        owned=1;
        if(!strcmp(option,"--guest-arg")) {guest_argv[guest_argc++]=argv[next++];continue;}
        uint64_t value;
        if(!number(argv[next++],&value) || !value || value>SIZE_MAX)goto invalid_cli;
        if(!strcmp(option,"--guest-quota")) {if(quota)goto invalid_cli;quota=value;}
        else if(!strcmp(option,"--guest-stack")) {if(stack)goto invalid_cli;stack=value;}
        else {if(resources)goto invalid_cli;resources=value;}
    }
    if(owned && (!quota || !stack))goto invalid_cli;
#endif
    if(next>=argc)goto invalid_cli;
    if(!strcmp(argv[next],"--init-wrapper")) {init=1;policy=NSO_INIT_DT_INIT_OWNS_ARRAY;next++;}
    else if(!strcmp(argv[next],"--init-elf")) {init=1;next++;}
    if(next>=argc)goto invalid_cli;
    const char *mode=argv[next++];
    int info=!strcmp(mode,"--info"),by_symbol=!strcmp(mode,"--symbol"),by_offset=!strcmp(mode,"--call");
    if(!info && !by_symbol && !by_offset)goto invalid_cli;
    const char *target=NULL;
    uint64_t off=0,args[6]={0};unsigned argument_count=0;
    if(info) {if(next!=argc)goto invalid_cli;}
    else {
        if(next>=argc)goto invalid_cli;
        target=argv[next++];
        if(by_offset && !number(target,&off))goto invalid_cli;
        if(argc-next>6)goto invalid_cli;
        for(int i=0;next<argc;i++,next++) {if(!number(argv[next],&args[i]))goto invalid_cli;argument_count++;}
    }
#ifdef NSO_WITH_GUEST_PROFILE
    if(owned) {
        struct profile_entry entry={.info=info,.init=init,.by_symbol=by_symbol,.argument_count=argument_count,.policy=policy,.target=target,.off=off};
        memcpy(entry.args,args,sizeof(args));
        int result=owned_profile(argv[1],(size_t)quota,(size_t)stack,(size_t)resources,guest_argc,guest_argv,&entry);
        free(guest_argv);return result;
    }
#endif
#ifdef NSO_WITH_GUEST_PROFILE
    free(guest_argv);
#endif
    (void)argument_count;
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
    if(init)nso_run_init_policy(m,policy);
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
invalid_cli:
#ifdef NSO_WITH_GUEST_PROFILE
    free(guest_argv);
#endif
    return usage(argv[0]);
}
