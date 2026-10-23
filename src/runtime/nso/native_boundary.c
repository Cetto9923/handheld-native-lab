/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "native_boundary.h"
#include <string.h>
#if defined(__ANDROID__) && defined(__aarch64__)
#include <sys/mman.h>
#include <sys/auxv.h>
#include <asm/hwcap.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
_Static_assert(offsetof(nb_scope, owner_tp) == 8 && offsetof(nb_scope, host_x18) == 16 &&
               offsetof(nb_scope, phase) == 24 && sizeof(nb_scope) == 64, "scope assembly ABI");
_Static_assert(offsetof(nb_registers, guest_x18) == 72 && offsetof(nb_registers, q) == 80 &&
               sizeof(nb_registers) == 208, "register assembly ABI");
_Static_assert(offsetof(nb_scope, lifetime)==40, "lifetime assembly ABI");
_Static_assert(offsetof(nb_scope,active_guest_x18)==48 && offsetof(nb_scope,callback_depth)==56,
               "host callback assembly ABI");
_Static_assert(NB_MAX_CALLBACK_DEPTH==4 && NB_DEPTH_LIMIT==10,"host callback assembly bound/status");
_Static_assert(sizeof(nb_stack13)==104, "fixed GP21 stack payload");
typedef struct nb_lifetime { uint64_t live, refs; } nb_lifetime;
static pthread_key_t lifetime_key;
static pthread_once_t lifetime_once=PTHREAD_ONCE_INIT;
static int lifetime_error;
static void lifetime_drop(nb_lifetime *life) {
    if (__atomic_sub_fetch(&life->refs,1,__ATOMIC_ACQ_REL)==0) free(life);
}
static void lifetime_end(void *opaque) {
    nb_lifetime *life=opaque;
    __atomic_store_n(&life->live,0,__ATOMIC_RELEASE); lifetime_drop(life);
}
static void lifetime_key_init(void) { lifetime_error=pthread_key_create(&lifetime_key,lifetime_end); }
static nb_lifetime *lifetime_get(void) {
    if (pthread_once(&lifetime_once,lifetime_key_init) || lifetime_error) return NULL;
    nb_lifetime *life=pthread_getspecific(lifetime_key);
    if (!life) {
        life=calloc(1,sizeof(*life)); if(!life)return NULL;
        life->live=1;life->refs=1;
        if(pthread_setspecific(lifetime_key,life)){free(life);return NULL;}
    }
    return life;
}
extern void nb_import_gateway(void);
extern void nb_import_stack13_gateway(void);
static uint64_t thread_token(void) {
    uint64_t value;
    __asm__ volatile("mrs %0, tpidr_el0" : "=r"(value));
    return value;
}
static nb_status check_scope(const nb_scope *scope) {
    if (!scope || scope->magic != 0x4e42) return NB_INVALID;
    nb_lifetime *life=scope->lifetime;
    if(!life)return NB_INVALID;
    if(!__atomic_load_n(&life->live,__ATOMIC_ACQUIRE))return NB_OWNER_DEAD;
    if (scope->owner_tp != thread_token()) return NB_WRONG_THREAD;
    if (__atomic_load_n(&scope->phase, __ATOMIC_ACQUIRE) || scope->callback_depth) return NB_BUSY;
    return NB_OK;
}
#endif

nb_status nb_scope_init(nb_scope *scope) {
#if defined(__ANDROID__) && defined(__aarch64__)
    if (!scope || ((uintptr_t)scope & 7)) return NB_INVALID;
    if (scope->magic || scope->binding_count || scope->phase) return NB_BUSY;
    uint64_t token = thread_token();
    if (!token) return NB_UNSUPPORTED;
    nb_lifetime *life=lifetime_get();if(!life)return NB_SYSTEM_ERROR;
    __atomic_add_fetch(&life->refs,1,__ATOMIC_RELAXED);
    *scope = (nb_scope){.magic = 0x4e42, .owner_tp = token,.lifetime=life};
    return NB_OK;
#else
    (void)scope; return NB_UNSUPPORTED;
#endif
}

nb_status nb_scope_close(nb_scope *scope) {
#if defined(__ANDROID__) && defined(__aarch64__)
    nb_status status = check_scope(scope);
    if (status != NB_OK && status != NB_OWNER_DEAD) return status;
    if(__atomic_load_n(&scope->phase,__ATOMIC_ACQUIRE)||scope->callback_depth)return NB_BUSY;
    if (scope->binding_count) return NB_BUSY;
    nb_lifetime *life=scope->lifetime;
    memset(scope, 0, sizeof(*scope));lifetime_drop(life); return NB_OK;
#else
    (void)scope; return NB_UNSUPPORTED;
#endif
}

