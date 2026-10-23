/* SPDX-License-Identifier: MIT */
#ifndef FFRS_NATIVE_BOUNDARY_H
#define FFRS_NATIVE_BOUNDARY_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef enum nb_status {
    NB_OK = 0, NB_UNSUPPORTED = 1, NB_INVALID = 2, NB_WRONG_THREAD = 3,
    NB_BUSY = 4, NB_MAP_FAILED = 5, NB_PROTECT_FAILED = 6, NB_OWNER_DEAD = 7,
    NB_SYSTEM_ERROR = 8, NB_RETAINED = 9
} nb_status;

/* Caller-owned, zero-init, no copying. All control APIs require normal host x18.
 * Android AArch64 only. TPIDR_EL0 is READ ONLY and used as an opaque owner token;
 * no Bionic TCB layout, TP replacement, global host-x18, or TLS lookup is used.
 * Keep scope alive through all calls and bindings; close refuses live bindings.
 * Caller synchronizes publication/control/destruction; no concurrent mutation.
 * Internal fields are public only for the fixed assembly ABI; do not modify.
 */
typedef struct nb_scope {
    uint64_t magic, owner_tp, host_x18, phase;
    uint64_t binding_count;
    void *lifetime;
    uint64_t reserved[2];
} nb_scope;

/* Scalar/pointer/vector register ABI packet. x8 carries an explicitly declared
 * indirect-result pointer. q0-q7 are raw 128-bit register contents, not C doubles.
 * nb_call is register-only; nb_call_stack13 adds exactly thirteen u64 words.
 * No variadic calls, HFA/heterogeneous/unknown aggregates,
 * guest unwind, foreign exceptions, longjmp, or asynchronous signal adaptation.
 */
typedef struct nb_registers {
    uint64_t x[9], guest_x18;
#ifdef __cplusplus
    alignas(16) unsigned char q[8][16];
#else
    _Alignas(16) unsigned char q[8][16];
#endif
} nb_registers;

typedef struct nb_stack13 { uint64_t words[13]; } nb_stack13;

typedef struct nb_signature {
    uint32_t version;                 /* 1 register-only; 2 fixed GP21 */
    uint32_t flags;                   /* v2: exactly NB_SIG_FIXED_GP21 */
    uint32_t stack_argument_bytes;    /* v1: 0; v2: exactly 104 */
    uint8_t gp_arguments, fp_arguments, gp_returns, fp_returns;
    uint8_t variadic, aggregate_arguments, unknown_layout, aggregate_returns;
} nb_signature;
enum { NB_SIG_REGISTER_ONLY = 1, NB_SIG_SRET_X8 = 2, NB_SIG_FIXED_GP21 = 4 };

typedef struct nb_import {
    nb_scope *scope;
    void *mapping;
    size_t mapping_bytes;
    void (*entry)(void);
    uint32_t bti_guarded;
} nb_import;

nb_status nb_scope_init(nb_scope *scope);
nb_status nb_scope_close(nb_scope *scope);
/* Bind returns NB_PROTECT_FAILED only if a failed RX transition was rolled
 * back by a successful munmap. NB_RETAINED means that rollback also failed:
 * binding owns mapping/scope/count but entry is NULL and MUST NOT be published.
 * Keep both owner objects alive; retry nb_import_unbind. Unbind accepts this
 * non-callable owner, and on NB_MAP_FAILED retains it unchanged. Only an actual
 * successful munmap decrements binding_count and clears the binding.
 */
nb_status nb_import_bind(nb_import *binding, nb_scope *scope,
                        void (*typed_host_target)(void), const nb_signature *signature);
nb_status nb_import_unbind(nb_import *binding);
void (*nb_import_entry(const nb_import *binding))(void);

/* Actual assembly entry captures host x18 before any C/SCS prologue. Loads and
 * returns x0-x8/q0-q7 plus guest_x18; preserves host x19-x29/q8-q15, SP and LR.
 * A refcounted per-thread lifetime token is invalidated by a pthread destructor;
 * TP reuse after thread exit cannot reactivate old bindings. A dead owner's idle
 * bindings/scope may be unbound/closed by a normal host thread. Non-idle dead
 * owners are retained, never reset as if normal completion had occurred.
 * Same-scope reentry returns NB_BUSY unchanged. A different scope may nest from
 * a normal typed-host callback and captures its own CURRENT host SCS depth.
 * An import thunk outside its GUEST phase or on another thread traps in assembly
 * before restoring x18/calling C. Never call an unbound/stale executable address.
 * Entry/signatures must be known register-only ABI; this does not classify code.
 */
nb_status nb_call(nb_scope *scope, void (*guest_entry)(void), nb_registers *registers);

/* Explicit machine ABI, not signature inference: x0-x7 plus exactly 13 scalar/
 * pointer-sized 64-bit stack words at callee SP+0 through SP+96, u64 result x0.
 * Bind version=2/flags=NB_SIG_FIXED_GP21/gp_arguments=8/fp_arguments=0/
 * gp_returns=1/fp_returns=0/stack_argument_bytes=104; all layout exclusions zero.
 * x8/q0-q7 are transported without adapter clobber, but v2 does not declare
 * sret or FP arguments/results. Padding to 112 bytes is not a fourteenth arg.
 * stack must name a live aligned nb_stack13, separate from scope/registers.
 * Guest/host targets must return normally with SP restored. Stack access faults
 * are outside the recoverable status domain; no foreign unwind/signal adaptation.
 * Existing scope/thread/lifetime/RETAINED semantics apply unchanged.
 */
nb_status nb_call_stack13(nb_scope *scope, void (*guest_entry)(void),
                          nb_registers *registers, const nb_stack13 *stack);

#ifdef __cplusplus
}
#endif
#endif
