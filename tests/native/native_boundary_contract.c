/* SPDX-License-Identifier: MIT: owned ABI contract only, no game input. */
#define _POSIX_C_SOURCE 200809L
#include "../../src/runtime/nso/native_boundary.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#if defined(__ANDROID__) && defined(__aarch64__)
#include <pthread.h>
#include <signal.h>
#include <sys/auxv.h>
#include <asm/hwcap.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(NB_CLEANUP_CONTRACT)
#include <errno.h>
#include <sys/mman.h>
/* Linked only into the separate cleanup contract with --wrap. These symbols
 * and failure controls are absent from the normal/production ELF.
 */
extern int __real_mprotect(void *,size_t,int);
extern int __real_munmap(void *,size_t);
static unsigned fail_protect,fail_unmap,protect_calls,unmap_calls,real_unmap_successes;
static void *last_unmap_address;
static size_t last_unmap_bytes;
int __wrap_mprotect(void *address,size_t bytes,int protection) {
    ++protect_calls;
    if(fail_protect){--fail_protect;errno=EACCES;return -1;}
    return __real_mprotect(address,bytes,protection);
}
int __wrap_munmap(void *address,size_t bytes) {
    ++unmap_calls;last_unmap_address=address;last_unmap_bytes=bytes;
    if(fail_unmap){--fail_unmap;errno=EBUSY;return -1;}
    int result=__real_munmap(address,bytes);
    if(!result)++real_unmap_successes;
    return result;
}
static void cleanup_unused_target(void) { }
static nb_signature cleanup_signature(void) {
    return (nb_signature){.version=1,.flags=NB_SIG_REGISTER_ONLY};
}
int main(void) {
    nb_scope scope={0};nb_import binding={0};nb_signature signature=cleanup_signature();
    assert(nb_scope_init(&scope)==NB_OK);
    /* First prove the ordinary protect-failure rollback still releases once. */
    fail_protect=1;
    assert(nb_import_bind(&binding,&scope,cleanup_unused_target,&signature)==NB_PROTECT_FAILED);
    assert(!binding.mapping&&!binding.entry&&!binding.scope&&!scope.binding_count);
    assert(protect_calls==1&&unmap_calls==1&&real_unmap_successes==1);
    assert(nb_scope_close(&scope)==NB_OK);
    assert(nb_scope_init(&scope)==NB_OK);
    protect_calls=unmap_calls=real_unmap_successes=0;
    fail_protect=1;fail_unmap=2;
    nb_status status=nb_import_bind(&binding,&scope,cleanup_unused_target,&signature);
    if(status!=NB_RETAINED) {
        /* A regression binary links the original 2f component to this exact
         * test. It must report FAIL/1 rather than hiding the lost ownership.
         * Its lost mapping is reclaimed only by process teardown.
         */
        nb_status close_status=nb_scope_close(&scope);
        printf("{\"status\":\"FAIL\",\"case\":\"double_failure_retention\",\"bind_status\":%u,"
            "\"scope_close_status\":%u,\"mapping_retained\":%s,\"munmap_attempts\":%u,"
            "\"actual_unmap_successes\":%u,\"cleanup\":\"process_teardown\"}\n",
            (unsigned)status,(unsigned)close_status,binding.mapping?"true":"false",
            unmap_calls,real_unmap_successes);
        return 1;
    }
    assert(binding.scope==&scope&&binding.mapping==last_unmap_address);
    assert(binding.mapping_bytes==last_unmap_bytes&&binding.mapping_bytes>0);
    assert(!binding.entry&&!binding.bti_guarded&&!nb_import_entry(&binding));
    assert(scope.binding_count==1&&scope.lifetime&&nb_scope_close(&scope)==NB_BUSY);
    void *saved=binding.mapping;size_t saved_bytes=binding.mapping_bytes;
    assert(nb_import_bind(&binding,&scope,cleanup_unused_target,&signature)==NB_INVALID);
    assert(nb_import_unbind(&binding)==NB_MAP_FAILED);
    assert(binding.mapping==saved&&binding.mapping_bytes==saved_bytes&&binding.scope==&scope);
    assert(!binding.entry&&scope.binding_count==1&&nb_scope_close(&scope)==NB_BUSY);
    assert(protect_calls==1&&unmap_calls==2&&real_unmap_successes==0);
    assert(nb_import_unbind(&binding)==NB_OK);
    assert(!binding.mapping&&!binding.entry&&!binding.scope&&!binding.mapping_bytes);
    assert(scope.binding_count==0&&unmap_calls==3&&real_unmap_successes==1);
    assert(nb_import_unbind(&binding)==NB_OK);
    assert(unmap_calls==3&&real_unmap_successes==1);
    assert(nb_scope_close(&scope)==NB_OK);
    printf("{\"status\":\"PASS\",\"scope\":\"owned_cleanup_failure_contract_only\","
        "\"protect_failure_rollback_success\":true,\"double_failure_retained\":true,"
        "\"entry_null\":true,\"scope_busy_until_actual_unmap\":true,"
        "\"failed_retry_preserves_owner\":true,\"actual_unmap_once\":true,"
        "\"count_refunded_once\":true,\"idempotent_unbind\":true,"
        "\"munmap_attempts\":%u,\"actual_unmap_successes\":%u,\"binding_count_after\":%" PRIu64 ","
        "\"game_input\":false}\n",unmap_calls,real_unmap_successes,scope.binding_count);
    return 0;
}
#else
typedef struct Context {
    void (*entry)(void);
    uint64_t native_seen, sp_seen, guest_after;
    nb_registers observed;
    nb_scope *scope;
    unsigned callbacks;
} Context;
typedef struct StackContext {
    void (*entry)(void);
    uint64_t native_seen,sp_seen,guest_after;
    uint64_t observed[21];
    uint64_t canary_before;
    nb_stack13 outgoing;
    uint64_t canary_after;
    nb_scope *scope;
    uint64_t nested_checks;
    _Alignas(16) unsigned char q_seen[8][16];
    uint64_t x8_seen;
} StackContext;
_Static_assert(offsetof(StackContext,observed)==32&&offsetof(StackContext,outgoing)==208&&
    offsetof(StackContext,q_seen)==336&&offsetof(StackContext,x8_seen)==464,"stack21 test ABI");
