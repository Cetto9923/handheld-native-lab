#include "nso_boot.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#ifdef NSO_WITH_GUEST_PROFILE
#include "../hle/guest_process.h"

/* Explicit isolated allocator contract, not the normal SDK/CRT boot order. */
static int exercise_original_allocator(guest_process *p) {
    void *blocks[2]={NULL,NULL};
    const size_t sizes[2]={48,64};
    int failed=0;
    for(unsigned i=0;i<2;i++) {
        struct nso_result r=guest_process_allocate_thread_local(p,sizes[i],&blocks[i]);
        if(r.status!=NSO_OK) {
            fprintf(stderr,"[original-allocator] allocate bytes=%zu status=%d native=%d; no fallback\n",sizes[i],r.status,r.native_error);
            failed=1;break;
        }
    }
    if(blocks[0] && blocks[1])
        printf("[original-allocator] actual callbacks returned two distinct owned records, sizes=48,64; no payload writes\n");
    for(unsigned i=2;i>0;i--)if(blocks[i-1]) {
        struct nso_result r=guest_process_free_thread_local(p,blocks[i-1],sizes[i-1]);
        if(r.status!=NSO_OK) {
            fprintf(stderr,"[original-allocator] free bytes=%zu status=%d native=%d; owner retained, no retry\n",sizes[i-1],r.status,r.native_error);
            failed=1;break;
        }
    }
    if(!failed)puts("[original-allocator] two original allocate/free pairs completed; CRT/ELF TLS still uninitialized");
    fflush(stdout);return failed;
}
#if defined(__aarch64__)
static uint64_t *s_orig_ctors = NULL;
static uint32_t s_ctor_total = 0;
static uint32_t s_ctors_started = 0;
static uint32_t s_ctors_finished = 0;
static uint64_t s_module_base = 0;

static void ctor_instrument_hook(uint32_t idx) {
    if(idx >= s_ctor_total || !s_orig_ctors) return;
    s_ctors_started++;
    uint64_t ctor_addr = s_orig_ctors[idx];
    uint64_t ctor_off = ctor_addr >= s_module_base ? ctor_addr - s_module_base : ctor_addr;
    printf("[ctor-start] idx=%u/%u off=0x%" PRIx64 "\n", idx, s_ctor_total, ctor_off);
    fflush(stdout);
    typedef void (*ctor_fn)(void);
    ctor_fn fn = (ctor_fn)(uintptr_t)ctor_addr;
    fn();
    s_ctors_finished++;
    printf("[ctor-done] idx=%u/%u (total finished=%u)\n", idx, s_ctor_total, s_ctors_finished);
    fflush(stdout);
}
#endif

