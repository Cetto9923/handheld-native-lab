#include "../../src/runtime/hle/nso_host.h"
#include <time.h>
#include <stdio.h>
static void pause_ns(long ns){struct timespec t={0,ns};nanosleep(&t,NULL);}
int main(void){int errors=0;
 nso_host_oe_set_host_focus(1);pause_ns(10000000);int64_t a=nso_host_oe_get_program_total_active_time();if(a<5000000)errors++;
 nso_host_oe_set_host_focus(2);pause_ns(10000000);int64_t b=nso_host_oe_get_program_total_active_time();if(b-a<5000000)errors++;
 nso_host_oe_set_host_focus(3);int64_t stopped=nso_host_oe_get_program_total_active_time();pause_ns(20000000);if(nso_host_oe_get_program_total_active_time()!=stopped)errors++;
 nso_host_oe_set_host_focus(1);pause_ns(10000000);int64_t c=nso_host_oe_get_program_total_active_time();if(c-stopped<5000000)errors++;
 nso_host_oe_initialize();if(nso_host_oe_get_program_total_active_time()<c)errors++;
 printf("oe_active_time_contract errors=%d active_ns=%lld background_frozen=1 unfocused_active=1 resume=1\n",errors,(long long)c);return errors!=0;
}