extern void stack_guest_record(void),stack_guest_construct(void),stack_guest_tail(void),stack_host_wrapper(void);
__asm__(
".text\n.p2align 2\n.global stack_guest_record\n.type stack_guest_record,%function\nstack_guest_record:\n"
".inst 0xd503245f\n mov x9,sp\n str x9,[x0,#16]\n str x8,[x0,#464]\n"
"stp x0,x1,[x0,#32]\n stp x2,x3,[x0,#48]\n stp x4,x5,[x0,#64]\n stp x6,x7,[x0,#80]\n"
"stp q0,q1,[x0,#336]\n stp q2,q3,[x0,#368]\n stp q4,q5,[x0,#400]\n stp q6,q7,[x0,#432]\n"
"ldp x9,x10,[sp]\n stp x9,x10,[x0,#96]\n ldp x9,x10,[sp,#16]\n stp x9,x10,[x0,#112]\n"
"ldp x9,x10,[sp,#32]\n stp x9,x10,[x0,#128]\n ldp x9,x10,[sp,#48]\n stp x9,x10,[x0,#144]\n"
"ldp x9,x10,[sp,#64]\n stp x9,x10,[x0,#160]\n ldp x9,x10,[sp,#80]\n stp x9,x10,[x0,#176]\n"
"ldr x9,[sp,#96]\n str x9,[x0,#192]\n mov x18,#0x13\n str x18,[x0,#24]\n"
"add x11,x0,#32\n mov x10,#21\n mov x0,#0x5678\n movk x0,#0x1234,lsl#16\n"
"1: ldr x12,[x11],#8\n ror x0,x0,#7\n eor x0,x0,x12\n subs x10,x10,#1\n b.ne 1b\n"
"str xzr,[sp]\n ret\n"
".global stack_guest_construct\n.type stack_guest_construct,%function\nstack_guest_construct:\n.inst 0xd503245f\n"
"sub sp,sp,#144\n stp x29,x30,[sp,#112]\n stp x19,x20,[sp,#128]\n add x29,sp,#112\n mov x19,x0\n"
"ldp x9,x10,[x19,#208]\n stp x9,x10,[sp]\n ldp x9,x10,[x19,#224]\n stp x9,x10,[sp,#16]\n"
"ldp x9,x10,[x19,#240]\n stp x9,x10,[sp,#32]\n ldp x9,x10,[x19,#256]\n stp x9,x10,[sp,#48]\n"
"ldp x9,x10,[x19,#272]\n stp x9,x10,[sp,#64]\n ldp x9,x10,[x19,#288]\n stp x9,x10,[sp,#80]\n"
"ldr x9,[x19,#304]\n str x9,[sp,#96]\n str xzr,[sp,#104]\n"
"ldp x1,x2,[x19,#40]\n ldp x3,x4,[x19,#56]\n ldp x5,x6,[x19,#72]\n ldr x7,[x19,#88]\n"
"mov x18,#0x21\n ldr x16,[x19]\n blr x16\n str x18,[x19,#24]\n"
"ldp x19,x20,[sp,#128]\n ldp x29,x30,[sp,#112]\n add sp,sp,#144\n ret\n"
".global stack_guest_tail\n.type stack_guest_tail,%function\nstack_guest_tail:\n.inst 0xd503245f\n"
"mov x18,#0x31\n ldr x16,[x0]\n br x16\n"
".global stack_host_wrapper\n.type stack_host_wrapper,%function\nstack_host_wrapper:\n.inst 0xd503245f\n"
"str x18,[x0,#8]\n mov x9,sp\n str x9,[x0,#16]\n str x8,[x0,#464]\n"
"stp q0,q1,[x0,#336]\n stp q2,q3,[x0,#368]\n stp q4,q5,[x0,#400]\n stp q6,q7,[x0,#432]\n"
"b stack_host21\n");
_Static_assert(offsetof(Context, observed) == 32, "owned test ABI");
extern void contract_guest(void), contract_leaf(void), contract_host_raw(void), contract_callback_wrapper(void);
extern void contract_fp_guest(void),contract_fp_wrapper(void);
__asm__(
".text\n.p2align 2\n.global contract_guest\n.type contract_guest,%function\ncontract_guest:\n"
".inst 0xd503245f\n stp x29,x30,[sp,#-16]!\n mov x29,sp\n mov x18,#1\n ldr x16,[x0]\n blr x16\n"
"str x18,[x0,#24]\n ldp x29,x30,[sp],#16\n ret\n"
".global contract_leaf\n.type contract_leaf,%function\ncontract_leaf:\n.inst 0xd503245f\n"
"mov x18,#2\n mov x0,#0x777\n mov x19,#0xdead\n mov x20,#0xdead\n mov x21,#0xdead\n"
"mov x22,#0xdead\n mov x23,#0xdead\n mov x24,#0xdead\n mov x25,#0xdead\n mov x26,#0xdead\n"
"mov x27,#0xdead\n mov x28,#0xdead\n mov x29,#0xbeef\n movi v8.16b,#0xee\n movi v9.16b,#0xee\n"
"movi v10.16b,#0xee\n movi v11.16b,#0xee\n movi v12.16b,#0xee\n movi v13.16b,#0xee\n"
"movi v14.16b,#0xee\n movi v15.16b,#0xee\n ret\n"
".global contract_host_raw\n.type contract_host_raw,%function\ncontract_host_raw:\n.inst 0xd503245f\n"
"str x18,[x0,#8]\n mov x9,sp\n str x9,[x0,#16]\n"
"stp x0,x1,[x0,#32]\n stp x2,x3,[x0,#48]\n stp x4,x5,[x0,#64]\n stp x6,x7,[x0,#80]\n"
"str x8,[x0,#96]\n stp q0,q1,[x0,#112]\n stp q2,q3,[x0,#144]\n"
"stp q4,q5,[x0,#176]\n stp q6,q7,[x0,#208]\n mov x9,#0x5a5a\n str x9,[x8]\n"
"add x1,x1,#7\n add x2,x2,#7\n add x3,x3,#7\n add x4,x4,#7\n add x5,x5,#7\n add x6,x6,#7\n add x7,x7,#7\n"
"movi v0.16b,#0xa0\n movi v1.16b,#0xa1\n movi v2.16b,#0xa2\n movi v3.16b,#0xa3\n"
"movi v4.16b,#0xa4\n movi v5.16b,#0xa5\n movi v6.16b,#0xa6\n movi v7.16b,#0xa7\n ret\n"
".global contract_callback_wrapper\n.type contract_callback_wrapper,%function\ncontract_callback_wrapper:\n"
".inst 0xd503245f\n str x18,[x0,#8]\n mov x9,sp\n str x9,[x0,#16]\n b contract_host_callback\n");
__asm__(
".text\n.p2align 2\n.global contract_fp_guest\n.type contract_fp_guest,%function\ncontract_fp_guest:\n"
".inst 0xd503245f\n stp x29,x30,[sp,#-32]!\n str x0,[sp,#16]\n mov x29,sp\n"
"mov x18,#3\n ldr x16,[x0]\n blr x16\n ldr x0,[sp,#16]\n str x18,[x0,#24]\n"
"ldp x29,x30,[sp],#32\n ret\n.global contract_fp_wrapper\n.type contract_fp_wrapper,%function\n"
"contract_fp_wrapper:\n.inst 0xd503245f\n str x18,[x0,#8]\n b contract_host_fp\n");

