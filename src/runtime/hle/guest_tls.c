#include "guest_tls.h"
#include <stdio.h>
#include <stdlib.h>
static _Thread_local nso_guest_tls guest_storage;
nso_guest_tls *nso_guest_tls_current(void) {return &guest_storage;}
static void check_slot(unsigned slot) {
    if(slot>=NSO_GUEST_TLS_SLOTS) {
        fprintf(stderr,"[guest-tls] invalid storage slot %u\n",slot);
        abort();
    }
}
uintptr_t nso_guest_tls_get(unsigned slot) {check_slot(slot);return guest_storage.values[slot];}
void nso_guest_tls_set(unsigned slot,uintptr_t value) {check_slot(slot);guest_storage.values[slot]=value;}
