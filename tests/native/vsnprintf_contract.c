#include "../../src/runtime/hle/nso_host.h"
#include <string.h>
static int errors;
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);errors++; } } while(0)
static void probe(const char *fmt, ...) {
    va_list ap, oracle; va_start(ap,fmt);va_copy(oracle,ap);
    unsigned char original[sizeof(ap)];memcpy(original,&ap,sizeof(ap));
    char expected[256],actual[256],shortened[5];
    int wanted=vsnprintf(expected,sizeof(expected),fmt,oracle);va_end(oracle);
    int found=nso_host_vsnprintf(actual,sizeof(actual),fmt,&ap);
    CHECK(found==wanted && !strcmp(expected,actual));
    CHECK(!memcmp(original,&ap,sizeof(ap)));
    CHECK(nso_host_vsnprintf(NULL,0,fmt,&ap)==wanted);
    CHECK(nso_host_vsnprintf(shortened,sizeof(shortened),fmt,&ap)==wanted);
    CHECK(shortened[4]==0 && !memcmp(shortened,expected,4));
    va_end(ap);
}
int main(void) {
    probe("value=%d name=%s float=%.3f wide=%lld",-7,"port",1.25,1234567890123LL);
    probe("%d %d %d %d %d %d %d %d %d %d %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f",1,2,3,4,5,6,7,8,9,10,1.,2.,3.,4.,5.,6.,7.,8.,9.);
    printf("vsnprintf_contract errors=%d\n",errors);return errors?1:0;
}
