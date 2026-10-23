#define _POSIX_C_SOURCE 200112L
#include "guest_tp.h"
#include "module_tls.h"
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

struct guest_tp_mirror {
    nso_module_tls_image *image;
    nso_module_tls_view view;
};

/* The calling thread's mirror. Thread local storage is read directly, so the
 * trap handler can resolve it without a lock or a libc call.
 * Aligned to 64 bytes to satisfy ARM64 Bionic PT_TLS minimum alignment. */
static _Thread_local _Alignas(64) guest_tp_mirror *guest_tp_thread_mirror;

/* Live mirror count, so an integrator can prove that no thread outlived its
 * mirror. */
static size_t guest_tp_live;

/* Thread exit cleanup. The key carries the same mirror pointer, so a thread
 * that never unbinds still releases its storage. */
static pthread_key_t guest_tp_cleanup_key;
static pthread_once_t guest_tp_cleanup_once = PTHREAD_ONCE_INIT;
static int guest_tp_cleanup_error;

static void guest_tp_thread_exit(void *value) {
    guest_tp_thread_mirror = NULL;
    guest_tp_mirror_free((guest_tp_mirror *)value);
}

static void guest_tp_cleanup_init(void) {
    guest_tp_cleanup_error = pthread_key_create(&guest_tp_cleanup_key, guest_tp_thread_exit);
}

struct guest_tp_result guest_tp_mirror_create(const guest_tp_template *template,
                                              guest_tp_mirror **out) {
    struct guest_tp_result result = {GUEST_TP_INVALID, 0};
    if (!out) return result;
    *out = NULL;
    if (!template || !template->alignment || (template->alignment & (template->alignment - 1)) ||
        template->initialized_size > template->total_size ||
        (template->initialized_size && !template->initialized_data)) {
        result.status = GUEST_TP_INVALID;
        return result;
    }
    guest_tp_mirror *mirror = calloc(1, sizeof(*mirror));
    if (!mirror) {
        result.status = GUEST_TP_NO_MEMORY;
        return result;
    }
    nso_module_tls_template source = {
        .initialized_data = template->initialized_data,
        .initialized_size = template->initialized_size,
        .total_size = template->total_size,
        .alignment = template->alignment,
        .reserved_header_size = template->reserved_header_size
    };
    nso_module_tls_image *image = NULL;
    int status = nso_module_tls_create(&source, &image);
    if (status) {
        free(mirror);
        result.status = (status == ENOMEM) ? GUEST_TP_NO_MEMORY : GUEST_TP_INVALID;
        result.native_error = status;
        return result;
    }
    const nso_module_tls_view *view = nso_module_tls_get_view(image);
    mirror->image = image;
    mirror->view = *view;
    __atomic_add_fetch(&guest_tp_live, 1, __ATOMIC_RELAXED);
    *out = mirror;
    result.status = GUEST_TP_OK;
    return result;
}

unsigned char *guest_tp_mirror_base(const guest_tp_mirror *mirror) {
    return mirror ? mirror->view.anchor : NULL;
}

unsigned char *guest_tp_mirror_payload(const guest_tp_mirror *mirror) {
    return mirror ? mirror->view.payload : NULL;
}

size_t guest_tp_mirror_span(const guest_tp_mirror *mirror) {
    return mirror ? mirror->view.storage_size : 0;
}

void guest_tp_mirror_free(guest_tp_mirror *mirror) {
    if (!mirror) return;
    nso_module_tls_free(mirror->image);
    free(mirror);
    __atomic_sub_fetch(&guest_tp_live, 1, __ATOMIC_RELAXED);
}

size_t guest_tp_live_mirrors(void) {
    return __atomic_load_n(&guest_tp_live, __ATOMIC_RELAXED);
}

