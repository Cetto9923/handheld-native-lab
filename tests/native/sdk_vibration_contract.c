#include "../../src/runtime/hle/sdk_component.h"
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
static int errors;
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);errors++; } } while(0)
int main(int argc,char **argv) {
    if (argc!=2) return 2;
    CHECK(ffrs_sdk_component_load(argv[1])==0);
    unsigned args=0,rets=0;
    const int64_t *duration=(const int64_t *)(uintptr_t)ffrs_sdk_component_lookup("_ZN2nn3hid18VibrationConverter13ChunkDurationE",NULL,NULL);
    CHECK(duration && *duration==5000000);
    void (*ctor)(void*,int)=(void *)(uintptr_t)ffrs_sdk_component_lookup("_ZN2nn3hid18VibrationConverterC1Ei",&args,&rets);
    CHECK(ctor && args==2 && rets==0);
    void (*process)(void*,void*,const int16_t*,int)=(void *)(uintptr_t)ffrs_sdk_component_lookup("_ZN2nn3hid18VibrationConverter12ProcessChunkEPNS0_14VibrationValueEPKsi",&args,&rets);
    CHECK(process && args==4 && rets==0);
    void (*dtor)(void*)=(void *)(uintptr_t)ffrs_sdk_component_lookup("_ZN2nn3hid18VibrationConverterD1Ev",NULL,NULL);
    CHECK(!ffrs_sdk_component_lookup("malloc",NULL,NULL));
    if (!ctor || !process || !dtor) return 1;
    struct { _Alignas(16) uint64_t before[2]; unsigned char object[0x1010]; uint64_t after[2]; } state;
    struct { _Alignas(16) uint64_t before[2]; float value[4]; uint64_t after[2]; } output;
    memset(&state,0xa5,sizeof(state));memset(&output,0xa5,sizeof(output));
    _Alignas(16) int16_t pcm[240]={0};
    ctor(state.object,48000);process(state.object,output.value,pcm,240);
    for(int i=0;i<4;i++) CHECK(isfinite(output.value[i]));
    CHECK(output.value[0]==0 && output.value[2]==0);
    float peak=0;
    for(int chunk=0;chunk<40;chunk++) {
        for(int i=0;i<240;i++) pcm[i]=(int16_t)(16000*sin((chunk*240+i)*2*3.141592653589793*160/48000));
        process(state.object,output.value,pcm,240);
        if(output.value[0]>peak)peak=output.value[0];
    }
    CHECK(peak>0);
    for(int i=0;i<2;i++) {
        CHECK(state.before[i]==UINT64_C(0xa5a5a5a5a5a5a5a5) && state.after[i]==UINT64_C(0xa5a5a5a5a5a5a5a5));
        CHECK(output.before[i]==UINT64_C(0xa5a5a5a5a5a5a5a5) && output.after[i]==UINT64_C(0xa5a5a5a5a5a5a5a5));
    }
    dtor(state.object);
    printf("sdk_vibration_contract errors=%d peak=%f last=%f/%f/%f/%f\n",errors,peak,
           output.value[0],output.value[1],output.value[2],output.value[3]);
    return errors?1:0;
}
