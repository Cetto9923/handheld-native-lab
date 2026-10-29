/* Android's real default output endpoint. Device names are adapter-local
 * identifiers, not claims about the physical speaker/headphone route. */
#include "audio_bridge.h"
#include "nso_host.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <stddef.h>
#include <limits.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#if defined(__ANDROID__)
#include <aaudio/AAudio.h>
#endif
static pthread_mutex_t audio_lock = PTHREAD_MUTEX_INITIALIZER;
static void *device_events[8];
static int cached_channels;
/* Exact SDK SHA9dfd1cc6... AudioOutBuffer, passed as0x28bytes atVA1f1eb0.
 * SetInfo1f14e0 writes+8,+16,+24 only; AudioBufferList1f148c clears+0,+32. */
typedef struct { void *next,*data; uint64_t capacity,bytes,offset; } audio_buffer;
_Static_assert(sizeof(audio_buffer)==40&&offsetof(audio_buffer,data)==8&&
    offsetof(audio_buffer,capacity)==16&&offsetof(audio_buffer,bytes)==24&&
    offsetof(audio_buffer,offset)==32,"verified retail AudioOutBuffer ABI");
static char diagnostic_directory[PATH_MAX];
static unsigned diagnostic_serial;
typedef struct {
    void *key, *event;
    int channels, rate;
    _Atomic int state;
    _Atomic int device_changed;
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    pthread_t worker;
    int closing, busy;
    audio_buffer *pending[64], *released[64], *active;
    unsigned ph, pt, rh, rt;
    uint64_t frames_written;
    struct ffrs_audio_stats stats;
    uint64_t last_log_frames,capture_frames;
    /* Level window: 100 ms of stream time. Records how much of the guest's own
     * PCM was silent and the longest silent run inside the window, which is
     * what a listener hears as a stutter. */
    uint64_t level_window_frames,level_window_nonzero,level_window_max_zero_run,level_zero_run,level_window_peak;
    FILE *capture;
#if defined(__ANDROID__)
    AAudioStream *stream;
#endif
} audio_output;
static audio_output *outputs[8];
static audio_output *find_output(const void *key) {
    pthread_mutex_lock(&audio_lock);
    audio_output *found = NULL;
    for (int i = 0; i < 8; i++) if (outputs[i] && outputs[i]->key == key) { found=outputs[i];break; }
    pthread_mutex_unlock(&audio_lock);return found;
}
static void mirror_output(audio_output *out) {
    uint32_t *words=out->key;
    words[0]=out->rate;words[1]=out->channels;words[2]=2;
    /* Horizon AudioOutState: Started=0, Stopped=1. Internal state is a bool. */
    __atomic_store_n(words+3,atomic_load(&out->state)?0u:1u,__ATOMIC_RELEASE);
}
static void release_buffer(audio_output *out,audio_buffer *buffer) {
    if (out->rt-out->rh>=64) {fputs("[audio] released queue overflow\n",stderr);abort();}
    out->released[out->rt++%64]=buffer;
    out->stats.released_buffers++;
}
static void scan_written(audio_output *out,const int16_t *pcm,uint64_t frames) {
    uint64_t samples=frames*(unsigned)out->channels;
    out->stats.frames_written+=frames;out->stats.samples_written+=samples;
    /* A frame is silent when every channel of it is zero. Counting frames
     * instead of samples keeps the silent-run length a real time gap: with six
     * channels one quiet channel would otherwise look like silence. */
    uint64_t audible=0,max_run=out->level_window_max_zero_run,silent_run=out->level_zero_run,window_peak=out->level_window_peak;
    const uint64_t channels=out->channels;
    for(uint64_t f=0;f<frames;f++){
        uint32_t frame_peak=0;int frame_audible=0;
        for(uint64_t c=0;c<channels;c++){
            int32_t value=pcm[f*channels+c];uint32_t peak=(uint32_t)(value<0?-value:value);
            if(value){out->stats.nonzero_samples++;frame_audible=1;}
            if(peak>frame_peak)frame_peak=peak;
        }
        if(frame_audible){audible++;silent_run=0;}
        else{silent_run++;if(silent_run>max_run)max_run=silent_run;}
        if(frame_peak>out->stats.peak)out->stats.peak=frame_peak;
        if(frame_peak>window_peak)window_peak=frame_peak;
    }
    out->level_zero_run=silent_run;out->level_window_max_zero_run=max_run;out->level_window_peak=window_peak;
    out->level_window_frames+=frames;out->level_window_nonzero+=audible;
    if(out->level_window_frames>=(uint64_t)out->rate/10u){
        fprintf(stderr,"[audio] level key=%p window_ms=%llu audible_frames=%llu frames=%llu max_silent_run_ms=%llu peak=%u\n",
            out->key,(unsigned long long)(out->level_window_frames*1000ull/(uint64_t)out->rate),
            (unsigned long long)out->level_window_nonzero,(unsigned long long)out->level_window_frames,
            (unsigned long long)(out->level_window_max_zero_run*1000ull/(uint64_t)out->rate),(unsigned)out->level_window_peak);
        out->level_window_frames=0;out->level_window_nonzero=0;out->level_window_max_zero_run=0;out->level_window_peak=0;
    }
    /* Three seconds only covered the silent start-up phase, where the title
     * music has not started yet. Capture the first three minutes so the whole
     * audible window can be analysed offline. */
    if(out->capture&&out->capture_frames<180u*(uint64_t)out->rate){
        uint64_t count=180u*(uint64_t)out->rate-out->capture_frames;if(count>frames)count=frames;
        size_t expected=(size_t)(count*out->channels),wrote=fwrite(pcm,sizeof(int16_t),expected,out->capture);
        out->capture_frames+=wrote/(unsigned)out->channels;
        if(wrote!=expected||out->capture_frames==180u*(uint64_t)out->rate){fclose(out->capture);out->capture=NULL;}
    }
}
static void log_stats(audio_output *out,const char *reason) {
#if defined(__ANDROID__)
    fprintf(stderr,"[audio] PCM %s key=%p buffers=%llu released=%llu rejected=%llu submitted_frames=%llu written_frames=%llu nonzero=%llu samples=%llu peak=%u AAudio_state=%d device=%d xruns=%d stream_written=%lld played=%lld\n",
        reason,out->key,(unsigned long long)out->stats.appended_buffers,(unsigned long long)out->stats.released_buffers,
        (unsigned long long)out->stats.rejected_buffers,(unsigned long long)out->stats.submitted_frames,
        (unsigned long long)out->stats.frames_written,(unsigned long long)out->stats.nonzero_samples,
        (unsigned long long)out->stats.samples_written,out->stats.peak,AAudioStream_getState(out->stream),
        AAudioStream_getDeviceId(out->stream),AAudioStream_getXRunCount(out->stream),
        (long long)AAudioStream_getFramesWritten(out->stream),(long long)AAudioStream_getFramesRead(out->stream));
#else
    (void)out;(void)reason;
#endif
}
#if defined(__ANDROID__)
static aaudio_result_t open_aaudio_stream(audio_output *out) {
    if (out->stream) {
        AAudioStream_close(out->stream);
        out->stream = NULL;
    }
    AAudioStreamBuilder *builder = NULL;
    aaudio_result_t status = AAudio_createStreamBuilder(&builder);
    if (status != AAUDIO_OK) return status;

    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setSampleRate(builder, out->rate);
    AAudioStreamBuilder_setChannelCount(builder, out->channels);
    AAudioStreamBuilder_setUsage(builder, AAUDIO_USAGE_GAME);
    AAudioStreamBuilder_setContentType(builder, AAUDIO_CONTENT_TYPE_MUSIC);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_NONE);

    status = AAudioStreamBuilder_openStream(builder, &out->stream);
    AAudioStreamBuilder_delete(builder);

    if (status == AAUDIO_OK && out->stream) {
        int32_t burst = AAudioStream_getFramesPerBurst(out->stream);
        int32_t cap = AAudioStream_getBufferCapacityInFrames(out->stream);
        int32_t desired = burst > 0 ? burst * 2 : 1920;
        if (desired < 1920) desired = 1920;
        if (burst > 1024) {
            /* Bluetooth A2DP (burst ~3354 / 70ms): use 2x burst to absorb wireless transmission jitter */
            desired = burst * 2;
        } else {
            /* Speaker / wired headset: cap at 3840 frames (~80ms) for low latency */
            if (desired > 3840) desired = 3840;
        }
        if (cap > 0 && desired > cap) desired = cap;
        if (desired > 0) {
            AAudioStream_setBufferSizeInFrames(out->stream, desired);
        }
        fprintf(stderr, "[audio] real AudioOut stream ready: key=%p rate=%d channels=%u device=%d burst=%d capacity=%d buf=%d\n",
                out->key, out->rate, out->channels,
                AAudioStream_getDeviceId(out->stream), burst, cap,
                AAudioStream_getBufferSizeInFrames(out->stream));
    }
    return status;
}
#endif
static void *audio_worker(void *argument) {
    audio_output *out=argument;
    pthread_mutex_lock(&out->mutex);
    while(!out->closing) {
        while(!out->closing && (!atomic_load(&out->state) || out->ph==out->pt))
            pthread_cond_wait(&out->condition,&out->mutex);
        if(out->closing)break;
        audio_buffer *buffer=out->pending[out->ph++%64];out->active=buffer;out->busy=1;
        pthread_mutex_unlock(&out->mutex);
        uint64_t position=0;
#if defined(__ANDROID__)
        uint64_t frames=buffer->bytes/(2u*out->channels);

        if (atomic_exchange(&out->device_changed, 0) || !out->stream) {
            if (open_aaudio_stream(out) == AAUDIO_OK && out->stream) {
                if (atomic_load(&out->state)) AAudioStream_requestStart(out->stream);
            }
        }

        while(position<frames && atomic_load(&out->state)) {
            if (!out->stream) {
                if (open_aaudio_stream(out) == AAUDIO_OK && out->stream) {
                    if (atomic_load(&out->state)) AAudioStream_requestStart(out->stream);
                }
            }
            if (!out->stream) {
                uint64_t rem = frames - position;
                struct timespec ts;
                ts.tv_sec = (time_t)(rem / (uint64_t)out->rate);
                ts.tv_nsec = (long)((rem % (uint64_t)out->rate) * 1000000000ull / (uint64_t)out->rate);
                nanosleep(&ts, NULL);
                position = frames;
                break;
            }

            int32_t ask=(int32_t)(frames-position>1024?1024:frames-position);
            int64_t timeout_ns = 100000000ll; /* 100ms: generous for Bluetooth A2DP packet scheduling */

            aaudio_result_t wrote=AAudioStream_write(out->stream,(const int16_t*)buffer->data+position*out->channels,ask,timeout_ns);
            pthread_mutex_lock(&out->mutex);out->stats.write_calls++;
            if(wrote == AAUDIO_ERROR_TIMEOUT) {
                out->stats.zero_writes++;
                pthread_mutex_unlock(&out->mutex);
                usleep(2000);
                continue;
            } else if(wrote < 0) {
                fprintf(stderr,"[audio] real AAudio write failed %d; device=%d\n", (int)wrote, AAudioStream_getDeviceId(out->stream));
                out->stats.errors++;
                pthread_mutex_unlock(&out->mutex);

                if (wrote == AAUDIO_ERROR_DISCONNECTED || atomic_exchange(&out->device_changed, 0)) {
                    if (open_aaudio_stream(out) == AAUDIO_OK && out->stream) {
                        if (atomic_load(&out->state)) AAudioStream_requestStart(out->stream);
                        continue;
                    }
                }
                uint64_t rem = frames - position;
                struct timespec ts;
                ts.tv_sec = (time_t)(rem / (uint64_t)out->rate);
                ts.tv_nsec = (long)((rem % (uint64_t)out->rate) * 1000000000ull / (uint64_t)out->rate);
                nanosleep(&ts, NULL);
                position = frames;
                break;
            }
            if(wrote==0) {
                out->stats.zero_writes++;
                pthread_mutex_unlock(&out->mutex);
                usleep(1000);
            } else {
                scan_written(out,(const int16_t*)buffer->data+position*out->channels,(uint32_t)wrote);
                if(wrote>0&&(!out->last_log_frames||out->stats.frames_written-out->last_log_frames>=(uint64_t)out->rate)){
                    log_stats(out,"progress");out->last_log_frames=out->stats.frames_written;
                }
                pthread_mutex_unlock(&out->mutex);
                position+=(uint32_t)wrote;
            }
        }
#endif
        pthread_mutex_lock(&out->mutex);
        out->frames_written+=position;out->active=NULL;out->busy=0;
        release_buffer(out,buffer);
        pthread_cond_broadcast(&out->condition);
        void *event=out->event;
        pthread_mutex_unlock(&out->mutex);
        if(event)nso_host_os_signal_system_event(event);
        pthread_mutex_lock(&out->mutex);
    }
    pthread_mutex_unlock(&out->mutex);return NULL;
}
static void initialize_parameter(void *parameter) {if(parameter){uint64_t defaults=UINT64_C(0xcafe000000000000);memcpy(parameter,&defaults,8);}}
static uint32_t open_output_event(void *key,void *event,const void *parameter) {
#if defined(__ANDROID__)
    if(!key || !parameter || find_output(key))return 0x2a2;
    int32_t rate;uint16_t channels;
    memcpy(&rate,parameter,4);memcpy(&channels,(const char*)parameter+4,2);
    if(!rate)rate=48000;if(!channels)channels=2;
    if(rate<8000 || rate>192000 || channels>8)return 0x2a2;
    audio_output *out=calloc(1,sizeof(*out));if(!out)return 0x2a2;
    out->key=key;out->event=event;out->rate=rate;out->channels=channels;
    aaudio_result_t status=open_aaudio_stream(out);
    if(status!=AAUDIO_OK || !out->stream) {
        fprintf(stderr,"[audio] AudioOut refused: rate=%d channels=%u AAudio=%d\n",rate,channels,status);
        if(out->stream)AAudioStream_close(out->stream);free(out);return 0x2a2;
    }
    pthread_mutex_init(&out->mutex,NULL);pthread_cond_init(&out->condition,NULL);
    pthread_mutex_lock(&audio_lock);int slot=-1;
    for(int i=0;i<8;i++)if(!outputs[i]){slot=i;break;}
    if(slot>=0)outputs[slot]=out;
    pthread_mutex_unlock(&audio_lock);
    if(slot<0 || pthread_create(&out->worker,NULL,audio_worker,out)) {
        pthread_mutex_lock(&audio_lock);if(slot>=0)outputs[slot]=NULL;pthread_mutex_unlock(&audio_lock);
        AAudioStream_close(out->stream);pthread_cond_destroy(&out->condition);pthread_mutex_destroy(&out->mutex);free(out);return 0x2a2;
    }
    if(event)nso_host_os_bind_system_event(event);
    strcpy((char*)key+16,"AAudioDefaultOutput");
    pthread_mutex_lock(&audio_lock);
    if(diagnostic_directory[0]){
        char path[PATH_MAX];int n=snprintf(path,sizeof(path),"%s/audio-%u-%dHz-%dch-s16le.pcm",diagnostic_directory,++diagnostic_serial,rate,channels);
        if(n>0&&(size_t)n<sizeof(path)){int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0600);if(fd>=0){out->capture=fdopen(fd,"wb");if(!out->capture)close(fd);}}
    }
    pthread_mutex_unlock(&audio_lock);
    mirror_output(out);
    fprintf(stderr,"[audio] real AudioOut opened key=%p rate=%d channels=%u device=%d frames_per_burst=%d performance=%d capacity_frames=%d usage=%d\n",
            key,rate,channels,AAudioStream_getDeviceId(out->stream),AAudioStream_getFramesPerBurst(out->stream),
            AAudioStream_getPerformanceMode(out->stream),AAudioStream_getBufferCapacityInFrames(out->stream),AAudioStream_getUsage(out->stream));
    return 0;
