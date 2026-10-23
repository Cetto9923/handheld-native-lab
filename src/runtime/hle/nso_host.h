#ifndef NSO_HOST_H
#define NSO_HOST_H
#include "../nso/nso_loader.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Optional exact-name extension, e.g. a typed NVN bootstrap resolver.
 * Return zero for unsupported names. No graphics headers or generic stub ABI. */
void nso_host_set_extension_resolver(nso_import_fn resolver);
/* Android builds strongly reference the linked compiler C++ ABI guard runtime.
 * Link libc++abi (plus its platform dependencies); no dlsym export is required.
 * Other hosts can exercise the same path with NSO_WITH_CXX_GUARDS=1. */
uint64_t nso_host_resolve(uint32_t index,const char *name);
#ifdef __cplusplus
}
#endif
#endif
