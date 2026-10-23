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
    NB_SYSTEM_ERROR = 8
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
 * No stack arguments, variadic calls, HFA/heterogeneous/unknown aggregates,
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

typedef struct nb_signature {
    uint32_t version;                 /* exactly 1 */
    uint32_t flags;                   /* NB_SIG_REGISTER_ONLY | optional SRET */
    uint32_t stack_argument_bytes;    /* must be 0 */
    uint8_t gp_arguments, fp_arguments, gp_returns, fp_returns;
    uint8_t variadic, aggregate_arguments, unknown_layout, aggregate_returns;
} nb_signature;
enum { NB_SIG_REGISTER_ONLY = 1, NB_SIG_SRET_X8 = 2 };

typedef struct nb_import {
    nb_scope *scope;
    void *mapping;
    size_t mapping_bytes;
    void (*entry)(void);
    uint32_t bti_guarded;
} nb_import;

nb_status nb_scope_init(nb_scope *scope);
nb_status nb_scope_close(nb_scope *scope);
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

#ifdef __cplusplus
}
#endif
#endif
