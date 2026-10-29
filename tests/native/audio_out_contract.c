#include "../../src/runtime/hle/audio_bridge.h"
#include "../../src/runtime/hle/nso_host.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <stdlib.h>
static int errors;
#define CHECK(c) do { if(!(c)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);errors++;} }while(0)
#define LOOK(type,n) ((type)(uintptr_t)ffrs_audio_lookup(n,NULL,NULL))
int main(void) {
    alarm(15);
    uint32_t (*openout)(void*,void*,const void*)=LOOK(uint32_t(*)(void*,void*,const void*),"_ZN2nn5audio19OpenDefaultAudioOutEPNS0_8AudioOutEPNS_2os11SystemEventERKNS0_17AudioOutParameterE");
    uint32_t (*start)(void*)=LOOK(uint32_t(*)(void*),"_ZN2nn5audio13StartAudioOutEPNS0_8AudioOutE");
    void (*stop)(void*)=LOOK(void(*)(void*),"_ZN2nn5audio12StopAudioOutEPNS0_8AudioOutE");
    void (*closeout)(void*)=LOOK(void(*)(void*),"_ZN2nn5audio13CloseAudioOutEPNS0_8AudioOutE");
    int (*append)(void*,void*)=LOOK(int(*)(void*,void*),"_ZN2nn5audio20AppendAudioOutBufferEPNS0_8AudioOutEPNS0_14AudioOutBufferE");
    void *(*released)(void*)=LOOK(void*(*)(void*),"_ZN2nn5audio25GetReleasedAudioOutBufferEPNS0_8AudioOutE");
    int (*channels)(const void*)=LOOK(int(*)(const void*),"_ZN2nn5audio23GetAudioOutChannelCountEPKNS0_8AudioOutE");
    int (*state)(const void*)=LOOK(int(*)(const void*),"_ZN2nn5audio16GetAudioOutStateEPKNS0_8AudioOutE");
    const char *(*name)(const void*)=LOOK(const char*(*)(const void*),"_ZN2nn5audio15GetAudioOutNameEPKNS0_8AudioOutE");
    void (*init)(void*)=LOOK(void(*)(void*),"_ZN2nn5audio27InitializeAudioOutParameterEPNS0_17AudioOutParameterE");
    void (*setinfo)(void*,void*,uint64_t,uint64_t)=LOOK(void(*)(void*,void*,uint64_t,uint64_t),"_ZN2nn5audio21SetAudioOutBufferInfoEPNS0_14AudioOutBufferEPvmm");
    void *(*data)(const void*)=LOOK(void*(*)(const void*),"_ZN2nn5audio28GetAudioOutBufferDataPointerEPKNS0_14AudioOutBufferE");
    uint64_t (*size)(const void*)=LOOK(uint64_t(*)(const void*),"_ZN2nn5audio25GetAudioOutBufferDataSizeEPKNS0_14AudioOutBufferE");
    uint64_t (*capacity)(const void*)=LOOK(uint64_t(*)(const void*),"_ZN2nn5audio27GetAudioOutBufferBufferSizeEPKNS0_14AudioOutBufferE");
    if(!openout||!start||!stop||!closeout||!append||!released||!channels||!state||!name||!init||!setinfo||!data||!size||!capacity)return 2;
    /* Independent original-byte offsets, not a second copy of the C struct.
     * SDK1f14d0 initializes8bytes;1f14e0 only writes+8,+16,+24. */
    _Alignas(16) unsigned char parameter[24];memset(parameter,0x6b,sizeof parameter);init(parameter+8);
    uint64_t raw;memcpy(&raw,parameter+8,8);CHECK(raw==UINT64_C(0xcafe000000000000));
    for(unsigned i=0;i<8;i++){CHECK(parameter[i]==0x6b);CHECK(parameter[16+i]==0x6b);}
    unsigned char setters[56];memset(setters,0xa5,sizeof setters);void *known=(void*)(uintptr_t)0x12345678;
    setinfo(setters+8,known,0x1000,0x780);void *actual=NULL;memcpy(&actual,setters+16,8);CHECK(actual==known);
    memcpy(&raw,setters+24,8);CHECK(raw==0x1000);memcpy(&raw,setters+32,8);CHECK(raw==0x780);
    CHECK(data(setters+8)==known&&size(setters+8)==0x780&&capacity(setters+8)==0x1000);
    for(unsigned i=0;i<16;i++)CHECK(setters[i]==0xa5);for(unsigned i=40;i<56;i++)CHECK(setters[i]==0xa5);
    for(int count=2;count<=6;count+=4) {
        _Alignas(16) unsigned char output[0x178],event[128],descriptor[56];memset(output,0xa5,sizeof(output));memset(event,0xa5,sizeof(event));memset(descriptor,0x6b,sizeof descriptor);
        struct {int32_t rate;uint16_t channels,magic;} p={48000,(uint16_t)count,0xcafe};
        uint32_t result=openout(output,event,&p);CHECK(result==0);
        if(result)continue;
        CHECK(channels(output)==count);CHECK(state(output)==1);CHECK(name(output)==(char*)output+16);CHECK(!strcmp(name(output),"AAudioDefaultOutput"));
        const unsigned frames=24000;uint64_t bytes=(uint64_t)frames*count*2;
        int16_t *pcm=malloc((size_t)bytes);if(!pcm)return 2;
        for(unsigned i=0;i<frames;i++)for(int ch=0;ch<count;ch++)pcm[i*count+ch]=(int16_t)(8192.0*sin(6.283185307179586*440.0*i/48000.0));
        unsigned char *b=descriptor+8;
        /* main5de25d0 descriptor stride0x28; manual+8/+16/+24 input. */
        memcpy(b+8,&pcm,8);memcpy(b+16,&bytes,8);memcpy(b+24,&bytes,8);
        CHECK(append(output,b)==1);CHECK(append(output,b)==0);CHECK(released(output)==NULL);
        memcpy(&raw,b,8);CHECK(raw==0);memcpy(&raw,b+32,8);CHECK(raw==0);
        CHECK(start(output)==0);CHECK(state(output)==0);
        nso_host_os_wait_system_event(event);
        CHECK(released(output)==b && released(output)==NULL);
        struct ffrs_audio_stats stats;CHECK(ffrs_audio_get_stats(output,&stats,sizeof stats)==0);
        CHECK(stats.appended_buffers==1&&stats.released_buffers==1&&stats.rejected_buffers==1);
        CHECK(stats.submitted_frames==frames&&stats.frames_written==frames&&stats.samples_written==(uint64_t)frames*count);
        CHECK(stats.nonzero_samples>frames&&stats.peak>=8190&&stats.peak<=8192&&stats.errors==0);
        printf("component channels=%d actualFrames=%llu nonzero=%llu peak=%u AAudioState=%d device=%d xruns=%d\n",count,(unsigned long long)stats.frames_written,(unsigned long long)stats.nonzero_samples,stats.peak,stats.aaudio_state,stats.device_id,stats.xruns);
        stop(output);CHECK(state(output)==1);closeout(output);free(pcm);
        for(unsigned i=0;i<8;i++){CHECK(descriptor[i]==0x6b);CHECK(descriptor[48+i]==0x6b);}
        for(unsigned i=16+strlen("AAudioDefaultOutput")+1;i<sizeof(output);i++)CHECK(output[i]==0xa5);
        for(unsigned i=0;i<sizeof(event);i++)CHECK(event[i]==0xa5);
    }
    printf("audio_out_contract errors=%d (SDK offsets/state, real AAudio sine stereo and5.1; no game-audio claim)\n",errors);return errors?1:0;
}
