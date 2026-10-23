/*
 * nso_loader — native loader for Nintendo Switch NSO0 modules (AArch64).
 *
 * Goal: make an unmodified Switch game module runnable as native AArch64
 * code on a non-Switch host (macOS arm64 now, Android arm64 later), by
 * providing the platform surface the module expects. This is the reference
 * executor used to obtain behaviour oracles for reconstructed code; it is
 * not, and must not become, a general-purpose CPU interpreter or emulator.
 *
 * What it does, in order:
 *   1. parse the NSO0 header and the dynamic string/symbol tables;
 *   2. locate MOD0 block and parse the ELF .dynamic section;
 *   3. map one contiguous module image (text/rodata/rwdata/bss) in memory;
 *   4. copy the segments to their module-relative offsets;
 *   5. apply compact relative relocations (DT_RELR);
 *   6. apply symbolic relocations (DT_RELA and DT_JMPREL);
 *   7. flip page protections so .text is execute-only and the rest stays read/write.
 *
 * Nothing here interprets or rewrites instructions: the module's own AArch64
 * code executes directly on the host CPU.
 */
#ifndef NSO_LOADER_H
#define NSO_LOADER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nso_module nso_module;

/* Return a correctly typed function/object address, or zero if unsupported.
 * The loader never invokes target functions through this resolver signature.
 * Unknown imports remain identifiable fault sentinels; they never return zero.
 */
typedef uint64_t (*nso_import_fn)(uint32_t symbol_index, const char *symbol_name);

/*
 * Load an unpacked NSO0 image from disk.
 * Only uncompressed segments are accepted. A NULL resolver leaves all imports
 * unresolved. Calling/reading one produces a named fatal diagnostic.
 */
nso_module *nso_load_file(const char *path, nso_import_fn import_fn, char *errbuf, size_t errlen);

/* Release everything owned by the module. */
void nso_free(nso_module *m);

/* Base address the module image is mapped at. */
uint64_t nso_base(const nso_module *m);

/* Address of a module-relative offset (e.g. an exported function). */
uint64_t nso_addr(const nso_module *m, uint64_t module_offset);

/* Name of the symbol with the given dynsym index, or NULL. */
const char *nso_symbol_name(const nso_module *m, uint32_t index);

/* Module-relative address of a defined symbol, or UINT64_MAX if undefined. */
uint64_t nso_symbol_value(const nso_module *m, uint32_t index);

/* Index of the first dynsym entry whose name matches, or 0 if not found. */
uint32_t nso_find_symbol(const nso_module *m, const char *name);

/* Number of imported (undefined) symbols. */
uint32_t nso_import_count(const nso_module *m);

/* Number of RELA relocation entries applied. */
uint64_t nso_reloc_count(const nso_module *m);

/* Number of DT_RELR relative relocations applied. */
uint64_t nso_relr_count(const nso_module *m);

/* Bound import statistics exclude ELF's mandatory null symbol. */
uint32_t nso_unresolved_count(const nso_module *m);
size_t nso_image_size(const nso_module *m);

/* Build id as 32 hex characters (big-endian display order). */
void nso_build_id(const nso_module *m, char out[33]);

/*
 * Call a function at a module-relative offset with up to six integer
 * arguments. Returns the value in x0.
 */
uint64_t nso_call(nso_module *m, uint64_t module_offset,
                  uint64_t a0, uint64_t a1, uint64_t a2,
                  uint64_t a3, uint64_t a4, uint64_t a5);

/* Run .init and .init_array static constructors. */
void nso_run_init(nso_module *m);

/* Install a fault handler that reports faulting PCs as module offsets. */
void nso_install_fault_trap(nso_module *m);

/* Enable the SIGILL trap that intercepts `svc` instructions. */
void nso_enable_svc_trap(int verbose);

#ifdef __cplusplus
}
#endif

#endif /* NSO_LOADER_H */
