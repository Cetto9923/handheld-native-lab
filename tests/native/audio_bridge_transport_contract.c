/* Full audio_bridge API/lifecycle test with a deliberately batching backend.
 * This is not a speaker/A2DP test. Run the Android ABI binary on RG477M. */
#include "../../src/runtime/hle/audio_bridge.h"
#include <aaudio/AAudio.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

struct AAudioStreamBuilderStruct {int channels,rate,performance;};
struct AAudioStreamStruct {int channels,rate,size;_Atomic int state;int64_t written;};
static pthread_mutex_t sink_lock=PTHREAD_MUTEX_INITIALIZER;
static int16_t sink[240*6*512];
static size_t sink_samples;
static int calls,opens,closes,errors,timeouts,disconnects,partial;
static _Atomic int block_writes;
static uint64_t now_ns(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000000ull+t.tv_nsec;}
#define CHECK(c) do{if(!(c)){fprintf(stderr,"FAIL line%d %s\n",__LINE__,#c);errors++;}}while(0)
aaudio_result_t AAudio_createStreamBuilder(AAudioStreamBuilder **b){*b=calloc(1,sizeof(**b));return *b?0:AAUDIO_ERROR_NO_MEMORY;}
aaudio_result_t AAudioStreamBuilder_delete(AAudioStreamBuilder *b){free(b);return 0;}
void AAudioStreamBuilder_setDirection(AAudioStreamBuilder *b,aaudio_direction_t v){(void)b;CHECK(v==AAUDIO_DIRECTION_OUTPUT);}
void AAudioStreamBuilder_setSharingMode(AAudioStreamBuilder *b,aaudio_sharing_mode_t v){(void)b;CHECK(v==AAUDIO_SHARING_MODE_SHARED);}
void AAudioStreamBuilder_setFormat(AAudioStreamBuilder *b,aaudio_format_t v){(void)b;CHECK(v==AAUDIO_FORMAT_PCM_I16);}
void AAudioStreamBuilder_setSampleRate(AAudioStreamBuilder *b,int32_t v){b->rate=v;}
void AAudioStreamBuilder_setChannelCount(AAudioStreamBuilder *b,int32_t v){b->channels=v;}
void AAudioStreamBuilder_setUsage(AAudioStreamBuilder *b,aaudio_usage_t v){(void)b;CHECK(v==AAUDIO_USAGE_GAME);}
void AAudioStreamBuilder_setContentType(AAudioStreamBuilder *b,aaudio_content_type_t v){(void)b;CHECK(v==AAUDIO_CONTENT_TYPE_MUSIC);}
void AAudioStreamBuilder_setPerformanceMode(AAudioStreamBuilder *b,aaudio_performance_mode_t v){CHECK(v==AAUDIO_PERFORMANCE_MODE_NONE);b->performance=v;}
aaudio_result_t AAudioStreamBuilder_openStream(AAudioStreamBuilder *b,AAudioStream **s){*s=calloc(1,sizeof(**s));if(!*s)return AAUDIO_ERROR_NO_MEMORY;(*s)->rate=b->rate?b->rate:48000;(*s)->channels=b->channels;(*s)->size=6150;atomic_store(&(*s)->state,AAUDIO_STREAM_STATE_OPEN);opens++;return 0;}
aaudio_result_t AAudioStream_close(AAudioStream *s){closes++;free(s);return 0;}
aaudio_result_t AAudioStream_requestStart(AAudioStream *s){atomic_store(&s->state,AAUDIO_STREAM_STATE_STARTED);return 0;}
aaudio_result_t AAudioStream_requestStop(AAudioStream *s){atomic_store(&s->state,AAUDIO_STREAM_STATE_STOPPED);return 0;}
int32_t AAudioStream_getFramesPerBurst(AAudioStream *s){(void)s;return 3075;}
int32_t AAudioStream_getBufferCapacityInFrames(AAudioStream *s){(void)s;return 6150;}
aaudio_result_t AAudioStream_setBufferSizeInFrames(AAudioStream *s,int32_t v){CHECK(v==6150);s->size=v;return v;}
int32_t AAudioStream_getBufferSizeInFrames(AAudioStream *s){return s->size;}
aaudio_stream_state_t AAudioStream_getState(AAudioStream *s){return atomic_load(&s->state);}
int32_t AAudioStream_getDeviceId(AAudioStream *s){(void)s;return 3;}
int32_t AAudioStream_getXRunCount(AAudioStream *s){(void)s;return 0;}
int64_t AAudioStream_getFramesWritten(AAudioStream *s){return s->written;}
int64_t AAudioStream_getFramesRead(AAudioStream *s){return s->written;}
aaudio_performance_mode_t AAudioStream_getPerformanceMode(AAudioStream *s){(void)s;return AAUDIO_PERFORMANCE_MODE_NONE;}
aaudio_usage_t AAudioStream_getUsage(AAudioStream *s){(void)s;return AAUDIO_USAGE_GAME;}
int32_t AAudioStream_getChannelCount(AAudioStream *s){return s->channels;}
int32_t AAudioStream_getSampleRate(AAudioStream *s){return s->rate;}
aaudio_result_t AAudioStream_write(AAudioStream *s,const void *pcm,int32_t frames,int64_t timeout){
    CHECK(timeout==100000000ll);int call=++calls;
    if(call==18){timeouts++;return AAUDIO_ERROR_TIMEOUT;}
    if(call==30){disconnects++;return AAUDIO_ERROR_DISCONNECTED;}
    uint64_t until=now_ns()+(call%16==0?80000000ull:0);
    while((block_writes || now_ns()<until) && atomic_load(&s->state)==AAUDIO_STREAM_STATE_STARTED)usleep(500);
    if(atomic_load(&s->state)!=AAUDIO_STREAM_STATE_STARTED)return AAUDIO_ERROR_INVALID_STATE;
    if(call==17 && frames>137){frames=137;partial++;}
    size_t n=(size_t)frames*s->channels;
    pthread_mutex_lock(&sink_lock);
    if(sink_samples+n>sizeof sink/sizeof *sink){errors++;pthread_mutex_unlock(&sink_lock);return AAUDIO_ERROR_INTERNAL;}
    memcpy(sink+sink_samples,pcm,n*sizeof(int16_t));sink_samples+=n;
    pthread_mutex_unlock(&sink_lock);s->written+=frames;return frames;
}
static _Atomic unsigned events;
void nso_host_os_bind_system_event(void *e){(void)e;}
void nso_host_os_signal_system_event(void *e){(void)e;events++;}
typedef struct {void *next,*data;uint64_t capacity,bytes,offset;} buffer_t;
#define LOOK(t,n) ((t)(uintptr_t)ffrs_audio_lookup(n,NULL,NULL))
static uint32_t (*open_out)(void*,void*,const void*);
static int (*append)(void*,buffer_t*);
static buffer_t *(*released)(void*);
static uint32_t (*start)(void*);
static void (*stop)(void*),(*close_out)(void*);
static void load_api(void){
 open_out=LOOK(uint32_t(*)(void*,void*,const void*),"_ZN2nn5audio19OpenDefaultAudioOutEPNS0_8AudioOutEPNS_2os11SystemEventERKNS0_17AudioOutParameterE");
 append=LOOK(int(*)(void*,buffer_t*),"_ZN2nn5audio20AppendAudioOutBufferEPNS0_8AudioOutEPNS0_14AudioOutBufferE");
 released=LOOK(buffer_t*(*)(void*),"_ZN2nn5audio25GetReleasedAudioOutBufferEPNS0_8AudioOutE");
 start=LOOK(uint32_t(*)(void*),"_ZN2nn5audio13StartAudioOutEPNS0_8AudioOutE");
 stop=LOOK(void(*)(void*),"_ZN2nn5audio12StopAudioOutEPNS0_8AudioOutE");
 close_out=LOOK(void(*)(void*),"_ZN2nn5audio13CloseAudioOutEPNS0_8AudioOutE");
 CHECK(open_out&&append&&released&&start&&stop&&close_out);
}
static int16_t value(size_t sample){return (int16_t)((int)(sample%16001)-8000);}
static void fill(int16_t *pcm,unsigned packet){for(unsigned i=0;i<240*6;i++)pcm[i]=value((size_t)packet*240*6+i);}
static void stream_test(unsigned count,int backpressure){
 unsigned char key[128]={0};uint64_t parameter=48000ull+(6ull<<32);buffer_t buffers[3];int16_t pcm[3][240*6];
 sink_samples=0;calls=0;timeouts=disconnects=partial=0;block_writes=backpressure;
 CHECK(open_out(key,key+80,&parameter)==0);
 unsigned sent=0,done=0;
 for(unsigned i=0;i<3;i++){fill(pcm[i],sent++);buffers[i]=(buffer_t){.data=pcm[i],.capacity=sizeof pcm[i],.bytes=sizeof pcm[i]};CHECK(append(key,buffers+i));}
 uint64_t began=now_ns();CHECK(start(key)==0);int full_checked=0;
 while(done<count && now_ns()-began<4000000000ull){
  uint64_t elapsed=now_ns()-began;
  if(backpressure && elapsed>650000000ull && !full_checked){CHECK(done<=102);full_checked=1;block_writes=0;}
  buffer_t *b=released(key);
  if(!b){usleep(100);continue;}
  CHECK(b>=buffers && b<buffers+3);
  if(done==0)CHECK(elapsed>=4000000ull);
  done++;unsigned i=(unsigned)(b-buffers);memset(pcm[i],0x77,sizeof pcm[i]);
  if(done==12)ffrs_audio_devices_changed();
  if(sent<count){fill(pcm[i],sent++);CHECK(append(key,b));}
 }
 block_writes=0;CHECK(done==count);
 double endpoint_seconds=(now_ns()-began)/1e9;
 if(!backpressure)CHECK(endpoint_seconds>count*.005-.005 && endpoint_seconds<count*.005+.08);
 struct ffrs_audio_stats stats={0};uint64_t wait=now_ns();
 while(now_ns()-wait<3000000000ull){CHECK(ffrs_audio_get_stats(key,&stats,sizeof stats)==0);if(stats.frames_written>=count*240u)break;usleep(1000);}
 CHECK(stats.frames_written==count*240u && stats.released_buffers==count && !stats.rejected_buffers);
 pthread_mutex_lock(&sink_lock);CHECK(sink_samples==count*240u*6u);for(size_t i=0;i<sink_samples;i++)if(sink[i]!=value(i)){CHECK(sink[i]==value(i));break;}pthread_mutex_unlock(&sink_lock);
 CHECK(timeouts==1 && disconnects==1 && partial==1);
 uint64_t closing=now_ns();stop(key);close_out(key);CHECK(now_ns()-closing<200000000ull);
 printf("stream count=%u full=%d endpoint_seconds=%.6f accepted=%llu errors=%d\n",count,backpressure,endpoint_seconds,(unsigned long long)stats.frames_written,errors);
}
static void stop_restart_test(void){
 unsigned char key[128]={0};uint64_t parameter=48000ull+(6ull<<32);int16_t pcm[4800*6]={0};buffer_t a={.data=pcm,.capacity=sizeof pcm,.bytes=sizeof pcm},b=a;
 calls=0;block_writes=0;CHECK(open_out(key,key+80,&parameter)==0);CHECK(append(key,&a));CHECK(append(key,&b));CHECK(start(key)==0);usleep(12000);stop(key);
 buffer_t *x=released(key),*y=released(key);CHECK((x==&a&&y==&b)||(x==&b&&y==&a));CHECK(released(key)==NULL);
 a.bytes=240*6*2;CHECK(append(key,&a));CHECK(start(key)==0);uint64_t t=now_ns();while(!released(key)&&now_ns()-t<200000000ull)usleep(100);
 /* Restarted endpoint owns/retires real data even before physical prefill. */
 CHECK(now_ns()-t<200000000ull);
 struct ffrs_audio_stats stats={0};t=now_ns();
 while(now_ns()-t<250000000ull){CHECK(ffrs_audio_get_stats(key,&stats,sizeof stats)==0);if(stats.frames_written>=240)break;usleep(1000);}
 CHECK(stats.frames_written==240); /* accepted write only; fake has no physical start threshold */
 stop(key);close_out(key);
 CHECK(open_out(key,key+80,&parameter)==0);t=now_ns();close_out(key);CHECK(now_ns()-t<100000000ull);
}
static void stop_blocked_writer_test(void){
 unsigned char key[128]={0};uint64_t parameter=48000ull+(6ull<<32);int16_t pcm[3][240*6]={{0}};buffer_t b[3];
 calls=0;block_writes=1;CHECK(open_out(key,key+80,&parameter)==0);
 for(unsigned i=0;i<3;i++){b[i]=(buffer_t){.data=pcm[i],.capacity=sizeof pcm[i],.bytes=sizeof pcm[i]};CHECK(append(key,b+i));}
 CHECK(start(key)==0);uint64_t t=now_ns();
 while(now_ns()-t<120000000ull){buffer_t *p=released(key);if(p)CHECK(append(key,p));else usleep(100);}
 t=now_ns();stop(key);CHECK(now_ns()-t<200000000ull);block_writes=0;
 unsigned returned=0;buffer_t *p;while((p=released(key))){CHECK(p>=b&&p<b+3);returned++;}CHECK(returned==3);
 close_out(key);
}
static void stereo_packet_test(void){
 unsigned char key[128]={0};uint64_t parameter=48000ull+(2ull<<32);
 int16_t pcm[3][1536*2];buffer_t b[3];unsigned sent=0,done=0;
 calls=0;timeouts=disconnects=partial=0;block_writes=0;sink_samples=0;
 CHECK(open_out(key,key+80,&parameter)==0);
 for(unsigned i=0;i<3;i++){
  for(unsigned j=0;j<1536*2;j++)pcm[i][j]=value((size_t)sent*1536*2+j);
  sent++;b[i]=(buffer_t){.data=pcm[i],.capacity=sizeof pcm[i],.bytes=sizeof pcm[i]};CHECK(append(key,b+i));
 }
 uint64_t t=now_ns();CHECK(start(key)==0);
 while(done<25 && now_ns()-t<3000000000ull){
  buffer_t *r=released(key);if(!r){usleep(100);continue;}done++;
  CHECK(r>=b && r<b+3);unsigned i=(unsigned)(r-b);memset(pcm[i],0x77,sizeof pcm[i]);
  if(sent<25){for(unsigned j=0;j<1536*2;j++)pcm[i][j]=value((size_t)sent*1536*2+j);sent++;CHECK(append(key,r));}
 }
 CHECK(done==25);CHECK(now_ns()-t>=790000000ull && now_ns()-t<900000000ull);
 struct ffrs_audio_stats s={0};t=now_ns();
 while(now_ns()-t<3000000000ull){CHECK(ffrs_audio_get_stats(key,&s,sizeof s)==0);if(s.frames_written==25*1536)break;usleep(1000);}
 CHECK(s.frames_written==25*1536 && sink_samples==25*1536*2);
 pthread_mutex_lock(&sink_lock);for(size_t i=0;i<sink_samples;i++)if(sink[i]!=value(i)){CHECK(sink[i]==value(i));break;}pthread_mutex_unlock(&sink_lock);
 stop(key);close_out(key);printf("stereo1536_packet errors=%d\n",errors);
}
int main(int argc,char **argv){
 if(argc!=2)return 2;mkdir(argv[1],0700);CHECK(ffrs_audio_set_platform_directory(argv[1])==0);
 char marker[512];snprintf(marker,sizeof marker,"%s/gfx-audio-paced-transport",argv[1]);FILE *f=fopen(marker,"wb");if(!f)return 3;fputs("1",f);fclose(f);
 load_api();stream_test(80,0);stream_test(160,1);stop_restart_test();stop_blocked_writer_test();stereo_packet_test();
 CHECK(opens==closes);printf("audio_bridge_transport_contract errors=%d opens=%d closes=%d events=%u\n",errors,opens,closes,(unsigned)events);return errors!=0;
}
