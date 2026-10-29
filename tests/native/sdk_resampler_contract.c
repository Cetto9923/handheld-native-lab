#include "../../src/runtime/hle/sdk_component.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#define LOOK(type,name) ((type)(uintptr_t)ffrs_sdk_component_lookup(name,0,0))
static int errors;
#define CHECK(c) do{if(!(c)){fprintf(stderr,"FAIL %d %s\n",__LINE__,#c);errors++;}}while(0)
int main(int argc,char **argv){if(argc!=2||ffrs_sdk_component_load(argv[1]))return 2;
 uint64_t(*size)(int)=LOOK(uint64_t(*)(int),"_ZN2nn5audio33GetRequiredBufferSizeForResamplerEi");
 uint32_t(*init)(void*,void*,uint64_t,int,int,int)=LOOK(uint32_t(*)(void*,void*,uint64_t,int,int,int),"_ZN2nn5audio19InitializeResamplerEPNS0_13ResamplerTypeEPvmiii");
 uint32_t(*process)(void*,int*,int16_t*,uint64_t,const int16_t*,int)=LOOK(uint32_t(*)(void*,int*,int16_t*,uint64_t,const int16_t*,int),"_ZN2nn5audio22ProcessResamplerBufferEPNS0_13ResamplerTypeEPiPsmPKsi");
 int(*count)(const void*,int)=LOOK(int(*)(const void*,int),"_ZN2nn5audio29GetResamplerOutputSampleCountEPKNS0_13ResamplerTypeEi");
 if(!size||!init||!process||!count)return 3;CHECK(size(2)==1216);
 struct{uint64_t pre;unsigned char bytes[32];uint64_t post;}state;memset(&state,0xa5,sizeof state);
 struct{uint64_t pre;unsigned char bytes[1216];uint64_t post;}work;memset(&work,0xa5,sizeof work);
 struct{uint64_t pre;int16_t pcm[1024];uint64_t post;}out;memset(&out,0xa5,sizeof out);
 int16_t in[960]={0};int n=-1;
 CHECK(init(state.bytes,work.bytes,sizeof work.bytes,48000,44100,2)==0);
 int expected=count(state.bytes,480);/* SDK +0x2634f0 returns ceil(frames*out/in)+2 capacity, not processed count. */CHECK(expected==443&&count(state.bytes,0)==0);
 CHECK(process(state.bytes,&n,out.pcm,sizeof out.pcm,in,480)==0);CHECK(n>370&&n<=441&&n<=expected);
 for(int i=0;i<n*2;i++)CHECK(out.pcm[i]==0);
 for(int i=0;i<480;i++)in[2*i]=in[2*i+1]=(int16_t)(10000*sin(i*6.28318530718*440/48000));
 CHECK(process(state.bytes,&n,out.pcm,sizeof out.pcm,in,480)==0);int peak=0;for(int i=0;i<n*2;i++){int v=abs(out.pcm[i]);if(v>peak)peak=v;}CHECK(peak>5000&&peak<15000);
 CHECK(process(state.bytes,&n,out.pcm,0,in,480)==0x899);
 CHECK(state.pre==UINT64_C(0xa5a5a5a5a5a5a5a5)&&state.post==UINT64_C(0xa5a5a5a5a5a5a5a5));CHECK(work.pre==state.pre&&work.post==state.post&&out.pre==state.pre&&out.post==state.post);
 printf("sdk_resampler_contract errors=%d frames=%d peak=%d capacity_query=%d\n",errors,n,peak,expected);return errors!=0;}
