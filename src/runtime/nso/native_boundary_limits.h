/* Shared by C and preprocessed assembly. Thread capacity is independent of
 * the SDK's 32 OS-TLS key slots. Run15 needs more than 32 live guest threads. */
#ifndef FFRS_NATIVE_BOUNDARY_LIMITS_H
#define FFRS_NATIVE_BOUNDARY_LIMITS_H
#define NB_MAX_GUEST_THREADS 64
#endif
