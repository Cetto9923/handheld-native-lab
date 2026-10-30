#include "../../src/runtime/hle/hid_bridge.h"
#include <stdio.h>
#include <string.h>
static int errors;
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);errors++; } } while(0)
int main(void) {
    unsigned args=99, rets=99; uint32_t id=0, missing=1;
    void (*init)(void)=(void (*)(void))(uintptr_t)ffrs_hid_lookup("_ZN2nn3hid14InitializeNpadEv",&args,&rets);
    CHECK(init && args==0 && rets==0);init();
    uint32_t (*style)(const uint32_t *)=(void *)(uintptr_t)ffrs_hid_lookup("_ZN2nn3hid15GetNpadStyleSetERKj",NULL,NULL);
    void (*get)(ffrs_npad_state*,const uint32_t*)=(void *)(uintptr_t)ffrs_hid_lookup("_ZN2nn3hid12GetNpadStateEPNS0_16NpadFullKeyStateERKj",&args,&rets);
    CHECK(get && args==2 && rets==0 && style(&id)==0);
    struct { uint64_t before; ffrs_npad_state s; uint64_t after; } out={0x1234,{0},0x5678};
    ffrs_hid_publish(1,1,50000,-50000,0,0); ffrs_hid_publish(1,0,50000,-50000,0,0);
    CHECK(style(&id)==1 && style(&missing)==0);
    get(&out.s,&missing);CHECK(out.s.attributes==0);
    get(&out.s,&id);CHECK(out.s.attributes==0x17 && (out.s.buttons&1));
    CHECK(out.s.lx==32767 && out.s.ly==-32767);
    CHECK((out.s.buttons & (1u<<18)) && (out.s.buttons & (1u<<19)));
    int64_t sample=out.s.sampling;get(&out.s,&id);CHECK(!(out.s.buttons&1) && out.s.sampling>sample);
    CHECK(out.before==0x1234 && out.after==0x5678);
    ffrs_hid_publish(0,1,1,1,1,1);get(&out.s,&id);CHECK(!out.s.attributes && !out.s.buttons && !out.s.lx);
    void (*sethold)(uint32_t)=(void *)(uintptr_t)ffrs_hid_lookup("_ZN2nn3hid18SetNpadJoyHoldTypeENS0_15NpadJoyHoldTypeE",NULL,NULL);
    uint32_t (*gethold)(void)=(void *)(uintptr_t)ffrs_hid_lookup("_ZN2nn3hid18GetNpadJoyHoldTypeEv",NULL,NULL);
    sethold(1);CHECK(gethold()==1);
    CHECK(!ffrs_hid_lookup("_ZN2nn3hidFutureUnsupported",NULL,NULL));
    printf("hid_bridge_contract errors=%d\n",errors);return errors?1:0;
}
