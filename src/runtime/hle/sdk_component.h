#ifndef FFRS_SDK_COMPONENT_H
#define FFRS_SDK_COMPONENT_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Exact supplied SDK only; no SDK startup/CRT or platform-service execution.
 * The public surface is restricted to VibrationConverter's pure DSP calls. */
int ffrs_sdk_component_load(const char *path);
uint64_t ffrs_sdk_component_lookup(const char *name, unsigned *arguments, unsigned *returns);
#ifdef __cplusplus
}
#endif
#endif
