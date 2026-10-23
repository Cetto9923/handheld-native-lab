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
NVNgenericFuncPtrFunc nvnBootstrapLoader(const char *name);

/*
 * Initialize NVN dispatch table with custom/Vulkan implementation callbacks.
 */
void nvn_dispatch_init(void);

#ifdef __cplusplus
}
#endif

#endif /* NVN_DISPATCH_H */
