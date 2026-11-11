#ifndef FFRS_HOST_MONOTONIC_COND_H
#define FFRS_HOST_MONOTONIC_COND_H
/* Host fixture. macOS has no pthread_condattr_setclock. mutex.c waits with a
 * CLOCK_MONOTONIC deadline, so this translation unit converts that deadline
 * to CLOCK_REALTIME. Android builds do not include this header. */
#include <pthread.h>
#include <time.h>
int ffrs_host_monotonic_timedwait(pthread_cond_t *cv, pthread_mutex_t *mutex, const struct timespec *abs_mono);
#define pthread_cond_timedwait ffrs_host_monotonic_timedwait
int pthread_condattr_setclock(pthread_condattr_t *attr, clockid_t clock);
#endif