struct guest_tp_result guest_tp_bind_current(guest_tp_mirror *mirror) {
    struct guest_tp_result result = {GUEST_TP_INVALID, 0};
    if (!mirror) {
        result.status = GUEST_TP_INVALID;
        return result;
    }
    if (guest_tp_thread_mirror) {
        result.status = GUEST_TP_BUSY;
        return result;
    }
    if (pthread_once(&guest_tp_cleanup_once, guest_tp_cleanup_init) || guest_tp_cleanup_error) {
        result.status = GUEST_TP_SYSTEM_ERROR;
        result.native_error = guest_tp_cleanup_error;
        return result;
    }
    if (pthread_setspecific(guest_tp_cleanup_key, mirror)) {
        result.status = GUEST_TP_SYSTEM_ERROR;
        result.native_error = EINVAL;
        return result;
    }
    guest_tp_thread_mirror = mirror;
    result.status = GUEST_TP_OK;
    return result;
}

struct guest_tp_result guest_tp_unbind_current(void) {
    struct guest_tp_result result = {GUEST_TP_UNBOUND, 0};
    guest_tp_mirror *mirror = guest_tp_thread_mirror;
    if (!mirror) return result;
    guest_tp_thread_mirror = NULL;
    pthread_setspecific(guest_tp_cleanup_key, NULL);
    guest_tp_mirror_free(mirror);
    result.status = GUEST_TP_OK;
    return result;
}

const unsigned char *guest_tp_current_base(void) {
    return guest_tp_thread_mirror ? guest_tp_thread_mirror->view.anchor : NULL;
}

uint32_t guest_tp_trap_encode(unsigned destination_register) {
    if (destination_register >= GUEST_TP_REGISTER_COUNT) return 0;
    return GUEST_TP_BRK_MAGIC | (destination_register << 5);
}

int guest_tp_trap_register(uint32_t instruction, unsigned *out_register) {
    if ((instruction & GUEST_TP_BRK_MASK) != GUEST_TP_BRK_MAGIC) return 0;
    if (out_register) *out_register = (instruction >> 5) & 31u;
    return 1;
}

struct guest_tp_result guest_tp_resolve_trap(uint32_t instruction,
                                             const unsigned char *base,
                                             unsigned *out_register,
                                             uint64_t *out_value) {
    struct guest_tp_result result = {GUEST_TP_INVALID, 0};
    unsigned destination = 0;
    if (!guest_tp_trap_register(instruction, &destination)) return result;
    if (out_register) *out_register = destination;
    if (!base) {
        result.status = GUEST_TP_UNBOUND;
        return result;
    }
    if (out_value) *out_value = (uint64_t)(uintptr_t)base;
    result.status = GUEST_TP_OK;
    return result;
}

/* Patched text ranges. The trap handler only resolves a fault whose PC lies
 * inside a registered range, so an unrelated BRK keeps its own behaviour. */
enum { GUEST_TP_MAX_RANGES = 8 };
typedef struct { uintptr_t low; uintptr_t high; } guest_tp_range;
static guest_tp_range guest_tp_ranges[GUEST_TP_MAX_RANGES];
static int guest_tp_range_ready[GUEST_TP_MAX_RANGES];

struct guest_tp_result guest_tp_register_text(const void *text, size_t bytes) {
    struct guest_tp_result result = {GUEST_TP_INVALID, 0};
    if (!text || !bytes) return result;
    uintptr_t low = (uintptr_t)text;
    if (bytes > UINTPTR_MAX - low) {
        result.status = GUEST_TP_INVALID;
        return result;
    }
    for (unsigned index = 0; index < GUEST_TP_MAX_RANGES; index++) {
        if (__atomic_load_n(&guest_tp_range_ready[index], __ATOMIC_ACQUIRE)) continue;
        guest_tp_ranges[index].low = low;
        guest_tp_ranges[index].high = low + bytes;
        __atomic_store_n(&guest_tp_range_ready[index], 1, __ATOMIC_RELEASE);
        result.status = GUEST_TP_OK;
        return result;
    }
    result.status = GUEST_TP_BUSY;
    return result;
}

int guest_tp_text_contains(uintptr_t address) {
    for (unsigned index = 0; index < GUEST_TP_MAX_RANGES; index++) {
        if (!__atomic_load_n(&guest_tp_range_ready[index], __ATOMIC_ACQUIRE)) continue;
        if (address >= guest_tp_ranges[index].low && address < guest_tp_ranges[index].high) return 1;
    }
    return 0;
}

