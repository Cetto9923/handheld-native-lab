#ifndef NSO_GUEST_TP_H
#define NSO_GUEST_TP_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* AArch64 encodings this adapter reads or writes.
 * GUEST_TP_MRS_TPIDR_EL0 carries its destination register in bits 4:0.
 * GUEST_TP_BRK_MAGIC is BRK #0x2a00 with the same register in bits 9:5;
 * the 0x2a00 immediate marks a trap that belongs to this adapter. The
 * immediate occupies bits 20:5, so the magic needs the mask below. */
#define GUEST_TP_MRS_TPIDR_EL0 0xd53bd040u
#define GUEST_TP_MRS_TPIDRRO_EL0 0xd53bd060u
#define GUEST_TP_MSR_TPIDR_EL0 0xd51bd040u
#define GUEST_TP_BRK_MAGIC 0xd4254000u
#define GUEST_TP_BRK_MASK 0xfffffc00u

enum { GUEST_TP_REGISTER_COUNT = 32 };

enum guest_tp_status {
    GUEST_TP_OK = 0,
    GUEST_TP_INVALID = 1,
    GUEST_TP_NO_MEMORY = 2,
    GUEST_TP_BUSY = 3,
    GUEST_TP_UNBOUND = 4,
    GUEST_TP_UNSUPPORTED = 5,
    GUEST_TP_SYSTEM_ERROR = 6
};
struct guest_tp_result { enum guest_tp_status status; int native_error; };

/* A static guest TLS template, already relocated into the loaded module.
 * The reviewed main module uses initialized_size 0x220, total_size 0x1019,
 * alignment 8 and reserved_header_size 0x10. The initialized image is copied
 * once per mirror; the remaining total_size bytes are zero filled and are NOT
 * copied from the module, because the linker total extent is ordinary module
 * data. The reserved header is zero filled and models no TCB field. */
typedef struct guest_tp_template {
    const void *initialized_data;
    size_t initialized_size;
    size_t total_size;
    size_t alignment;
    size_t reserved_header_size;
} guest_tp_template;

typedef struct guest_tp_mirror guest_tp_mirror;

/* Counts of one call. Every function that fills a report clears it first, so
 * the counts always describe that call only. tpidrro_sites counts MRS
 * TPIDRRO_EL0 reads, which this adapter never rewrites; msr_sites counts MSR
 * TPIDR_EL0 writes, which this adapter never performs. */
struct guest_tp_patch_report {
    size_t scanned_words;
    size_t patched;
    size_t tpidrro_sites;
    size_t msr_sites;
    size_t registers[GUEST_TP_REGISTER_COUNT];
};

/* Create one thread's mirror of the template. The template is copied
 * synchronously and is not retained. On success *out owns the mirror; on
 * failure *out is NULL and the caller keeps every buffer it passed in. */
struct guest_tp_result guest_tp_mirror_create(const guest_tp_template *template,
                                              guest_tp_mirror **out);

/* The value the guest must observe in TPIDR_EL0, and the first byte of the
 * guest TLS block that follows the 0x10 reserved header. Both stay valid
 * until the mirror is freed. The guest writes into this block, so both
 * pointers are mutable. */
unsigned char *guest_tp_mirror_base(const guest_tp_mirror *mirror);
unsigned char *guest_tp_mirror_payload(const guest_tp_mirror *mirror);
/* Span from base to the end of the allocation, header included. */
size_t guest_tp_mirror_span(const guest_tp_mirror *mirror);

/* Free the mirror storage. The owning thread must have unbound it first. */
void guest_tp_mirror_free(guest_tp_mirror *mirror);

/* Mirrors this process currently owns. A thread that exits while holding a
 * mirror releases it, so an integrator can check that no mirror is left. */
size_t guest_tp_live_mirrors(void);

/* Bind one mirror to the calling thread. On success the thread owns the
 * mirror and frees it when the thread exits; on failure the caller keeps
 * ownership. A thread may hold one mirror at a time. */
struct guest_tp_result guest_tp_bind_current(guest_tp_mirror *mirror);
/* Release the calling thread's mirror. */
struct guest_tp_result guest_tp_unbind_current(void);
/* The calling thread's guest TP, or NULL when the thread holds no mirror.
 * Safe to call from a signal handler: it reads thread local storage only. */
const unsigned char *guest_tp_current_base(void);

/* Read-only census of a text range. text need not be writable. */
struct guest_tp_result guest_tp_scan_text(const void *text, size_t bytes,
                                          struct guest_tp_patch_report *report);

/* Rewrite every MRS Xt, TPIDR_EL0 in text into the adapter trap. text must
 * be writable and stay writable until the caller clears the instruction
 * cache. A range that also holds data words equal to that encoding may be
 * rewritten by mistake, so prefer guest_tp_patch_site for reviewed sites.
 * Patching twice is a no-op: a rewritten word is no longer an MRS. */
struct guest_tp_result guest_tp_patch_text(void *text, size_t bytes,
                                           struct guest_tp_patch_report *report);

/* Rewrite one reviewed instruction word. Returns GUEST_TP_INVALID when the
 * word is not an MRS TPIDR_EL0, so an approved site list cannot silently
 * rewrite something else. */
struct guest_tp_result guest_tp_patch_site(void *instruction,
                                            struct guest_tp_patch_report *report);

/* Record a patched text range. The trap handler only resolves faults inside
 * a registered range, so an unrelated BRK in the host or the guest keeps its
 * own behaviour. Register the range before the text becomes executable. */
struct guest_tp_result guest_tp_register_text(const void *text, size_t bytes);
/* Whether an address lies inside a registered patched range. */
int guest_tp_text_contains(uintptr_t address);

/* Install or remove the SIGTRAP handler that turns an adapter trap into the
 * calling thread's guest TP. Android AArch64 only; other targets report
 * GUEST_TP_UNSUPPORTED. The handler chains unknown traps to the previous
 * handler. */
struct guest_tp_result guest_tp_install_trap_handler(void);
struct guest_tp_result guest_tp_remove_trap_handler(void);

/* Pure helpers, usable on any host. */
uint32_t guest_tp_trap_encode(unsigned destination_register);
int guest_tp_trap_register(uint32_t instruction, unsigned *out_register);
/* Resolve the value one adapter trap must produce. base is the guest TP the
 * calling thread owns, or NULL when the thread holds no mirror. */
struct guest_tp_result guest_tp_resolve_trap(uint32_t instruction,
                                             const unsigned char *base,
                                             unsigned *out_register,
                                             uint64_t *out_value);

/* What a trap must do. CHAIN keeps the previous handler, RESOLVE writes the
 * guest TP and resumes, NO_MIRROR reports an adapter trap on a thread that
 * holds no mirror. */
enum guest_tp_trap_decision {
    GUEST_TP_TRAP_CHAIN = 0,
    GUEST_TP_TRAP_RESOLVE = 1,
    GUEST_TP_TRAP_NO_MIRROR = 2
};
/* The kernel may leave the faulting PC on the trap or advance it past the
 * trap, so both words are inspected. out_advance reports whether the caller
 * must step the PC past the trap. */
enum guest_tp_trap_decision guest_tp_trap_decide(int in_range, uint32_t instruction_at_pc,
                                                uint32_t instruction_before_pc,
                                                const unsigned char *base,
                                                unsigned *out_register, int *out_advance);

#ifdef __cplusplus
}
#endif
#endif
