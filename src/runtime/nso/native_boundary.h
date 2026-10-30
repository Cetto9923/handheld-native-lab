/* SPDX-License-Identifier: MIT */
#ifndef FFRS_NATIVE_BOUNDARY_H
#define FFRS_NATIVE_BOUNDARY_H
#include <stddef.h>
#include <stdint.h>
#include "native_boundary_limits.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef enum nb_status {
    NB_OK = 0, NB_UNSUPPORTED = 1, NB_INVALID = 2, NB_WRONG_THREAD = 3,
    NB_BUSY = 4, NB_MAP_FAILED = 5, NB_PROTECT_FAILED = 6, NB_OWNER_DEAD = 7,
    NB_SYSTEM_ERROR = 8, NB_RETAINED = 9, NB_DEPTH_LIMIT = 10
} nb_status;
enum { NB_MAX_CALLBACK_DEPTH = 4 };

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
    /* Former reserved words; fixed size/offsets retained. */
    uint64_t active_guest_x18, callback_depth;
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

typedef struct nb_thread_slot {
    uint64_t tpidr;             /* offset 0 */
    uint64_t owner_tp;          /* offset 8 */
    uint64_t host_x18;          /* offset 16 */
    uint64_t phase;             /* offset 24: 0=IDLE, 1=GUEST, 2=HOST */
    uint64_t callback_depth;    /* offset 32 */
    nb_scope *scope;            /* offset 40 */
    uint64_t active_guest_x18;  /* offset 48 */
    uint64_t reserved;          /* offset 56 */
} nb_thread_slot;

typedef struct nb_signature {
    uint32_t version;                 /* 1 register-only; 2 fixed GP21; 3 variadic snprintf; 4 variadic stdio */
    uint32_t flags;                   /* v2: NB_SIG_FIXED_GP21; v3: NB_SIG_VARIADIC_SNPRINTF; v4: NB_SIG_VARIADIC_STDIO */
    uint32_t stack_argument_bytes;    /* v1: 0; v2: exactly 104; v3: 0; v4: 0 */
    uint8_t gp_arguments, fp_arguments, gp_returns, fp_returns;
    uint8_t variadic, aggregate_arguments, unknown_layout, aggregate_returns;
} nb_signature;
enum { NB_SIG_REGISTER_ONLY = 1, NB_SIG_SRET_X8 = 2, NB_SIG_FIXED_GP21 = 4, NB_SIG_VARIADIC_SNPRINTF = 8, NB_SIG_VARIADIC_STDIO = 16 };

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

/* Synchronous owned HOST->GUEST callback on this same active scope only.
 * Must be called by a normal host function currently reached through its import
 * gateway (phase HOST). Plain nb_call remains NB_BUSY for same-scope reentry.
 * Assembly captures the CURRENT host SCS depth before C; it temporarily replaces
 * scope.host_x18 so callback guest code may use this scope's existing imports.
 * Initial guest x18 is the gateway's actual active_guest_x18, NOT the packet's
 * input guest_x18. Output packet captures actual guest registers/x18 on return.
 * All outer scope fields (HOST phase, host_x18, active_guest_x18, depth) and host
 * x18/callee-saved/SP/LR are restored. At most NB_MAX_CALLBACK_DEPTH callbacks
 * may be active. NB_DEPTH_LIMIT, wrong phase/owner, invalid/dead scope refusals
 * leave packet/scope untouched. This is register-only (same x0-x8/q0-q7 raw
 * contract as nb_call), with no new stack/variadic/aggregate declaration.
 * Target/packet/scope lifetimes must span the normal return. Foreign unwind,
 * longjmp and asynchronous signal adaptation remain unsupported; abandoned
 * active scopes are never reset as if they completed.
 */
nb_status nb_call_from_host(nb_scope *scope, void (*guest_entry)(void), nb_registers *registers);

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

/* Execute guest entry on an authorized child thread of scope on guest_sp.
 * Captures host x18, registers thread in slot table, restores host SP/x18 on return. */
nb_status nb_call_child(nb_scope *scope, void (*guest_entry)(void),
                        nb_registers *registers, uintptr_t guest_sp);
nb_thread_slot *nb_register_current_thread(nb_scope *scope, uint64_t host_x18);
void nb_unregister_current_thread(void);

#ifdef __cplusplus
}
#endif
#endif