static void *run_profile_entry(guest_process *p,void *opaque) {
    struct nso_boot_entry *entry=opaque;
    nso_module *m=guest_process_module(p);
    if(entry->boot) {
        printf("[boot-step1] executing DT_INIT phase 1 (DSO registration)...\n");
        fflush(stdout);
        struct nso_result r=guest_process_initialize(p,NSO_INIT_DT_INIT_OWNS_ARRAY);
        if(r.status!=NSO_OK) {
            fprintf(stderr,"[boot-step1] DT_INIT failed status=%d native=%d\n",r.status,r.native_error);
            entry->rc=1;return NULL;
        }
        uint32_t startup_sym=nso_find_symbol(m,"nninitStartup");
        if(!startup_sym) {fputs("[boot-step2] nninitStartup symbol not found\n",stderr);entry->rc=1;return NULL;}
        uint64_t startup_off=nso_symbol_value(m,startup_sym);
        printf("[boot-step2] calling nninitStartup (offset 0x%" PRIx64 ")...\n",startup_off);
        fflush(stdout);
        uint64_t startup_res=0;
        const uint64_t zero_args[6]={0};
        r=guest_process_call(p,startup_off,zero_args,0,&startup_res);
        if(r.status!=NSO_OK) {
            fprintf(stderr,"[boot-step2] nninitStartup failed status=%d native=%d\n",r.status,r.native_error);
            entry->rc=1;return NULL;
        }
        printf("[boot-step2] nninitStartup returned 0x%" PRIx64 "\n",startup_res);
        fflush(stdout);
        uint64_t init_off=nso_init_offset(m);
        if(!init_off) {fputs("[boot-step3] DT_INIT offset not found\n",stderr);entry->rc=1;return NULL;}
#if defined(__aarch64__)
        uint64_t arr_off=nso_init_array_offset(m);
        uint64_t arr_sz=nso_init_array_size(m);
        if(arr_off && arr_sz && (arr_sz % 8 == 0)) {
            uint32_t count=(uint32_t)(arr_sz / 8);
            uint64_t *arr=(uint64_t *)(uintptr_t)(nso_base(m) + arr_off);
            s_ctor_total=count;
            s_module_base=nso_base(m);
            s_orig_ctors=malloc(arr_sz);
            if(s_orig_ctors) {
                memcpy(s_orig_ctors, arr, arr_sz);
                size_t tramp_bytes=((count * 24 + 4095) / 4096) * 4096;
                void *tramps=mmap(NULL, tramp_bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
                if(tramps!=MAP_FAILED) {
                    for(uint32_t i=0;i<count;i++) {
                        char *item=(char *)tramps + i * 24;
                        uint32_t *code=(uint32_t *)item;
                        code[0]=0x52800000 | (i << 5); /* movz w0, #i */
                        code[1]=0xd503201f;            /* nop */
                        code[2]=0x58000050;            /* ldr x16, [pc, #8] */
                        code[3]=0xd61f0200;            /* br x16 */
                        *(uint64_t *)(item + 16)=(uintptr_t)&ctor_instrument_hook;
                        arr[i]=(uint64_t)(uintptr_t)item;
                    }
                    mprotect(tramps, tramp_bytes, PROT_READ | PROT_EXEC);
                    __builtin___clear_cache(tramps, (char *)tramps + tramp_bytes);
                    printf("[boot-step3] instrumented %u constructors for execution accounting\n", count);
                    fflush(stdout);
                }
            }
        }
#endif
        printf("[boot-step3] calling DT_INIT Phase 2 (constructors & ProtectRelro) at offset 0x%" PRIx64 "...\n",init_off);
        fflush(stdout);
        uint64_t ctor_res=0;
        r=guest_process_call(p,init_off,zero_args,0,&ctor_res);
        if(r.status!=NSO_OK) {
            fprintf(stderr,"[boot-step3] constructors failed status=%d native=%d\n",r.status,r.native_error);
            entry->rc=1;return NULL;
        }
        printf("[boot-step3] constructors completed successfully! total finished=%u return=0x%" PRIx64 "\n",s_ctors_finished,ctor_res);
        fflush(stdout);
        uint32_t main_sym=nso_find_symbol(m,"nnMain");
        if(!main_sym) {fputs("[boot-step4] nnMain symbol not found\n",stderr);entry->rc=1;return NULL;}
        uint64_t main_off=nso_symbol_value(m,main_sym);
        printf("[boot-step4] calling nnMain at offset 0x%" PRIx64 "...\n",main_off);
        fflush(stdout);
        uint64_t main_res=0;
        r=guest_process_call(p,main_off,zero_args,0,&main_res);
        if(r.status!=NSO_OK) {
            fprintf(stderr,"[boot-step4] nnMain failed status=%d native=%d\n",r.status,r.native_error);
            entry->rc=1;return NULL;
        }
        printf("[boot-step4] nnMain returned 0x%" PRIx64 "\n",main_res);
        fflush(stdout);
        entry->rc=0;
        return NULL;
    }
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
        if(entry->allocator_diagnostic)entry->rc=exercise_original_allocator(p);
    }
    return NULL;
}
int nso_owned_profile(const char *path,size_t quota,size_t stack,size_t resources,size_t guest_argc,const char *const guest_argv[],struct nso_boot_entry *entry) {
    struct guest_process_config config={quota,stack};
    guest_process *p=NULL;
    struct nso_result r=resources?guest_process_create_with_resources(&config,resources,guest_argc,guest_argv,&p):guest_process_create_with_arguments(&config,guest_argc,guest_argv,&p);
    char err[256]={0};
    if(r.status==NSO_OK)r=guest_process_load(p,path,err,sizeof(err));
    if(r.status==NSO_OK) {
        nso_module *m=guest_process_module(p);
        char bid[33];nso_build_id(m,bid);
        printf("[profile] explicit diagnostic port-policy quota=%zu stack=%zu image=%zu resource_quota=%zu guest_argc=%zu build=%s imports=%u unresolved=%u module_base=0x%" PRIx64 "; CRT/TLS not initialized\n",
            quota,stack,nso_image_size(m),resources,guest_argc,bid,nso_import_count(m),nso_unresolved_count(m),nso_base(m));fflush(stdout);
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
int nso_boot_run(const nso_boot_options *options) {
    if(!options || !options->path) {
        fputs("[boot] missing guest image path\n",stderr);
        return 1;
    }
    struct nso_boot_entry entry={0};
    entry.boot=1;
    return nso_owned_profile(options->path,options->quota,options->stack,options->resources,
                             options->guest_argc,options->guest_argv,&entry);
}
#else
int nso_owned_profile(const char *path,size_t quota,size_t stack,size_t resources,size_t guest_argc,const char *const guest_argv[],struct nso_boot_entry *entry) {
    (void)path;(void)quota;(void)stack;(void)resources;(void)guest_argc;(void)guest_argv;(void)entry;
    fputs("[boot] this build has no owned guest profile\n",stderr);
    return 1;
}
int nso_boot_run(const nso_boot_options *options) {
    (void)options;
    fputs("[boot] this build has no owned guest profile\n",stderr);
    return 1;
}
#endif /* NSO_WITH_GUEST_PROFILE */