nb_status nb_import_bind(nb_import *binding, nb_scope *scope,
                        void (*target)(void), const nb_signature *signature) {
#if defined(__ANDROID__) && defined(__aarch64__)
    nb_status status = check_scope(scope);
    if (status != NB_OK) return status;
    if (!binding || binding->mapping || binding->entry || binding->scope || !target || !signature)
        return NB_INVALID;
    int excluded=signature->variadic || signature->aggregate_arguments ||
                 signature->unknown_layout || signature->aggregate_returns;
    int register_only=signature->version==1 && (signature->flags & NB_SIG_REGISTER_ONLY) &&
        !(signature->flags & ~(NB_SIG_REGISTER_ONLY | NB_SIG_SRET_X8)) &&
        !signature->stack_argument_bytes && !(signature->gp_arguments > 8 ||
        signature->fp_arguments > 8 || signature->gp_returns > 2 || signature->fp_returns > 1 ||
        (signature->gp_returns && signature->fp_returns) ||
        ((signature->flags & NB_SIG_SRET_X8) && (signature->gp_returns || signature->fp_returns)));
    int fixed_gp21=signature->version==2 && signature->flags==NB_SIG_FIXED_GP21 &&
        signature->stack_argument_bytes==104 && signature->gp_arguments==8 &&
        !signature->fp_arguments && signature->gp_returns==1 && !signature->fp_returns;
    if(excluded || (!register_only && !fixed_gp21))return NB_UNSUPPORTED;
    long page = sysconf(_SC_PAGESIZE);
    if (page < 64 || scope->binding_count == UINT64_MAX) return NB_INVALID;
    void *memory = mmap(NULL, (size_t)page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (memory == MAP_FAILED) return NB_MAP_FAILED;
    /* Fixed data-bearing ABI thunk only: BTI c; load scope, target, gateway; BR.
     * No guest instruction is decoded, copied, interpreted, or translated.
     */
    uint32_t instructions[8] = {0xd503245f, 0x580000f0, 0x58000111, 0x58000129,
                                0xd61f0120, 0xd503201f, 0xd503201f, 0xd503201f};
    uint64_t literals[4] = {(uintptr_t)scope, (uintptr_t)target,
        (uintptr_t)(fixed_gp21?nb_import_stack13_gateway:nb_import_gateway), 0};
    memcpy(memory, instructions, sizeof(instructions));
    memcpy((unsigned char *)memory + 32, literals, sizeof(literals));
    uint32_t bti=(getauxval(AT_HWCAP2)&HWCAP2_BTI)?1u:0u;
    if (mprotect(memory, (size_t)page, PROT_READ | PROT_EXEC | (bti?PROT_BTI:0))) {
        if (!munmap(memory, (size_t)page)) return NB_PROTECT_FAILED;
        /* RX publication failed and rollback did not release the allocation.
         * Retain a non-callable owner so scope/lifetime cannot be closed and a
         * later unbind retries the exact mapping. Never lose the only handle.
         */
        *binding = (nb_import){.scope=scope,.mapping=memory,.mapping_bytes=(size_t)page};
        ++scope->binding_count;
        return NB_RETAINED;
    }
    __builtin___clear_cache(memory, (char *)memory + 64);
    *binding = (nb_import){.scope = scope, .mapping = memory, .mapping_bytes = (size_t)page,
                          .entry = (void (*)(void))memory,.bti_guarded=bti};
    ++scope->binding_count;
    return NB_OK;
#else
    (void)binding; (void)scope; (void)target; (void)signature; return NB_UNSUPPORTED;
#endif
}

nb_status nb_import_unbind(nb_import *binding) {
#if defined(__ANDROID__) && defined(__aarch64__)
    if (!binding) return NB_INVALID;
    if (!binding->mapping && !binding->entry && !binding->scope) return NB_OK;
    nb_status status = check_scope(binding->scope);
    if (status != NB_OK && status != NB_OWNER_DEAD) return status;
    if(__atomic_load_n(&binding->scope->phase,__ATOMIC_ACQUIRE)||binding->scope->callback_depth)return NB_BUSY;
    if (!binding->mapping || !binding->mapping_bytes || !binding->scope->binding_count) return NB_INVALID;
    if (munmap(binding->mapping, binding->mapping_bytes)) return NB_MAP_FAILED;
    --binding->scope->binding_count;
    *binding = (nb_import){0}; return NB_OK;
#else
    (void)binding; return NB_UNSUPPORTED;
#endif
}

void (*nb_import_entry(const nb_import *binding))(void) {
    return binding ? binding->entry : NULL;
}

#if !defined(__ANDROID__) || !defined(__aarch64__)
nb_status nb_call(nb_scope *scope, void (*entry)(void), nb_registers *registers) {
    (void)scope; (void)entry; (void)registers; return NB_UNSUPPORTED;
}
nb_status nb_call_from_host(nb_scope *scope, void (*entry)(void), nb_registers *registers) {
    (void)scope;(void)entry;(void)registers;return NB_UNSUPPORTED;
}
nb_status nb_call_stack13(nb_scope *scope, void (*entry)(void),
                          nb_registers *registers, const nb_stack13 *stack) {
    (void)scope;(void)entry;(void)registers;(void)stack;return NB_UNSUPPORTED;
}
#endif