static uint64_t current_x18(void) {
    uint64_t value; __asm__ volatile("mov %0,x18" : "=r"(value)); return value;
}
static uint64_t current_tp(void) {
    uint64_t value; __asm__ volatile("mrs %0,tpidr_el0" : "=r"(value)); return value;
}
static nb_signature scalar_signature(void) {
    return (nb_signature){.version=1,.flags=NB_SIG_REGISTER_ONLY,.gp_arguments=8,
                          .fp_arguments=8,.gp_returns=1};
}

static nb_signature stack_signature(void) {
    return (nb_signature){.version=2,.flags=NB_SIG_FIXED_GP21,.stack_argument_bytes=104,
        .gp_arguments=8,.gp_returns=1};
}
static uint64_t digest21(const uint64_t words[21]) {
    uint64_t value=0x12345678;
    for(unsigned i=0;i<21;++i)value=((value>>7)|(value<<57))^words[i];
    return value;
}
static void prepare_stack(StackContext *context,nb_scope *scope,nb_registers *registers,
                          nb_stack13 *stack,uint64_t words[21]) {
    memset(context,0,sizeof(*context));context->scope=scope;
    context->canary_before=UINT64_C(0xabcddcba11223344);
    context->canary_after=UINT64_C(0x12344321baadcafe);
    memset(registers,0,sizeof(*registers));
    words[0]=(uintptr_t)context;
    for(unsigned i=1;i<21;++i)words[i]=UINT64_C(0x1100220033004400)+i*UINT64_C(0x102030405);
    for(unsigned i=0;i<8;++i)registers->x[i]=words[i];
    registers->x[8]=UINT64_C(0xbadc0de8);
    for(unsigned i=0;i<8;++i)memset(registers->q[i],0x20+i,16);
    for(unsigned i=0;i<13;++i)stack->words[i]=context->outgoing.words[i]=words[i+8];
    memcpy(context->observed,words,21*sizeof(uint64_t));
}
static void verify_stack(const StackContext *context,const uint64_t words[21]) {
    assert(!memcmp(context->observed,words,21*sizeof(uint64_t)));
    assert(context->observed[20]==words[20]&&!(context->sp_seen&15));
    assert(context->x8_seen==UINT64_C(0xbadc0de8));
    assert(context->canary_before==UINT64_C(0xabcddcba11223344));
    assert(context->canary_after==UINT64_C(0x12344321baadcafe));
    for(unsigned i=0;i<8;++i)for(unsigned j=0;j<16;++j)assert(context->q_seen[i][j]==0x20+i);
}

