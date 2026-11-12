/* FinalizeEvent must not free an entry that a WaitEvent thread is still
 * using. Host-only: no guest, no device. */
#include "../../src/runtime/hle/nso_host.h"
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int errors;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); errors++; } } while (0)

struct event_object { unsigned char bytes[64]; };

static void *wait_until_finalize(void *event) {
    nso_host_os_wait_event(event);
    return NULL;
}

static void init_event(struct event_object *event, int signaled, int clear_mode) {
    memset(event, 0, sizeof *event);
    nso_host_os_initialize_event(event, signaled, clear_mode);
}

int main(void) {
    alarm(20);
    struct event_object event;
    init_event(&event, 1, 0);
    CHECK(nso_host_os_try_wait_event(&event));
    nso_host_os_finalize_event(&event);

    init_event(&event, 0, 1);
    pthread_t waiter;
    CHECK(pthread_create(&waiter, NULL, wait_until_finalize, &event) == 0);
    usleep(50 * 1000);
    nso_host_os_finalize_event(&event);
    CHECK(pthread_join(waiter, NULL) == 0);

    pthread_t waiters[4];
    init_event(&event, 0, 0);
    for (int i = 0; i < 4; i++) CHECK(pthread_create(&waiters[i], NULL, wait_until_finalize, &event) == 0);
    usleep(50 * 1000);
    nso_host_os_finalize_event(&event);
    for (int i = 0; i < 4; i++) CHECK(pthread_join(waiters[i], NULL) == 0);

    /* The same guest object can be initialized again after finalize. */
    init_event(&event, 1, 1);
    CHECK(nso_host_os_try_wait_event(&event));
    CHECK(!nso_host_os_try_wait_event(&event));
    nso_host_os_finalize_event(&event);

    for (int round = 0; round < 40; round++) {
        init_event(&event, 0, 0);
        CHECK(pthread_create(&waiter, NULL, wait_until_finalize, &event) == 0);
        usleep(2 * 1000);
        nso_host_os_finalize_event(&event);
        CHECK(pthread_join(waiter, NULL) == 0);
    }

    if (errors) return 1;
    puts("event finalize contract passed");
    return 0;
}
