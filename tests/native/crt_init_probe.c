/* Diagnostic only: capture the 21 GP words prepared by this main's DT_INIT.
 * This is not a recovered source declaration or a CRT compatibility adapter.
 * No constructors are allowed to continue beyond this observation point. */
#include "../../src/runtime/nso/nso_loader.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WORD_PARAMETERS \
    uint64_t a0,uint64_t a1,uint64_t a2,uint64_t a3,uint64_t a4, \
    uint64_t a5,uint64_t a6,uint64_t a7,uint64_t a8,uint64_t a9, \
    uint64_t a10,uint64_t a11,uint64_t a12,uint64_t a13,uint64_t a14, \
    uint64_t a15,uint64_t a16,uint64_t a17,uint64_t a18,uint64_t a19,uint64_t a20
#define WORD_ARGUMENTS a0,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11,a12,a13,a14,a15,a16,a17,a18,a19,a20
typedef uint32_t (*dso_words_fn)(WORD_PARAMETERS);
static nso_module *main_module,*sdk_module;
static int call_sdk;
/* GOT slot order follows the original register and stack stores. */
static const uint64_t slots[21]={
    0x776f120,0x776f128,0x776f130,0x776f138,0x776f140,0x776f148,0x776f158,
    0x776f160,0x776f170,0x776f168,0x776f118,0x776f150,0x776f110,0x776f108,
    0x776f100,0x776f0f8,0x776f0f0,0x776f0e8,0x776f0e0,0x776f0d8,0x776f0d0
};
static uint32_t observe_init(WORD_PARAMETERS) {
    uint64_t args[21]={WORD_ARGUMENTS};
    uint64_t base=nso_base(main_module);
    for(size_t i=0;i<21;i++) {
        uint64_t expected;
        memcpy(&expected,(void*)(uintptr_t)nso_addr(main_module,slots[i]),8);
        printf("argument[%zu] location=%s%zu got=0x%" PRIx64 " module_offset=0x%" PRIx64 " match=%d\n",
            i,i<8?"x":"sp+",i<8?i:(i-8)*8,slots[i],args[i]-base,args[i]==expected);
        if(args[i]!=expected) {fflush(stdout);exit(1);}
    }
    if(call_sdk) {
        uint32_t index=nso_find_symbol(sdk_module,"_ZN2nn2os40MemoryAllocatorForThreadLocalInitializedEv");
        uint64_t gate_offset=nso_symbol_value(sdk_module,index);
        if(!index || gate_offset!=0x144620)exit(1);
        /* The reviewed gate reads an SDK-owned zero-initialized pointer. No
         * host TP/TLS access, setter, SDK DT_INIT or constructor is invoked. */
        uint64_t gate=nso_call(sdk_module,gate_offset,0,0,0,0,0,0);
        printf("sdk_tls_allocator_initialized=%" PRIu64 "\n",gate);
        if(gate) {fflush(stdout);exit(1);}
        index=nso_find_symbol(sdk_module,"__nnmusl_init_dso");
        uint64_t offset=nso_symbol_value(sdk_module,index);
        if(!index || offset!=0x98e30)exit(1);
        nso_install_fault_trap(sdk_module);
        uint32_t result=((dso_words_fn)(uintptr_t)nso_addr(sdk_module,offset))(WORD_ARGUMENTS);
        printf("sdk_init_dso_result=%u expected=2 initialized=0\n",result);
        if(result!=2) {fflush(stdout);exit(1);}
    }
    puts("diagnostic_stop=86 constructors_not_resumed=1");
    fflush(stdout);
    /* Intentionally stop before main's result handling/ProtectRelro/arrays.
     * Nothing here reports DSO initialization or game startup success. */
    exit(86);
}
static uint64_t resolve(uint32_t index,const char *name) {
    (void)index;
    return !strcmp(name,"__nnmusl_init_dso")?(uintptr_t)&observe_init:0;
}
int main(int argc,char **argv) {
    if(argc!=4 || (strcmp(argv[3],"capture") && strcmp(argv[3],"sdk-gate")))return 2;
    call_sdk=!strcmp(argv[3],"sdk-gate");
    char err[256];
    if(call_sdk) {
        sdk_module=nso_load_file(argv[2],NULL,err,sizeof(err));
        if(!sdk_module) {fprintf(stderr,"SDK: %s\n",err);return 1;}
    }
    main_module=nso_load_file(argv[1],resolve,err,sizeof(err));
    if(!main_module) {fprintf(stderr,"main: %s\n",err);return 1;}
    nso_install_fault_trap(main_module);
    nso_run_init_policy(main_module,NSO_INIT_DT_INIT_OWNS_ARRAY);
    fputs("unexpected DT_INIT return\n",stderr);
    return 1;
}
