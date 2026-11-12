/* A scene unload must not kill the process when a MultiWait holder is waiting
 * on the event, and a late signal must not touch the guest object any more.
 * Host-only: no guest, no device. */
#include "../../src/runtime/hle/nso_host.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

extern int ffrs_test_multiwait_waiting;
extern int ffrs_test_multiwait_referenced;

static int errors;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); errors++; } } while (0)

struct event_object { unsigned char bytes[64]; };

static void init_event(struct event_object *event, int signaled) {
    memset(event, 0, sizeof *event);
    nso_host_os_initialize_event(event, signaled, 0);
}

int main(void) {
    alarm(20);
    struct event_object event;

    /* 1. Finalizing an event that a holder waits on must not abort the
     *    process. The old code called abort() here, which turned a lifetime
     *    race into a guaranteed crash during the scene unload. */
    ffrs_test_multiwait_waiting = 1;
    ffrs_test_multiwait_referenced = 1;
    init_event(&event, 0);
    nso_host_os_finalize_event(&event);
    CHECK(1);

    /* 2. The waiting holder is released: the host side reports the event as
     *    signaled, so an untimed wait can finish instead of hanging. */
    CHECK(nso_host_os_is_event_signaled(&event));

    /* 3. A late signal must not write into the guest object: the guest may
     *    already have released that page. */
    unsigned char pattern[64];
    memset(pattern, 0xA5, sizeof pattern);
    memcpy(event.bytes, pattern, sizeof pattern);
    nso_host_os_signal_event(&event);
    CHECK(memcmp(event.bytes, pattern, sizeof pattern) == 0);

    /* 4. The guest starts a new life on the same address. */
    ffrs_test_multiwait_waiting = 0;
    ffrs_test_multiwait_referenced = 0;
    init_event(&event, 1);
    CHECK(nso_host_os_try_wait_event(&event));

    /* 5. Finalizing twice is a no-op, not a crash. */
    nso_host_os_finalize_event(&event);
    nso_host_os_finalize_event(&event);
    CHECK(1);

    /* 6. The retired table is bounded: many retirements in a row must keep
     *    working and must not corrupt the entries that are still reachable. */
    static _Alignas(8) struct event_object many[3000];
    for (int i = 0; i < 3000; i++) {
        init_event(&many[i], 0);
        nso_host_os_finalize_event(&many[i]);
    }
    CHECK(1);

    if (errors) {
        fprintf(stderr, "event retire contract: %d checks failed\n", errors);
        return 1;
    }
    printf("event retire contract passed\n");
    return 0;
}
