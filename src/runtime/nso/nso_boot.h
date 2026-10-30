#ifndef NSO_BOOT_H
#define NSO_BOOT_H

#include "nso_loader.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The owned guest startup profile.
 *
 * This is the exact boot sequence the official runtime uses
 * (DT_INIT phase 1 -> nninitStartup -> DT_INIT phase 2 -> nnMain) together
 * with the bounded resource policy of this runtime. It lives in its own
 * translation unit so that the standalone CLI and the in-process library
 * run the same guest code instead of two drifting copies.
 *
 * The entry struct keeps the modes the CLI needs (info / call / symbol /
 * allocator diagnostic) next to the boot mode, so one loader path serves
 * all of them. */
struct nso_boot_entry {
    int info;
    int init;
    int by_symbol;
    int rc;
    int allocator_diagnostic;
    int boot;
    unsigned argument_count;
    nso_init_policy policy;
    const char *target;
    uint64_t off;
    uint64_t args[6];
};

/* Runs the owned guest profile and returns the process exit code:
 * 0 when the guest finished the requested step, 1 on any failure.
 * The guest keeps its own fatal-error policy: a guest fatal error ends the
 * run where the game declared it, it is never swallowed here. */
int nso_owned_profile(const char *path, size_t quota, size_t stack, size_t resources,
                      size_t guest_argc, const char *const guest_argv[],
                      struct nso_boot_entry *entry);

/* The in-process entry point used by the Android library.
 *
 * It runs the complete official boot sequence for the guest image at
 * `path`. The caller owns the thread: the guest must not run on the UI
 * thread, because it blocks for the whole game session. */
typedef struct nso_boot_options {
    const char *path;
    size_t quota;
    size_t stack;
    size_t resources;
    size_t guest_argc;
    const char *const *guest_argv;
} nso_boot_options;

int nso_boot_run(const nso_boot_options *options);

#ifdef __cplusplus
}
#endif
#endif /* NSO_BOOT_H */
