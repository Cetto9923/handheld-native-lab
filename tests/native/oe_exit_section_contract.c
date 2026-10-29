#include "../../src/runtime/hle/nso_host.h"
#include <stdio.h>
int main(void){int errors=0;
 if(nso_host_oe_host_exit_ready(false))errors++;
 nso_host_oe_enter_exit_section();nso_host_oe_enter_exit_section();
 if(nso_host_oe_host_exit_ready(true))errors++;
 nso_host_oe_leave_exit_section();if(nso_host_oe_host_exit_ready(false))errors++;
 nso_host_oe_leave_exit_section();if(!nso_host_oe_host_exit_ready(false))errors++;
 printf("oe_exit_section_contract errors=%d nested_request_deferred=1\n",errors);return errors!=0;}