/* A real typed 21-argument C target, compiled with SCS. It is a checksum and
 * observation contract, has no musl name/semantics, and never returns fake CRT
 * success. Each argument contributes to a data-dependent result.
 */
__attribute__((noinline)) uint64_t stack_host21(StackContext *context,
    uint64_t a1,uint64_t a2,uint64_t a3,uint64_t a4,uint64_t a5,uint64_t a6,uint64_t a7,
    uint64_t a8,uint64_t a9,uint64_t a10,uint64_t a11,uint64_t a12,uint64_t a13,uint64_t a14,
    uint64_t a15,uint64_t a16,uint64_t a17,uint64_t a18,uint64_t a19,uint64_t a20) {
    uint64_t words[21]={(uintptr_t)context,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11,a12,a13,a14,a15,a16,a17,a18,a19,a20};
    memcpy(context->observed,words,sizeof(words));
    assert(context->native_seen==context->scope->host_x18&&!(context->sp_seen&15));
    uint64_t native=current_x18();assert(pthread_self());
    nb_scope nested={0};StackContext nested_context;nb_stack13 stack;nb_registers registers;
    uint64_t expected[21];prepare_stack(&nested_context,&nested,&registers,&stack,expected);
    assert(nb_call_stack13(context->scope,stack_guest_record,&registers,&stack)==NB_BUSY);
    assert(nb_scope_init(&nested)==NB_OK);
    assert(nb_call_stack13(&nested,stack_guest_record,&registers,&stack)==NB_OK);
    assert(registers.x[0]==digest21(expected)&&registers.guest_x18==0x13);
    verify_stack(&nested_context,expected);
    assert(nb_scope_close(&nested)==NB_OK&&current_x18()==native);
    ++context->nested_checks;
    return digest21(words);
}

