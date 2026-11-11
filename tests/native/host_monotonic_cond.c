#include <pthread.h>
#include <stdint.h>
#include <time.h>
int pthread_condattr_setclock(pthread_condattr_t *attr, clockid_t clock) {
    (void)attr; (void)clock; return 0;
}
int ffrs_host_monotonic_timedwait(pthread_cond_t *cv, pthread_mutex_t *mutex, const struct timespec *abs_mono) {
    struct timespec mono, real, deadline;
    int64_t delta, nsec;
    if (!abs_mono || clock_gettime(CLOCK_MONOTONIC, &mono) || clock_gettime(CLOCK_REALTIME, &real))
        return pthread_cond_timedwait(cv, mutex, abs_mono);
    delta = ((int64_t)abs_mono->tv_sec - (int64_t)mono.tv_sec) * 1000000000LL
          + ((int64_t)abs_mono->tv_nsec - (int64_t)mono.tv_nsec);
    deadline = real;
    nsec = (int64_t)deadline.tv_nsec + delta;
    deadline.tv_sec += nsec / 1000000000LL;
    nsec %= 1000000000LL;
    if (nsec < 0) { deadline.tv_sec -= 1; nsec += 1000000000LL; }
    deadline.tv_nsec = (long)nsec;
    return pthread_cond_timedwait(cv, mutex, &deadline);
}
