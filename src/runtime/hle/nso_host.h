#ifndef NSO_HOST_H
#define NSO_HOST_H
#include "../nso/nso_loader.h"
/* Optional exact-name extension, e.g. a typed NVN bootstrap resolver.
 * Return zero for unsupported names. No graphics headers or generic stub ABI. */
void nso_host_set_extension_resolver(nso_import_fn resolver);
uint64_t nso_host_resolve(uint32_t index,const char *name);
#endif