static void fixed_stack_contract(nb_scope *scope) {
    StackContext contexts[2];nb_registers registers;uint64_t words[21];
    struct Guarded {uint64_t before;nb_stack13 stack;uint64_t after;} guarded;
    guarded.before=UINT64_C(0xfedcba9876543210);guarded.after=UINT64_C(0x0123456789abcdef);
    uint64_t previous=0;
    /* Independent host->guest reader: 21 single-argument variations, including
     * a second live context pointer and the thirteenth stacked word. */
    for(unsigned variant=0;variant<22;++variant) {
        StackContext *context=&contexts[variant==1?1:0];
        prepare_stack(context,scope,&registers,&guarded.stack,words);
        if(variant>1)words[variant-1]^=UINT64_C(0x8000000000000001);
        for(unsigned i=0;i<8;++i)registers.x[i]=words[i];
        for(unsigned i=0;i<13;++i)guarded.stack.words[i]=words[i+8];
        uint64_t native=current_x18();
        assert(nb_call_stack13(scope,stack_guest_record,&registers,&guarded.stack)==NB_OK);
        assert(current_x18()==native&&registers.guest_x18==0x13);
        assert(registers.x[0]==digest21(words)&&registers.x[0]!=0);
        if(variant)assert(registers.x[0]!=previous);
        if(!variant)previous=registers.x[0];
        verify_stack(context,words);
        assert(!memcmp(guarded.stack.words,words+8,104));
        assert(guarded.before==UINT64_C(0xfedcba9876543210)&&guarded.after==UINT64_C(0x0123456789abcdef));
    }
    nb_import binding={0};nb_signature signature=stack_signature();
    assert(nb_import_bind(&binding,scope,stack_host_wrapper,&signature)==NB_OK);
    /* Independent guest->host route starts via register-only nb_call. The
     * guest constructs its OWN outgoing 13 words, not nb_call_stack13's area. */
    previous=0;
    for(unsigned variant=0;variant<22;++variant) {
        StackContext *context=&contexts[variant==1?1:0];
        prepare_stack(context,scope,&registers,&guarded.stack,words);context->entry=binding.entry;
        if(variant>1)words[variant-1]^=UINT64_C(0x8000000000000001);
        memcpy(context->observed,words,sizeof(words));
        for(unsigned i=0;i<13;++i)context->outgoing.words[i]=words[i+8];
        uint64_t native=current_x18();
        assert(nb_call(scope,stack_guest_construct,&registers)==NB_OK);
        assert(current_x18()==native&&registers.guest_x18==0x21&&context->guest_after==0x21);
        assert(registers.x[0]==digest21(words)&&registers.x[0]!=0);
        if(variant)assert(registers.x[0]!=previous);
        if(!variant)previous=registers.x[0];
        assert(context->nested_checks==1);verify_stack(context,words);
    }
    StackContext *context=&contexts[0];
    prepare_stack(context,scope,&registers,&guarded.stack,words);context->entry=binding.entry;
    /* Combined round-trip reads the real incoming SP, not context.outgoing. */
    memset(&context->outgoing,0,sizeof(context->outgoing));
    assert(nb_call_stack13(scope,stack_guest_tail,&registers,&guarded.stack)==NB_OK);
    assert(registers.x[0]==digest21(words)&&registers.guest_x18==0x31);
    verify_stack(context,words);assert(context->nested_checks==1);
    assert(nb_import_unbind(&binding)==NB_OK);
    assert(nb_call_stack13(scope,stack_guest_record,&registers,NULL)==NB_INVALID);
    assert(!scope->phase&&!scope->host_x18);
    for(unsigned variant=0;variant<14;++variant) {
        signature=stack_signature();
        switch(variant) {
        case 0:signature.version=1;break;case 1:signature.flags=NB_SIG_REGISTER_ONLY;break;
        case 2:signature.stack_argument_bytes=96;break;case 3:signature.stack_argument_bytes=112;break;
        case 4:signature.gp_arguments=7;break;case 5:signature.gp_arguments=9;break;
        case 6:signature.fp_arguments=1;break;case 7:signature.gp_returns=0;break;
        case 8:signature.fp_returns=1;break;case 9:signature.variadic=1;break;
        case 10:signature.aggregate_arguments=1;break;case 11:signature.aggregate_returns=1;break;
        case 12:signature.unknown_layout=1;break;case 13:signature.flags|=NB_SIG_SRET_X8;break;
        }
        assert(nb_import_bind(&binding,scope,stack_host_wrapper,&signature)==NB_UNSUPPORTED);
        assert(!binding.mapping&&!scope->binding_count);
    }
}