struct guest_tp_result guest_tp_scan_text(const void *text, size_t bytes,
                                          struct guest_tp_patch_report *report) {
    struct guest_tp_result result = {GUEST_TP_INVALID, 0};
    if (!text || !bytes || (bytes % 4)) return result;
    if (report) memset(report, 0, sizeof(*report));
    const uint32_t *words = (const uint32_t *)text;
    size_t count = bytes / 4;
    for (size_t index = 0; index < count; index++) {
        uint32_t word = words[index];
        if (report) report->scanned_words++;
        if ((word & ~31u) == GUEST_TP_MRS_TPIDR_EL0) {
            if (report) report->registers[word & 31u]++;
        } else if ((word & ~31u) == GUEST_TP_MRS_TPIDRRO_EL0) {
            if (report) report->tpidrro_sites++;
        } else if ((word & ~31u) == GUEST_TP_MSR_TPIDR_EL0) {
            if (report) report->msr_sites++;
        }
    }
    result.status = GUEST_TP_OK;
    return result;
}

struct guest_tp_result guest_tp_patch_site(void *instruction,
                                            struct guest_tp_patch_report *report) {
    struct guest_tp_result result = {GUEST_TP_INVALID, 0};
    if (!instruction) return result;
    if (report) memset(report, 0, sizeof(*report));
    uint32_t word = *(uint32_t *)instruction;
    if ((word & ~31u) != GUEST_TP_MRS_TPIDR_EL0) {
        result.status = GUEST_TP_INVALID;
        return result;
    }
    uint32_t trap = guest_tp_trap_encode(word & 31u);
    memcpy(instruction, &trap, sizeof(trap));
    __builtin___clear_cache((char *)instruction, (char *)instruction + 4);
    if (report) {
        report->patched++;
        report->registers[word & 31u]++;
    }
    result.status = GUEST_TP_OK;
    return result;
}

struct guest_tp_result guest_tp_patch_text(void *text, size_t bytes,
                                           struct guest_tp_patch_report *report) {
    struct guest_tp_result result = {GUEST_TP_INVALID, 0};
    if (!text || !bytes || (bytes % 4)) return result;
    if (report) memset(report, 0, sizeof(*report));
    uint32_t *words = (uint32_t *)text;
    size_t count = bytes / 4;
    for (size_t index = 0; index < count; index++) {
        uint32_t word = words[index];
        if (report) report->scanned_words++;
        if ((word & ~31u) == GUEST_TP_MRS_TPIDR_EL0) {
            uint32_t trap = guest_tp_trap_encode(word & 31u);
            memcpy(&words[index], &trap, sizeof(trap));
            if (report) {
                report->patched++;
                report->registers[word & 31u]++;
            }
        } else if ((word & ~31u) == GUEST_TP_MRS_TPIDRRO_EL0) {
            if (report) report->tpidrro_sites++;
        } else if ((word & ~31u) == GUEST_TP_MSR_TPIDR_EL0) {
            if (report) report->msr_sites++;
        }
    }
    __builtin___clear_cache((char *)text, (char *)text + bytes);
    result.status = GUEST_TP_OK;
    return result;
}

/* The kernel may leave the faulting PC on the trap or advance it past the
 * trap, so both words are inspected. The decision is separated from the
 * signal handler so it can be exercised on any host. */
enum guest_tp_trap_decision guest_tp_trap_decide(int in_range, uint32_t instruction_at_pc,
                                                uint32_t instruction_before_pc,
                                                const unsigned char *base,
                                                unsigned *out_register, int *out_advance) {
    unsigned destination = 0;
    int advance = 0;
    if (!in_range) return GUEST_TP_TRAP_CHAIN;
    /* The word at the program counter is the trap that was executed. On
     * AArch64 Linux the kernel leaves the PC on the BRK instruction, so the
     * adapter has to advance it itself. The word before the PC is only
     * consulted when the PC is not on a trap word, which is the case of a
     * kernel that advanced past the trap. The word at the PC is tested first
     * on purpose: two trap words next to each other must never be resolved
     * as the earlier one, or the trap would repeat forever. */
    if (!guest_tp_trap_register(instruction_at_pc, &destination)) {
        if (!guest_tp_trap_register(instruction_before_pc, &destination)) return GUEST_TP_TRAP_CHAIN;
    } else {
        advance = 1;
    }
    if (out_register) *out_register = destination;
    if (out_advance) *out_advance = advance;
    return base ? GUEST_TP_TRAP_RESOLVE : GUEST_TP_TRAP_NO_MIRROR;
}

