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

typedef struct Context {
    void (*entry)(void);
    uint64_t native_seen, sp_seen, guest_after;
    nb_registers observed;
    nb_scope *scope;
    unsigned callbacks;
} Context;
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
static void expect_import_traps(nb_scope *scope) {
    nb_import binding={0};nb_signature signature=scalar_signature();
    assert(nb_import_bind(&binding,scope,contract_host_raw,&signature)==NB_OK);
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
    assert(nb_scope_close(data->foreign)==NB_WRONG_THREAD);
    nb_scope scope={0};assert(nb_scope_init(&scope)==NB_OK);
    data->owner=scope.owner_tp;
    run_scope(&scope,0);
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
    run_scope(&scope,1);run_scope(&scope,0);fp_contract(&scope);expect_import_traps(&scope);
    ThreadData data={.foreign=&scope};pthread_t thread;
    assert(!pthread_create(&thread,NULL,thread_contract,&data));assert(!pthread_join(thread,NULL));
    assert(data.result==1 && data.owner!=scope.owner_tp);
    nb_registers dead_registers={0};
    assert(nb_call(&data.abandoned,contract_leaf,&dead_registers)==NB_OWNER_DEAD);
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
         "\"game_input\":false}\n",(getauxval(AT_HWCAP2)&HWCAP2_BTI)?"true":"false");return 0;
}
#else
int main(void) {
    nb_scope scope={0};nb_registers registers={0};
    assert(nb_scope_init(&scope)==NB_UNSUPPORTED);
    assert(nb_call(&scope,NULL,&registers)==NB_UNSUPPORTED);
    puts("{\"status\":\"UNSUPPORTED\",\"platform\":\"non_android_a64\",\"android_execution\":\"NOT_RUN\"}");return 0;
}
#endif