/* Compiled with real Clang SCS. The assembly wrapper records x18 before this
 * C function's shadow-stack prologue; nb_call captures its current depth again.
 */
__attribute__((noinline)) Context *contract_host_callback(Context *context) {
    assert(context->native_seen == context->scope->host_x18);
    assert(!(context->sp_seen & 15));
    uint64_t before = current_x18();
    assert(pthread_self());
    nb_registers untouched = {0};
    untouched.x[0]=0x1234;
    assert(nb_call(context->scope,contract_leaf,&untouched)==NB_BUSY);
    assert(untouched.x[0]==0x1234 && context->scope->phase==2);
    nb_scope nested={0}; nb_registers nested_registers={0};
    assert(nb_scope_init(&nested)==NB_OK);
    assert(nb_call(&nested,contract_leaf,&nested_registers)==NB_OK);
    assert(nested_registers.x[0]==0x777 && nested_registers.guest_x18==2);
    assert(nb_scope_close(&nested)==NB_OK);
    assert(current_x18()==before);
    ++context->callbacks;
    return context;
}

__attribute__((noinline)) double contract_host_fp(Context *context,double left,double right) {
    assert(context->native_seen==context->scope->host_x18);
    assert(pthread_self());
    return left+right;
}

static void fp_contract(nb_scope *scope) {
    nb_signature signature={.version=1,.flags=NB_SIG_REGISTER_ONLY,.gp_arguments=1,
        .fp_arguments=2,.fp_returns=1};
    nb_import binding={0};Context context={.scope=scope};nb_registers registers={0};
    assert(nb_import_bind(&binding,scope,contract_fp_wrapper,&signature)==NB_OK);
    context.entry=binding.entry;registers.x[0]=(uintptr_t)&context;
    double left=5.25,right=2.5,result=0;
    memcpy(registers.q[0],&left,sizeof(left));memcpy(registers.q[1],&right,sizeof(right));
    assert(nb_call(scope,contract_fp_guest,&registers)==NB_OK);
    memcpy(&result,registers.q[0],sizeof(result));
    assert(result==7.75 && registers.guest_x18==3 && context.guest_after==3);
    assert(nb_import_unbind(&binding)==NB_OK);
}

