#ifndef NVN_DISPATCH_H
#define NVN_DISPATCH_H

#include "nvn/nvn.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * nvnBootstrapLoader - Core entry point imported dynamically by main.bin.
 * Resolves NVN C API function pointers by function name string.
 */
PFNNVNGENERICFUNCPTRPROC nvnBootstrapLoader(const char *name);

typedef struct {
    const char *name;
    PFNNVNGENERICFUNCPTRPROC function;
} nvn_dispatch_entry;
typedef struct {
    size_t registered;
    uint64_t lookups;
    uint64_t missing;
} nvn_dispatch_stats;
/* Atomic batch registration; duplicate/reserved names are rejected. Names are
 * copied. Backends own the correct function ABI. No backend is installed by
 * default; unsupported APIs return NULL. Returns errno values, 0 on success. */
int nvn_dispatch_register(const nvn_dispatch_entry *entries, size_t count);
typedef void (*nvn_missing_symbol_fn)(const char *name, void *user);
void nvn_dispatch_set_missing_callback(nvn_missing_symbol_fn callback, void *user);
void nvn_dispatch_get_stats(nvn_dispatch_stats *stats);
const char *nvn_dispatch_last_missing(void);

/*
 * Initialize NVN dispatch table with custom/Vulkan implementation callbacks.
 */
void nvn_dispatch_init(void);

#ifdef __cplusplus
}
#endif

#endif /* NVN_DISPATCH_H */
