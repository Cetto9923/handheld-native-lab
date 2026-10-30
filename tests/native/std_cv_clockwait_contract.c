/* The timed and untimed libc++ waits must use the same host mutex/CV as
 * lock/unlock/notify. A guest object is an opaque key, not a Bionic object. */
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

void nso_host_std_mutex_lock(void *);
void nso_host_std_mutex_unlock(void *);
void nso_host_std_cv_notify_one(void *);
int nso_host_pthread_cond_clockwait(pthread_cond_t *, pthread_mutex_t *, clockid_t,
                                   const struct timespec *);

static _Alignas(16) unsigned char guest_mutex[64];
static _Alignas(16) unsigned char guest_cv[64];
static atomic_int entered;
static int ready, observed, wait_result;

static struct timespec deadline_ms(clockid_t clock, long milliseconds) {
    struct timespec t;
    clock_gettime(clock, &t);
    t.tv_nsec += milliseconds * 1000000;
    t.tv_sec += t.tv_nsec / 1000000000;
    t.tv_nsec %= 1000000000;
    return t;
}

static void *waiter(void *unused) {
    (void)unused;
    nso_host_std_mutex_lock(guest_mutex);
    atomic_store(&entered, 1);
    struct timespec deadline = deadline_ms(CLOCK_MONOTONIC, 1000);
    int result = 0;
    while (!ready && !result)
        result = nso_host_pthread_cond_clockwait((pthread_cond_t *)guest_cv,
                                                (pthread_mutex_t *)guest_mutex,
                                                CLOCK_MONOTONIC, &deadline);
    wait_result = result;
    observed = ready;
    nso_host_std_mutex_unlock(guest_mutex);
    return NULL;
}

int main(void) {
    /* A failed bridge can deadlock the producer: keep the regression bounded. */
    alarm(4);
    pthread_t worker;
    if (pthread_create(&worker, NULL, waiter, NULL)) return 2;
    while (!atomic_load(&entered)) usleep(1000);
    nso_host_std_mutex_lock(guest_mutex);
    ready = 37;
    nso_host_std_cv_notify_one(guest_cv);
    nso_host_std_mutex_unlock(guest_mutex);
    pthread_join(worker, NULL);
    int errors = wait_result != 0 || observed != 37;
    nso_host_std_mutex_lock(guest_mutex);
    struct timespec deadline = deadline_ms(CLOCK_MONOTONIC, 25);
    int result = nso_host_pthread_cond_clockwait((pthread_cond_t *)guest_cv,
                                                (pthread_mutex_t *)guest_mutex,
                                                CLOCK_MONOTONIC, &deadline);
    errors += result != ETIMEDOUT;
    nso_host_std_mutex_unlock(guest_mutex);
    /* Neither operation may overwrite the guest's opaque objects. */
    for (unsigned i = 0; i < sizeof(guest_mutex); ++i) errors += guest_mutex[i] != 0;
    for (unsigned i = 0; i < sizeof(guest_cv); ++i) errors += guest_cv[i] != 0;
    alarm(0);
    printf("{\"std_cv_clockwait_errors\":%d,\"notified_value\":%d,\"wait_result\":%d,\"timeout_result\":%d}\n",
           errors, observed, wait_result, result);
    return errors ? 1 : 0;
}