#else
    (void)key;(void)event;(void)parameter;return 0x2a2;
#endif
}
static uint32_t open_output(void *key,const void *parameter) {return open_output_event(key,NULL,parameter);}
static uint32_t start_output(void *key) {
    audio_output *out=find_output(key);if(!out)return 0x2a2;
#if defined(__ANDROID__)
    if(AAudioStream_requestStart(out->stream)!=AAUDIO_OK)return 0x2a2;
#endif
    atomic_store(&out->state,1);mirror_output(out);
    pthread_mutex_lock(&out->mutex);log_stats(out,"start");pthread_cond_broadcast(&out->condition);pthread_mutex_unlock(&out->mutex);return 0;
}
static void stop_output(void *key) {
    audio_output *out=find_output(key);if(!out)return;
    atomic_store(&out->state,0);
#if defined(__ANDROID__)
    AAudioStream_requestStop(out->stream);
#endif
    pthread_mutex_lock(&out->mutex);
    while(out->busy)pthread_cond_wait(&out->condition,&out->mutex);
    int returned=0;while(out->ph!=out->pt){release_buffer(out,out->pending[out->ph++%64]);returned=1;}
    pthread_mutex_unlock(&out->mutex);mirror_output(out);if(returned&&out->event)nso_host_os_signal_system_event(out->event);
}
static void close_output(void *key) {
    audio_output *out=find_output(key);if(!out)return;stop_output(key);
    pthread_mutex_lock(&out->mutex);out->closing=1;pthread_cond_broadcast(&out->condition);pthread_mutex_unlock(&out->mutex);
    pthread_join(out->worker,NULL);
    log_stats(out,"close");if(out->capture){fclose(out->capture);out->capture=NULL;}
#if defined(__ANDROID__)
    AAudioStream_close(out->stream);
#endif
    fprintf(stderr,"[audio] AudioOut closed key=%p real_frames_written=%llu\n",key,(unsigned long long)out->frames_written);
    pthread_mutex_lock(&audio_lock);for(int i=0;i<8;i++)if(outputs[i]==out)outputs[i]=NULL;pthread_mutex_unlock(&audio_lock);
    pthread_cond_destroy(&out->condition);pthread_mutex_destroy(&out->mutex);free(out);
}
static int get_rate(const void *key) {audio_output *out=find_output(key);return out?out->rate:0;}
static int get_channels(const void *key) {audio_output *out=find_output(key);return out?out->channels:0;}
static int get_format(const void *key) {return find_output(key)?2:0;}
static int get_state(const void *key) {audio_output *out=find_output(key);return out?(atomic_load(&out->state)?0:1):1;}
static const char *get_name(const void *key) {return find_output(key)?(const char*)key+16:"";}
static void buffer_info(audio_buffer *b,void *data,uint64_t cap,uint64_t bytes) {if(b){b->data=data;b->capacity=cap;b->bytes=bytes;}}
static void *buffer_data(const audio_buffer *b) {return b?b->data:NULL;}
static uint64_t buffer_size(const audio_buffer *b) {return b?b->bytes:0;}
static uint64_t buffer_capacity(const audio_buffer *b) {return b?b->capacity:0;}
static int append_buffer(void *key,audio_buffer *buffer) {
    audio_output *out=find_output(key);
    if(!out)return 0;
    pthread_mutex_lock(&out->mutex);
    int ok=buffer&&buffer->data&&buffer->bytes&&buffer->bytes<=buffer->capacity&&
        buffer->bytes%(2u*out->channels)==0&&!out->closing&&
        out->pt-out->ph+out->rt-out->rh+out->busy<64&&out->active!=buffer;
    for(unsigned i=out->ph;ok && i!=out->pt;i++)if(out->pending[i%64]==buffer)ok=0;
    for(unsigned i=out->rh;ok && i!=out->rt;i++)if(out->released[i%64]==buffer)ok=0;
    if(ok){
        buffer->next=NULL;buffer->offset=0; // original AudioBufferList::push_back
        out->stats.appended_buffers++;out->stats.submitted_frames+=buffer->bytes/(2u*out->channels);
        out->pending[out->pt++%64]=buffer;pthread_cond_broadcast(&out->condition);
    }else{
        out->stats.rejected_buffers++;
        if(out->stats.rejected_buffers<=8)fprintf(stderr,"[audio] append rejected key=%p buffer=%p data=%p capacity=%llu bytes=%llu pending=%u released=%u active=%u\n",key,(void*)buffer,buffer?buffer->data:NULL,
            (unsigned long long)(buffer?buffer->capacity:0),(unsigned long long)(buffer?buffer->bytes:0),out->pt-out->ph,out->rt-out->rh,out->busy);
    }
    pthread_mutex_unlock(&out->mutex);return ok;
}
static audio_buffer *released_buffer(void *key) {
    audio_output *out=find_output(key);if(!out)return NULL;
    pthread_mutex_lock(&out->mutex);
    audio_buffer *buffer=out->rh!=out->rt?out->released[out->rh++%64]:NULL;
    pthread_mutex_unlock(&out->mutex);return buffer;
}
int ffrs_audio_get_stats(const void *key,struct ffrs_audio_stats *stats,size_t bytes) {
    if(!stats||bytes!=sizeof(*stats))return -1;audio_output *out=find_output(key);if(!out)return -1;
    pthread_mutex_lock(&out->mutex);*stats=out->stats;stats->sample_rate=out->rate;stats->channels=out->channels;stats->running=atomic_load(&out->state);
#if defined(__ANDROID__)
    stats->aaudio_state=AAudioStream_getState(out->stream);stats->device_id=AAudioStream_getDeviceId(out->stream);stats->xruns=AAudioStream_getXRunCount(out->stream);
#endif
    pthread_mutex_unlock(&out->mutex);return 0;
}
int ffrs_audio_set_diagnostics(const char *directory) {
    struct stat st;if(directory&&(directory[0]!='/'||strlen(directory)>=sizeof(diagnostic_directory)||stat(directory,&st)||!S_ISDIR(st.st_mode)))return -1;
    pthread_mutex_lock(&audio_lock);if(directory)strcpy(diagnostic_directory,directory);else diagnostic_directory[0]=0;pthread_mutex_unlock(&audio_lock);return 0;
}
static int output_channels(void) {
    pthread_mutex_lock(&audio_lock);
#if defined(__ANDROID__)
    if (!cached_channels) {
        AAudioStreamBuilder *builder = NULL; AAudioStream *stream = NULL;
        aaudio_result_t result = AAudio_createStreamBuilder(&builder);
        if (result == AAUDIO_OK) {
            AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
            AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
            AAudioStreamBuilder_setChannelCount(builder, 2);
            result = AAudioStreamBuilder_openStream(builder, &stream);
            if (result == AAUDIO_OK) {
                cached_channels = AAudioStream_getChannelCount(stream);
                fprintf(stderr,"[audio] AAudio default endpoint opened: channels=%d rate=%d device=%d (not playing)\n",
                        cached_channels, AAudioStream_getSampleRate(stream), AAudioStream_getDeviceId(stream));
                AAudioStream_close(stream);
            }
            AAudioStreamBuilder_delete(builder);
        }
        if (result != AAUDIO_OK) fprintf(stderr,"[audio] default output unavailable: %d\n", result);
    }
#endif
    int value = cached_channels;
    pthread_mutex_unlock(&audio_lock); return value;
}
static void device_name(char *out) {
    if (!out) return;
    memset(out, 0, 256);
    if (output_channels()) strcpy(out, "AAudioDefaultOutput");
}
static void acquire_device_event(void *event) {
    nso_host_os_bind_system_event(event);
    pthread_mutex_lock(&audio_lock);
    for (int i = 0; i < 8; i++) {
        if (device_events[i] == event) break;
        if (!device_events[i]) { device_events[i] = event; break; }
    }
    pthread_mutex_unlock(&audio_lock);
}
void ffrs_audio_devices_changed(void) {
    pthread_mutex_lock(&audio_lock);
    cached_channels = 0;
    for (int i = 0; i < 8; i++) {
        if (outputs[i]) {
            atomic_store(&outputs[i]->device_changed, 1);
            pthread_mutex_lock(&outputs[i]->mutex);
            pthread_cond_broadcast(&outputs[i]->condition);
            pthread_mutex_unlock(&outputs[i]->mutex);
        }
        if (device_events[i]) nso_host_os_signal_system_event(device_events[i]);
    }
    pthread_mutex_unlock(&audio_lock);
}
/* Retail SDK 0023a1b0: GetSampleByteSize +0x21f740, 40 bytes;
 * table +0x943988 gives signed8/16/24/32/float32 sizes 1,2,3,4,4.
 * Invalid enum follows the SDK abort path, never guesses a successful size. */
