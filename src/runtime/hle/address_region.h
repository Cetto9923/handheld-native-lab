#ifndef NSO_ADDRESS_REGION_H
#define NSO_ADDRESS_REGION_H
#include <stddef.h>
#include <stdint.h>
/* Observed SDK Result ABI: x0=output pointer, x1=size, w0=result.
 * Reserve virtual addresses only. No memory block/physical commit semantics.
 * Nonzero, 4-KiB-multiple sizes are the supported contract. */
uint32_t nso_host_allocate_address_region(uintptr_t *out,size_t size);
uint32_t nso_host_free_address_region(uintptr_t address);
#endif