#if defined(__ANDROID__) && defined(__aarch64__)
#include <signal.h>
#include <ucontext.h>

static struct sigaction guest_tp_previous_action;
static int guest_tp_handler_installed;

static void guest_tp_forward(int number, siginfo_t *info, void *context) {
    if (guest_tp_previous_action.sa_flags & SA_SIGINFO) {
        if (guest_tp_previous_action.sa_sigaction) {
            guest_tp_previous_action.sa_sigaction(number, info, context);
        }
        return;
    }
    if (guest_tp_previous_action.sa_handler == SIG_IGN) return;
    if (guest_tp_previous_action.sa_handler && guest_tp_previous_action.sa_handler != SIG_DFL) {
        guest_tp_previous_action.sa_handler(number);
        return;
    }
    sigaction(number, &guest_tp_previous_action, NULL);
    raise(number);
}

static void guest_tp_trap_handler(int number, siginfo_t *info, void *context) {
    ucontext_t *uc = (ucontext_t *)context;
    if (!uc) {
        guest_tp_forward(number, info, context);
        return;
    }
    uint64_t pc = uc->uc_mcontext.pc;
    if (!guest_tp_text_contains((uintptr_t)pc)) {
        guest_tp_forward(number, info, context);
        return;
    }
    uint32_t at_pc = 0;
    uint32_t before_pc = 0;
    memcpy(&at_pc, (const void *)(uintptr_t)pc, sizeof(at_pc));
    if (pc >= 4 && guest_tp_text_contains((uintptr_t)(pc - 4))) {
        memcpy(&before_pc, (const void *)(uintptr_t)(pc - 4), sizeof(before_pc));
    }
    const unsigned char *base = guest_tp_current_base();
    unsigned destination = 0;
    int advance = 0;
    enum guest_tp_trap_decision decision = guest_tp_trap_decide(
        guest_tp_text_contains((uintptr_t)pc), at_pc, before_pc, base, &destination, &advance);
    if (decision != GUEST_TP_TRAP_RESOLVE) {
        /* Not an adapter trap, or an adapter trap without a mirror: keep the
         * fault observable instead of returning a wrong guest TP. */
        guest_tp_forward(number, info, context);
        return;
    }
    if (destination < 31) uc->uc_mcontext.regs[destination] = (uint64_t)(uintptr_t)base;
    if (advance) uc->uc_mcontext.pc = pc + 4;
}
#endif

struct guest_tp_result guest_tp_install_trap_handler(void) {
    struct guest_tp_result result = {GUEST_TP_UNSUPPORTED, 0};
#if defined(__ANDROID__) && defined(__aarch64__)
    if (guest_tp_handler_installed) {
        result.status = GUEST_TP_BUSY;
        return result;
    }
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_sigaction = guest_tp_trap_handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    if (sigaction(SIGTRAP, &action, &guest_tp_previous_action)) {
        result.status = GUEST_TP_SYSTEM_ERROR;
        result.native_error = errno;
        return result;
    }
    guest_tp_handler_installed = 1;
    result.status = GUEST_TP_OK;
#endif
    return result;
}

struct guest_tp_result guest_tp_remove_trap_handler(void) {
    struct guest_tp_result result = {GUEST_TP_UNSUPPORTED, 0};
#if defined(__ANDROID__) && defined(__aarch64__)
    if (!guest_tp_handler_installed) {
        result.status = GUEST_TP_UNBOUND;
        return result;
    }
    if (sigaction(SIGTRAP, &guest_tp_previous_action, NULL)) {
        result.status = GUEST_TP_SYSTEM_ERROR;
        result.native_error = errno;
        return result;
    }
    guest_tp_handler_installed = 0;
    result.status = GUEST_TP_OK;
#endif
    return result;
}
