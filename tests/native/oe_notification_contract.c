#include "../../src/runtime/hle/nso_host.h"
#include <stdio.h>

static int errors;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); errors++; } } while (0)
int main(void) {
    uint32_t message = 0xdeadbeef;
    CHECK(!nso_host_oe_try_pop_notification_message(&message) && message == 0xdeadbeef);
    nso_host_oe_set_focus_handling_mode(2);
    nso_host_oe_set_host_focus(1);
    CHECK(nso_host_oe_get_current_focus_state() == 1);
    CHECK(nso_host_oe_try_pop_notification_message(&message) && message == 15);
    message = 0xdeadbeef;
    nso_host_oe_set_host_focus(1);
    CHECK(!nso_host_oe_try_pop_notification_message(&message) && message == 0xdeadbeef);
    nso_host_oe_set_host_focus(2);
    nso_host_oe_set_host_focus(3);
    CHECK(nso_host_oe_get_current_focus_state() == 3);
    CHECK(!nso_host_oe_try_pop_notification_message(NULL));
    CHECK(nso_host_oe_try_pop_notification_message(&message) && message == 15);
    CHECK(!nso_host_oe_try_pop_notification_message(&message));
    nso_host_oe_set_focus_handling_mode(0);
    nso_host_oe_set_host_focus(1);
    CHECK(nso_host_oe_get_current_focus_state() == 1);
    CHECK(!nso_host_oe_try_pop_notification_message(&message));
    nso_host_oe_set_host_focus(42);
    CHECK(nso_host_oe_get_current_focus_state() == 1);
    printf("oe_notification_contract errors=%d\n", errors);
    return errors ? 1 : 0;
}
