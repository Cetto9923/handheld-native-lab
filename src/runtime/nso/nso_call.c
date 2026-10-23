/*
 * nso_call — invoke a function inside a loaded Switch NSO module natively.
 *
 * The module's own AArch64 code runs directly on the host CPU; this tool only
 * provides the platform surface the module expects (see nso_loader.c).
 *
 * Usage:
 *   nso_call <nso> --info
 *   nso_call <nso> --call <module_offset> [arg0 ... arg5]
 *   nso_call <nso> --symbol <name>        (call an exported symbol)
 */
#include "nso_loader.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t g_trap_calls;

static uint64_t import_trap(uint32_t idx, const char *name,
                            uint64_t a0, uint64_t a1, uint64_t a2,
                            uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    g_trap_calls++;
    fprintf(stderr, "[import-trap] sym#%u %s\n", idx, name ? name : "?");
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <nso> --info|--call <off> [args]|--symbol <name>\n", argv[0]);
        return 2;
    }
    const char *path = argv[1];
    char err[256] = {0};
    nso_module *m = nso_load_file(path, import_trap, err, sizeof(err));
    if (!m) {
        fprintf(stderr, "load failed: %s\n", err);
        return 1;
    }

    char bid[33];
    nso_build_id(m, bid);
    printf("base          : 0x%" PRIx64 "\n", nso_base(m));
    printf("build id      : %s\n", bid);
    printf("imports       : %u\n", nso_import_count(m));
    printf("relocations   : %" PRIu64 "\n", nso_reloc_count(m));
    fflush(stdout);

    int rc = 0;
    if (argc >= 3 && !strcmp(argv[2], "--call")) {
        if (argc < 4) { fprintf(stderr, "--call needs an offset\n"); return 2; }
        uint64_t off = strtoull(argv[3], NULL, 0);
        uint64_t a[6] = {0};
        for (int i = 0; i < 6 && 4 + i < argc; i++) a[i] = strtoull(argv[4 + i], NULL, 0);
        uint64_t r = nso_call(m, off, a[0], a[1], a[2], a[3], a[4], a[5]);
        printf("result        : 0x%" PRIx64 "\n", r);
    } else if (argc >= 3 && !strcmp(argv[2], "--symbol")) {
        if (argc < 4) { fprintf(stderr, "--symbol needs a name\n"); return 2; }
        uint32_t n = nso_find_symbol(m, argv[3]);
        if (!n) { fprintf(stderr, "symbol not found: %s\n", argv[3]); rc = 1; }
        else {
            uint64_t off = nso_symbol_value(m, n);
            printf("symbol index  : %u\n", n);
            printf("module offset : 0x%" PRIx64 "\n", off);
            if (off == UINT64_MAX) {
                fprintf(stderr, "symbol is an import, not callable here\n");
                rc = 1;
            } else {
                uint64_t a[6] = {0};
                for (int i = 0; i < 6 && 4 + i < argc; i++) a[i] = strtoull(argv[4 + i], NULL, 0);
                uint64_t r = nso_call(m, off, a[0], a[1], a[2], a[3], a[4], a[5]);
                printf("result        : 0x%" PRIx64 "\n", r);
            }
        }
    }
    printf("trap calls    : %" PRIu64 "\n", g_trap_calls);
    fflush(stdout);
    nso_free(m);
    return rc;
}