static void run_scope(nb_scope *scope, int raw) {
    Context context={.scope=scope}; nb_import binding={0};
    nb_signature signature=scalar_signature();
    if(raw){signature.flags|=NB_SIG_SRET_X8;signature.gp_returns=0;}
    assert(nb_import_bind(&binding,scope,raw?contract_host_raw:contract_callback_wrapper,&signature)==NB_OK);
    context.entry=nb_import_entry(&binding);
    assert(binding.bti_guarded==((getauxval(AT_HWCAP2)&HWCAP2_BTI)?1u:0u));
    assert(nb_scope_close(scope)==NB_BUSY);
    nb_registers registers={0};
    uint64_t indirect=0;
    registers.x[0]=(uintptr_t)&context;
    for(unsigned i=1;i<8;++i)registers.x[i]=0x100+i;
    registers.x[8]=(uintptr_t)&indirect;
    for(unsigned i=0;i<8;++i)memset(registers.q[i],0x10+i,16);
    uint64_t native=current_x18();
    assert(nb_call(scope,contract_guest,&registers)==NB_OK);
    assert(current_x18()==native && scope->phase==0 && scope->host_x18==0);
    assert(registers.guest_x18==1 && context.guest_after==1 && context.native_seen==native);
    assert(!(context.sp_seen&15));
    if(raw) {
        assert(context.observed.x[0]==(uintptr_t)&context && context.observed.x[8]==(uintptr_t)&indirect);
        assert(indirect==0x5a5a && registers.x[8]==(uintptr_t)&indirect);
        for(unsigned i=1;i<8;++i)assert(context.observed.x[i]==0x100+i && registers.x[i]==0x107+i);
        for(unsigned i=0;i<8;++i)for(unsigned j=0;j<16;++j) {
            assert(context.observed.q[i][j]==0x10+i); assert(registers.q[i][j]==0xa0+i);
        }
    } else assert(context.callbacks==1);
    assert(nb_import_unbind(&binding)==NB_OK && nb_import_unbind(&binding)==NB_OK);
}

static void *wrong_thread_import(void *opaque) {
    nb_import *binding=opaque;binding->entry();return NULL;
}
static void expect_import_traps(nb_scope *scope,int fixed_stack) {
    nb_import binding={0};nb_signature signature=scalar_signature();
    if(fixed_stack)signature=stack_signature();
    assert(nb_import_bind(&binding,scope,fixed_stack?stack_host_wrapper:contract_host_raw,&signature)==NB_OK);
    for(unsigned wrong_thread=0;wrong_thread<2;++wrong_thread) {
        pid_t child=fork();assert(child>=0);
        if(!child) {
            if(wrong_thread) {
                pthread_t thread;assert(!pthread_create(&thread,NULL,wrong_thread_import,&binding));
                (void)pthread_join(thread,NULL);
            } else binding.entry();
            _exit(99);
        }
        int status=0;assert(waitpid(child,&status,0)==child);
        assert(WIFSIGNALED(status)&&WTERMSIG(status)==SIGTRAP);
    }
    assert(nb_import_unbind(&binding)==NB_OK);
}

typedef struct ThreadData {nb_scope *foreign;uint64_t owner;int result;
    nb_scope abandoned;nb_import abandoned_binding;} ThreadData;
static void *thread_contract(void *opaque) {
    ThreadData *data=opaque;nb_registers registers={0};
    assert(nb_call(data->foreign,contract_leaf,&registers)==NB_WRONG_THREAD);
    nb_stack13 stack={0};
    assert(nb_call_stack13(data->foreign,stack_guest_record,&registers,&stack)==NB_WRONG_THREAD);
    assert(nb_scope_close(data->foreign)==NB_WRONG_THREAD);
    nb_scope scope={0};assert(nb_scope_init(&scope)==NB_OK);
    data->owner=scope.owner_tp;
    run_scope(&scope,0);
    fixed_stack_contract(&scope);
    assert(nb_scope_close(&scope)==NB_OK);
    assert(nb_scope_init(&data->abandoned)==NB_OK);
    nb_signature signature=scalar_signature();
    assert(nb_import_bind(&data->abandoned_binding,&data->abandoned,contract_host_raw,&signature)==NB_OK);
    data->result=1;return NULL;
}