static uint64_t sample_byte_size(int format) {
    static const uint64_t bytes[]={1,2,3,4,4};
    if(format<1 || format>5){fprintf(stderr,"[audio] invalid SampleFormat %d\n",format);abort();}
    return bytes[format-1];
}
uint64_t ffrs_audio_lookup(const char *name, unsigned *arguments, unsigned *returns) {
    uint64_t function = 0; unsigned args = 0, rets = 0;
    if (!name) return 0;
    if (!strcmp(name,"_ZN2nn5audio24GetActiveAudioDeviceNameEPNS0_15AudioDeviceNameE")) { function=(uintptr_t)device_name;args=1; }
    else if (!strcmp(name,"_ZN2nn5audio41GetActiveAudioDeviceChannelCountForOutputEv")) { function=(uintptr_t)output_channels;rets=1; }
    else if (!strcmp(name,"_ZN2nn5audio39AcquireAudioDeviceNotificationForOutputEPNS_2os11SystemEventE")) { function=(uintptr_t)acquire_device_event;args=1; }
#define A(n,f,a,r) else if(!strcmp(name,n)){function=(uintptr_t)f;args=a;rets=r;}
    A("_ZN2nn5audio17GetSampleByteSizeENS0_12SampleFormatE",sample_byte_size,1,1)
    A("_ZN2nn5audio27InitializeAudioOutParameterEPNS0_17AudioOutParameterE",initialize_parameter,1,0)
    A("_ZN2nn5audio19OpenDefaultAudioOutEPNS0_8AudioOutERKNS0_17AudioOutParameterE",open_output,2,1)
    A("_ZN2nn5audio19OpenDefaultAudioOutEPNS0_8AudioOutEPNS_2os11SystemEventERKNS0_17AudioOutParameterE",open_output_event,3,1)
    A("_ZN2nn5audio13CloseAudioOutEPNS0_8AudioOutE",close_output,1,0)
    A("_ZN2nn5audio13StartAudioOutEPNS0_8AudioOutE",start_output,1,1)
    A("_ZN2nn5audio12StopAudioOutEPNS0_8AudioOutE",stop_output,1,0)
    A("_ZN2nn5audio21GetAudioOutSampleRateEPKNS0_8AudioOutE",get_rate,1,1)
    A("_ZN2nn5audio23GetAudioOutChannelCountEPKNS0_8AudioOutE",get_channels,1,1)
    A("_ZN2nn5audio23GetAudioOutSampleFormatEPKNS0_8AudioOutE",get_format,1,1)
    A("_ZN2nn5audio16GetAudioOutStateEPKNS0_8AudioOutE",get_state,1,1)
    A("_ZN2nn5audio15GetAudioOutNameEPKNS0_8AudioOutE",get_name,1,1)
    A("_ZN2nn5audio21SetAudioOutBufferInfoEPNS0_14AudioOutBufferEPvmm",buffer_info,4,0)
    A("_ZN2nn5audio28GetAudioOutBufferDataPointerEPKNS0_14AudioOutBufferE",buffer_data,1,1)
    A("_ZN2nn5audio25GetAudioOutBufferDataSizeEPKNS0_14AudioOutBufferE",buffer_size,1,1)
    A("_ZN2nn5audio27GetAudioOutBufferBufferSizeEPKNS0_14AudioOutBufferE",buffer_capacity,1,1)
    A("_ZN2nn5audio20AppendAudioOutBufferEPNS0_8AudioOutEPNS0_14AudioOutBufferE",append_buffer,2,1)
    A("_ZN2nn5audio25GetReleasedAudioOutBufferEPNS0_8AudioOutE",released_buffer,1,1)
#undef A
    if (function) { if (arguments) *arguments=args; if (returns) *returns=rets; }
    return function;
}
