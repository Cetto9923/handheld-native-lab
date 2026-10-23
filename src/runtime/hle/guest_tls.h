#ifndef NSO_GUEST_TLS_H
#define NSO_GUEST_TLS_H
#include <stdint.h>
/* Independent host-owned guest storage PoC. This is not a Switch ThreadType or
 * ELF TLS layout. No SDK AllocateTlsSlot/GetTlsValue/SetTlsValue is resolved to
 * it until the caller's inline TLS accesses have a verified adaptation. */
enum { NSO_GUEST_TLS_SLOTS=16 };
typedef struct nso_guest_tls {uintptr_t values[NSO_GUEST_TLS_SLOTS];} nso_guest_tls;
nso_guest_tls *nso_guest_tls_current(void);
uintptr_t nso_guest_tls_get(unsigned slot);
void nso_guest_tls_set(unsigned slot,uintptr_t value);
#endif