__attribute__((no_sanitize("shadow-call-stack"))) int main(void) {
    uint64_t native=current_x18();
    if(!native) {puts("{\"status\":\"UNSUPPORTED\",\"reason\":\"native_x18_is_zero\"}");return 77;}
    nb_scope scope={0};assert(nb_scope_init(&scope)==NB_OK);
    assert(scope.owner_tp==current_tp());
    nb_import rejected={0}; nb_signature signature=scalar_signature();
    signature.stack_argument_bytes=8;assert(nb_import_bind(&rejected,&scope,contract_host_raw,&signature)==NB_UNSUPPORTED);
    signature=scalar_signature();signature.variadic=1;assert(nb_import_bind(&rejected,&scope,contract_host_raw,&signature)==NB_UNSUPPORTED);
    signature=scalar_signature();signature.aggregate_arguments=1;assert(nb_import_bind(&rejected,&scope,contract_host_raw,&signature)==NB_UNSUPPORTED);
    run_scope(&scope,1);run_scope(&scope,0);fp_contract(&scope);fixed_stack_contract(&scope);
    expect_import_traps(&scope,0);expect_import_traps(&scope,1);
    ThreadData data={.foreign=&scope};pthread_t thread;
    assert(!pthread_create(&thread,NULL,thread_contract,&data));assert(!pthread_join(thread,NULL));
    assert(data.result==1 && data.owner!=scope.owner_tp);
    nb_registers dead_registers={0};
    assert(nb_call(&data.abandoned,contract_leaf,&dead_registers)==NB_OWNER_DEAD);
    nb_stack13 dead_stack={0};
    assert(nb_call_stack13(&data.abandoned,stack_guest_record,&dead_registers,&dead_stack)==NB_OWNER_DEAD);
    assert(nb_import_unbind(&data.abandoned_binding)==NB_OK);
    assert(nb_scope_close(&data.abandoned)==NB_OK);
    assert(nb_scope_close(&scope)==NB_OK);
    assert(current_x18()==native);
    printf("{\"status\":\"PASS\",\"scope\":\"owned_android_a64_native_boundary_contract_only\","
         "\"raw_gp_fp_sret\":true,\"guest_x18_clobber\":true,\"host_scs_callback\":true,"
         "\"same_scope_busy\":true,\"distinct_scope_nested\":true,\"thread_isolation\":true,"
         "\"inactive_import_sigtrap\":true,\"unsupported_signatures_rejected\":true,"
         "\"dead_owner_rejected_and_released\":true,"
         "\"typed_double_return\":true,"
         "\"wrong_thread_import_sigtrap\":true,\"bti_guarded_thunks\":%s,"
         "\"fixed_gp21_host_to_guest\":true,\"fixed_gp21_guest_to_host\":true,"
         "\"fixed_gp21_combined\":true,\"every_argument_mutation\":true,\"stack_canaries\":true,"
         "\"fixed_gp21_nested_threads_lifecycle\":true,\"fixed_gp21_signature_rejections\":14,"
         "\"game_input\":false}\n",(getauxval(AT_HWCAP2)&HWCAP2_BTI)?"true":"false");return 0;
}
#endif /* NB_CLEANUP_CONTRACT */
#else
int main(void) {
    nb_scope scope={0};nb_registers registers={0};
    nb_stack13 stack={0};
    assert(nb_scope_init(&scope)==NB_UNSUPPORTED);
    assert(nb_call(&scope,NULL,&registers)==NB_UNSUPPORTED);
    assert(nb_call_stack13(&scope,NULL,&registers,&stack)==NB_UNSUPPORTED);
    puts("{\"status\":\"UNSUPPORTED\",\"platform\":\"non_android_a64\",\"android_execution\":\"NOT_RUN\"}");return 0;
}
#endif
