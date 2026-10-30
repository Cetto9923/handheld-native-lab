/* Native Android movie ABI bridge. Input subsdk0 SHA256:
 * 50c488fb55d497b2a50b75fb8dee6ef6e8eccc31cd45510b580011490014a058.
 * Decoder VA124684/vtable523988, MediaData1281ac, VP9 configuration129570.
 * main5e1453c uses real CRI demux -> VP9(type3), NV12(out4), mode1.
 * No dummy frame, synthetic EOF, skipped movie, or guest instruction engine.
 */
#include "movie_decoder.h"
#include "movie_buffer.h"
#include "nso_host.h"
#include "guest_process.h"
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <algorithm>
#include <array>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
constexpr uint32_t OK=0,UNKNOWN=0x1000,STATE=0x1006,SHORT_BUFFER=0x100a,
    ARGUMENT=0x100b,CONFIGURE=0x100c,NO_OUTPUT=0x100e,NO_INPUT=0x1011;
enum Kind { I32=0,I64=1,SIZE=2,FLOAT=3,DOUBLE=4,POINTER=5,STRING=6,RECT=7,BYTES=8 };
struct Value {Kind kind=I32;int64_t number=0;std::string text;std::array<int,4> rect{};std::vector<uint8_t> bytes;};
struct Metadata {std::map<std::string,Value> values;};
struct MediaData {Metadata *impl;};
static_assert(sizeof(MediaData)==8,"MediaData object ABI");
void md_ctor(MediaData *m) {m->impl=new Metadata;}
void md_dtor(MediaData *m) {if(m)delete m->impl;}
void md_clear(MediaData *m) {if(m&&m->impl)m->impl->values.clear();}
void md_copy(MediaData *m,const MediaData *from) {m->impl=new Metadata;if(from&&from->impl)*m->impl=*from->impl;}
MediaData *md_assign(MediaData *m,const MediaData *from) {if(m&&m!=from&&m->impl&&from&&from->impl)*m->impl=*from->impl;return m;}
Value *md_put(MediaData *m,const char *key,Kind kind) {
    if(!m||!m->impl||!key)return nullptr;Value &v=m->impl->values[key];v=Value{};v.kind=kind;return &v;
}
const Value *md_get(const MediaData *m,const char *key,Kind kind) {
    if(!m||!m->impl||!key)return nullptr;auto i=m->impl->values.find(key);
    return i!=m->impl->values.end()&&i->second.kind==kind?&i->second:nullptr;
}
void md_i32(MediaData *m,const char *key,int32_t value) {if(auto *v=md_put(m,key,I32))v->number=value;}
void md_i64(MediaData *m,const char *key,int64_t value) {if(auto *v=md_put(m,key,I64))v->number=value;}
void md_string(MediaData *m,const char *key,const char *s) {if(!s)return;if(auto *v=md_put(m,key,STRING))v->text=s;}
void md_rect(MediaData *m,const char *key,int a,int b,int c,int d) {if(auto *v=md_put(m,key,RECT))v->rect={a,b,c,d};}
bool md_find32(const MediaData *m,const char *key,int32_t *out) {auto *v=md_get(m,key,I32);if(!v)return false;if(out)*out=int32_t(v->number);return true;}
bool md_find64(const MediaData *m,const char *key,int64_t *out) {auto *v=md_get(m,key,I64);if(!v)return false;if(out)*out=v->number;return true;}
bool md_findstring(const MediaData *m,const char *key,const char **out) {auto *v=md_get(m,key,STRING);if(!v)return false;if(out)*out=v->text.c_str();return true;}
bool md_findrect(const MediaData *m,const char *key,int *a,int *b,int *c,int *d) {auto *v=md_get(m,key,RECT);if(!v)return false;if(a)*a=v->rect[0];if(b)*b=v->rect[1];if(c)*c=v->rect[2];if(d)*d=v->rect[3];return true;}
void md_bytes(MediaData *m,const char *key,const std::vector<uint8_t> &b) {if(auto *v=md_put(m,key,BYTES))v->bytes=b;}
int i32(const Metadata &m,const char *key,int fallback=0) {auto i=m.values.find(key);return i!=m.values.end()&&i->second.kind==I32?int(i->second.number):fallback;}
void format_read(Metadata &m,AMediaFormat *f) {
    if(!f)return;MediaData wrapper{&m};
    const char *keys[]={"width","height","stride","slice-height","color-format","crop-left","crop-top","crop-right","crop-bottom","color-range","color-standard","color-transfer"};
    for(auto *k:keys){int32_t v;if(AMediaFormat_getInt32(f,k,&v))md_i32(&wrapper,k,v);}
    int w=i32(m,"width"),h=i32(m,"height");
    int32_t left=0,top=0,right=w-1,bottom=h-1;
    if(!AMediaFormat_getRect(f,"crop",&left,&top,&right,&bottom)){
        AMediaFormat_getInt32(f,"crop-left",&left);AMediaFormat_getInt32(f,"crop-top",&top);
        AMediaFormat_getInt32(f,"crop-right",&right);AMediaFormat_getInt32(f,"crop-bottom",&bottom);
    }
    md_rect(&wrapper,"crop",left,top,right,bottom);
}
AMediaFormat *format_make(const Metadata &m) {
    AMediaFormat *f=AMediaFormat_new();if(!f)return nullptr;
    for(auto &p:m.values){auto &v=p.second;
        if(v.kind==I32){
            /* Nintendo movie/CRI pass milli-frames per second, main5e177cc. */
            if(p.first=="frame-rate")AMediaFormat_setFloat(f,p.first.c_str(),float(v.number)/1000.0f);
            else AMediaFormat_setInt32(f,p.first.c_str(),int32_t(v.number));
        } else if(v.kind==I64)AMediaFormat_setInt64(f,p.first.c_str(),v.number);
        else if(v.kind==STRING)AMediaFormat_setString(f,p.first.c_str(),v.text.c_str());
        else if(v.kind==BYTES)AMediaFormat_setBuffer(f,p.first.c_str(),v.bytes.data(),v.bytes.size());
    }return f;
}
struct Buffer {int32_t offset,size,data32;uint32_t udata32;uint8_t owned,pad[7];uint64_t capacity,data64;void *base,*device,*sync;};
static_assert(sizeof(Buffer)==64&&offsetof(Buffer,base)==0x28,"Buffer ABI");
struct Output {AMediaCodecBufferInfo info{};Metadata format;bool acquired=false;};
struct DecoderImpl {
    std::mutex mutex;std::recursive_mutex api_mutex;
    AMediaCodec *codec=nullptr;int type,output,mode,priority;
    bool stopping=false,started=false;uint32_t error=0;int native_error=0;
    std::array<void*,4> events{};Metadata configuration,format;
    std::deque<int> available_inputs,available_outputs;
    std::set<int> acquired_inputs;std::map<int,Output> outputs;
    DecoderImpl(int t,int o,int m,int p):type(t),output(o),mode(m),priority(p){}
};
struct Decoder {void **vtable;DecoderImpl *impl;};
static_assert(sizeof(Decoder)==16,"Decoder object ABI");
std::mutex virtual_mutex;void *virtual_gates[19]{};size_t live_decoders=0;
std::mutex diagnostics_mutex;std::string diagnostics_directory;size_t diagnostic_bytes=0;unsigned diagnostic_chunks=0;
void diagnose_input(DecoderImpl *d,const uint8_t *data,size_t n,int64_t pts,uint32_t flags) {
    std::lock_guard<std::mutex> lock(diagnostics_mutex);
    if(diagnostics_directory.empty()||!n||diagnostic_chunks>=4||n>8u*1024u*1024u-diagnostic_bytes)return;
    char leaf[80];snprintf(leaf,sizeof leaf,"input-%u-type%d.bin",diagnostic_chunks,d->type);
    std::string path=diagnostics_directory+"/"+leaf;
    int fd=open(path.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0600);if(fd<0)return;
    size_t at=0;while(at<n){ssize_t z=write(fd,data+at,n-at);if(z<=0)break;at+=size_t(z);}close(fd);
    std::string index=diagnostics_directory+"/input-index.txt";
    int ix=open(index.c_str(),O_WRONLY|O_APPEND|O_CREAT|O_CLOEXEC,0600);
    if(ix>=0){dprintf(ix,"file=%s requested=%zu written=%zu pts=%lld flags=%u width=%d height=%d output=%d\n",leaf,n,at,(long long)pts,flags,i32(d->format,"width"),i32(d->format,"height"),d->output);close(ix);}
    diagnostic_bytes+=at;++diagnostic_chunks;
}
/* subsdk123528/40/58/70 -> PLT34b550 -> symbol290 SignalEvent(EventType*).
 * These are the client's ordinary manual/auto-reset events, not SystemEvent. */
void signal(DecoderImpl *d,unsigned event) {if(d->events[event])nso_host_os_signal_event(d->events[event]);}
void callback_input(AMediaCodec *,void *user,int32_t index) {
    auto *d=static_cast<DecoderImpl*>(user);std::lock_guard<std::mutex> l(d->mutex);
    if(d->stopping)return;d->available_inputs.push_back(index);signal(d,0);
}
void callback_output(AMediaCodec *,void *user,int32_t index,AMediaCodecBufferInfo *info) {
    auto *d=static_cast<DecoderImpl*>(user);std::lock_guard<std::mutex> l(d->mutex);
    if(d->stopping)return;d->outputs[index]=Output{*info,d->format,false};d->available_outputs.push_back(index);signal(d,1);
}
void callback_format(AMediaCodec *,void *user,AMediaFormat *format) {
    auto *d=static_cast<DecoderImpl*>(user);std::lock_guard<std::mutex> l(d->mutex);
    if(d->stopping)return;format_read(d->format,format);
    fprintf(stderr,"[movie] actual output format: %s\n",AMediaFormat_toString(format));signal(d,2);
}
void callback_error(AMediaCodec *,void *user,media_status_t error,int32_t action,const char *detail) {
    auto *d=static_cast<DecoderImpl*>(user);std::lock_guard<std::mutex> l(d->mutex);
    if(d->stopping)return;d->error=UNKNOWN;d->native_error=error;
    fprintf(stderr,"[movie] actual codec error=%d action=%d detail=%s\n",error,action,detail?detail:"");signal(d,3);
}
constexpr AMediaCodecOnAsyncNotifyCallback callbacks={callback_input,callback_output,callback_format,callback_error};
/* Never hold d->mutex while draining the NDK internal callback thread. NDK
 * unregister/release guarantee no later callback before freeing userdata. */
void codec_close(DecoderImpl *d) {
    std::lock_guard<std::recursive_mutex> api(d->api_mutex);
    AMediaCodec *codec;bool started;
    {std::lock_guard<std::mutex> l(d->mutex);d->stopping=true;codec=d->codec;started=d->started;}
    if(codec){if(started)AMediaCodec_stop(codec);AMediaCodec_setAsyncNotifyCallback(codec,{},nullptr);AMediaCodec_delete(codec);}
    {std::lock_guard<std::mutex> l(d->mutex);d->codec=nullptr;d->started=false;d->available_inputs.clear();d->acquired_inputs.clear();d->available_outputs.clear();d->outputs.clear();}
}
void decoder_ctor(Decoder *self,int type,int output,int mode,int priority) {
    std::lock_guard<std::mutex> l(virtual_mutex);if(!self||!virtual_gates[0])abort();
    self->vtable=virtual_gates;self->impl=new DecoderImpl(type,output,mode,priority);++live_decoders;
    fprintf(stderr,"[movie] Decoder native bridge type=%d output=%d mode=%d priority=%d coreMask=%llu\n",type,output,mode,priority,(unsigned long long)ffrs_movie_core_mask());
}
void decoder_dtor(Decoder *self) {
    if(!self||!self->impl)return;codec_close(self->impl);delete self->impl;self->impl=nullptr;
    std::lock_guard<std::mutex> l(virtual_mutex);--live_decoders;
}
void decoder_delete(Decoder *self) {decoder_dtor(self);if(guest_process_interposed_free(self)!=1)abort();}
uint32_t configure(Decoder *self,const MediaData *metadata) {
    if(!self||!self->impl||!metadata||!metadata->impl)return ARGUMENT;
    auto *d=self->impl;std::lock_guard<std::recursive_mutex> api(d->api_mutex);
    const char *mime=d->type==3?"video/x-vnd.on2.vp9":d->type==1?"video/avc":nullptr;
    if(!mime||d->output!=4||d->mode!=1){fprintf(stderr,"[movie] unsupported decoder combination type=%d output=%d mode=%d\n",d->type,d->output,d->mode);return CONFIGURE;}
    int w=i32(*metadata->impl,"width"),h=i32(*metadata->impl,"height");
    if(w<=0||h<=0||w>8192||h>8192||(w&1)||(h&1))return ARGUMENT;
    codec_close(d);
    {std::lock_guard<std::mutex> l(d->mutex);d->configuration=*metadata->impl;d->format=*metadata->impl;d->error=0;d->native_error=0;d->stopping=false;}
    AMediaFormat *f=format_make(*metadata->impl);if(!f)return CONFIGURE;
    AMediaFormat_setString(f,"mime",mime);AMediaFormat_setInt32(f,"color-format",21);
    AMediaCodec *codec=AMediaCodec_createDecoderByType(mime);if(!codec){AMediaFormat_delete(f);return CONFIGURE;}
    char *codec_name=nullptr;
    if(AMediaCodec_getName(codec,&codec_name)==AMEDIA_OK&&codec_name){fprintf(stderr,"[movie] actual codec name=%s\n",codec_name);AMediaCodec_releaseName(codec,codec_name);}
    {std::lock_guard<std::mutex> l(d->mutex);d->codec=codec;}
    media_status_t r=AMediaCodec_setAsyncNotifyCallback(codec,callbacks,d);
    if(r==AMEDIA_OK)r=AMediaCodec_configure(codec,f,nullptr,nullptr,0);
    fprintf(stderr,"[movie] configure actual codec mime=%s %dx%d status=%d\n",mime,w,h,r);AMediaFormat_delete(f);
    if(r!=AMEDIA_OK){codec_close(d);return CONFIGURE;}return OK;
}
uint32_t register_events(Decoder *self,void *const *events) {
    if(!self||!self->impl||!events)return ARGUMENT;auto *d=self->impl;std::lock_guard<std::mutex> l(d->mutex);
    std::copy(events,events+4,d->events.begin());if(!d->available_inputs.empty())signal(d,0);if(!d->available_outputs.empty())signal(d,1);return OK;
}
uint32_t start(Decoder *self) {
    if(!self||!self->impl)return STATE;auto *d=self->impl;
    std::lock_guard<std::recursive_mutex> api(d->api_mutex);if(!d->codec)return STATE;
    media_status_t r=AMediaCodec_start(d->codec);{std::lock_guard<std::mutex> l(d->mutex);if(r==AMEDIA_OK)d->started=true;}
    fprintf(stderr,"[movie] start actual codec status=%d\n",r);return r==AMEDIA_OK?OK:STATE;
}
uint32_t stop(Decoder *self) {if(!self||!self->impl)return ARGUMENT;codec_close(self->impl);return OK;}
uint32_t reset(Decoder *self) {return stop(self);}
uint32_t flush(Decoder *self) {
    if(!self||!self->impl)return STATE;auto *d=self->impl;
    std::lock_guard<std::recursive_mutex> api(d->api_mutex);if(!d->codec)return STATE;
    /* Android rejects setCallback while executing. A bare flush also leaves
     * previously posted NDK callback messages without a generation token. Use
     * a real codec release/recreate boundary to discard queued frames safely.
     * codec_close keeps userdata alive until the old codec has been released;
     * only afterwards can configure publish the next generation. Playback PTS
     * remain caller-owned and the original configuration is copied unchanged. */
    Metadata saved;bool was_started;
    {std::lock_guard<std::mutex> l(d->mutex);saved=d->configuration;was_started=d->started;}
    MediaData metadata{&saved};uint32_t r=configure(self,&metadata);
    if(r==OK&&was_started)r=start(self);
    fprintf(stderr,"[movie] flush actual codec release/reconfigure started=%d status=%#x\n",was_started,r);
    return r;
}
void output_metadata(MediaData *out,const Metadata &m) {
    *out->impl=m;int w=i32(m,"width"),h=i32(m,"height");md_i32(out,"stride",w);
    if(!md_get(out,"crop",RECT))md_rect(out,"crop",0,0,w-1,h-1);
    /* main5e15928 consumes these exact plane descriptors. They describe the
     * tight NV12 copy, rather than MediaCodec's source allocation padding. */
    md_i32(out,"nv12-y-stride",w);md_i32(out,"nv12-y-offset",0);
    md_i32(out,"nv12-uv-stride",w);md_i32(out,"nv12-uv-offset",w*h);
    auto *crop=md_get(out,"crop",RECT);
    md_i32(out,"crop-width",crop->rect[2]-crop->rect[0]+1);
    md_i32(out,"crop-height",crop->rect[3]-crop->rect[1]+1);
}
uint32_t output_format(Decoder *self,MediaData *out) {
    if(!self||!self->impl||!out||!out->impl)return ARGUMENT;std::lock_guard<std::mutex> l(self->impl->mutex);output_metadata(out,self->impl->format);return OK;
}
uint32_t acquire_input(Decoder *self,int *index,int *remaining) {
    if(!self||!self->impl||!index||!remaining)return ARGUMENT;auto *d=self->impl;std::lock_guard<std::mutex> l(d->mutex);
    *index=-1;*remaining=0;if(d->error)return d->error;if(d->available_inputs.empty())return NO_INPUT;
    *index=d->available_inputs.front();d->available_inputs.pop_front();d->acquired_inputs.insert(*index);*remaining=int(d->available_inputs.size());return OK;
}
uint32_t input_buffer(Decoder *self,int index,Buffer *out) {
    if(!self||!self->impl||!out)return ARGUMENT;auto *d=self->impl;
    std::lock_guard<std::recursive_mutex> api(d->api_mutex);std::lock_guard<std::mutex> l(d->mutex);
    if(!d->codec||!d->acquired_inputs.count(index))return ARGUMENT;
    size_t n=0;uint8_t *bytes=AMediaCodec_getInputBuffer(d->codec,size_t(index),&n);if(!bytes||n>INT_MAX)return UNKNOWN;
    using Set=void(*)(void*,void*,uint64_t);auto fn=reinterpret_cast<Set>(ffrs_movie_buffer_lookup("_ZN5movie6Buffer18SetDataAndCapacityEPvm",nullptr,nullptr));
    if(!fn)return UNKNOWN;fn(out,bytes,n);out->offset=0;out->size=int(n);return OK;
}
uint32_t send_input(Decoder *self,int index,int offset,int size,int64_t pts,uint32_t flags) {
    if(!self||!self->impl||offset<0||size<0)return ARGUMENT;auto *d=self->impl;
    std::lock_guard<std::recursive_mutex> api(d->api_mutex);AMediaCodec *codec;
    {std::lock_guard<std::mutex> l(d->mutex);codec=d->codec;if(!codec||!d->acquired_inputs.count(index))return ARGUMENT;
        size_t n=0;uint8_t *p=AMediaCodec_getInputBuffer(codec,size_t(index),&n);if(!p||size_t(offset)>n||size_t(size)>n-size_t(offset))return ARGUMENT;
        diagnose_input(d,p+offset,size_t(size),pts,flags);d->acquired_inputs.erase(index);}
    media_status_t r=AMediaCodec_queueInputBuffer(codec,size_t(index),offset,size,pts,flags);
    if(r!=AMEDIA_OK){std::lock_guard<std::mutex> l(d->mutex);d->error=UNKNOWN;d->native_error=r;signal(d,3);fprintf(stderr,"[movie] queue input failed status=%d index=%d bytes=%d\n",r,index,size);return UNKNOWN;}return OK;
}
uint32_t acquire_output(Decoder *self,int *index,int64_t *pts,uint32_t *flags,int *remaining) {
    if(!self||!self->impl||!index||!pts||!flags||!remaining)return ARGUMENT;auto *d=self->impl;std::lock_guard<std::mutex> l(d->mutex);
    *index=-1;*pts=0;*flags=0;*remaining=0;if(d->error)return d->error;if(d->available_outputs.empty())return NO_OUTPUT;
    *index=d->available_outputs.front();d->available_outputs.pop_front();auto &o=d->outputs.at(*index);o.acquired=true;*pts=o.info.presentationTimeUs;*flags=o.info.flags;*remaining=int(d->available_outputs.size());return OK;
}
uint32_t buffer_size(Decoder *self,int *bytes,int *auxiliary) {
    if(!self||!self->impl||!bytes||!auxiliary)return ARGUMENT;std::lock_guard<std::mutex> l(self->impl->mutex);
    int w=i32(self->impl->format,"width"),h=i32(self->impl->format,"height");if(w<=0||h<=0)return STATE;
    /* Original VA122470..122484 only accepts mode1 and writes 1 to out2. */
    uint64_t n=uint64_t(w)*h+2ull*(w/2)*(h/2);if(n>INT_MAX)return ARGUMENT;*bytes=int(n);*auxiliary=1;return OK;
}
/* Only verified standard linear layouts. Vendor/flexible opaque layouts have
 * no trustworthy plane-stride description in this byte-buffer API. */
uint32_t copy_nv12(const Metadata &m,const uint8_t *src,size_t available,uint8_t *dst,size_t capacity,size_t &written) {
    int w=i32(m,"width"),h=i32(m,"height"),stride=i32(m,"stride",w),slice=i32(m,"slice-height",h),color=i32(m,"color-format");
    written=0;if(w<=0||h<=0||(w&1)||(h&1)||stride<w||slice<h||stride>32768||slice>32768)return ARGUMENT;
    size_t y=static_cast<size_t>(stride)*slice,n=static_cast<size_t>(w)*h*3/2;
    size_t ch_stride=color==19?(static_cast<size_t>(stride)+1)/2:stride,ch_height=(static_cast<size_t>(slice)+1)/2;
    if(color!=19&&color!=21){fprintf(stderr,"[movie] unsupported actual output layout color=%#x stride=%d slice=%d\n",color,stride,slice);return UNKNOWN;}
    /* The decoder's advertised range need only contain the bytes actually
     * read: padding after the last copied row is not accessed. Planar V still
     * starts after the entire allocated U plane, including its row padding. */
    size_t y_end=static_cast<size_t>(h-1)*stride+w;
    size_t chroma_end=color==19?
        y+ch_stride*ch_height+static_cast<size_t>(h/2-1)*ch_stride+w/2:
        y+static_cast<size_t>(h/2-1)*stride+w;
    size_t source_bytes=std::max(y_end,chroma_end);
    if(capacity<n||available<source_bytes)return SHORT_BUFFER;
    if(stride==w && (color==21 || color==19)){
        memcpy(dst,src,static_cast<size_t>(w)*h);
        uint8_t *uv=dst+static_cast<size_t>(w)*h;const uint8_t *u=src+y;
        if(color==21){memcpy(uv,u,static_cast<size_t>(w)*(h/2));}
        else {const uint8_t *v=u+ch_stride*ch_height;for(int row=0;row<h/2;row++)for(int col=0;col<w/2;col++){
            uv[static_cast<size_t>(row)*w+col*2]=u[static_cast<size_t>(row)*ch_stride+col];uv[static_cast<size_t>(row)*w+col*2+1]=v[static_cast<size_t>(row)*ch_stride+col];}}
    } else {
        for(int row=0;row<h;row++)memcpy(dst+static_cast<size_t>(row)*w,src+static_cast<size_t>(row)*stride,w);
        uint8_t *uv=dst+static_cast<size_t>(w)*h;const uint8_t *u=src+y;
        if(color==21){for(int row=0;row<h/2;row++)memcpy(uv+static_cast<size_t>(row)*w,u+static_cast<size_t>(row)*stride,w);}
        else {const uint8_t *v=u+ch_stride*ch_height;for(int row=0;row<h/2;row++)for(int col=0;col<w/2;col++){
            uv[static_cast<size_t>(row)*w+col*2]=u[static_cast<size_t>(row)*ch_stride+col];uv[static_cast<size_t>(row)*w+col*2+1]=v[static_cast<size_t>(row)*ch_stride+col];}}
    }
    written=n;return OK;
}
uint32_t get_output(Decoder *self,int index,Buffer *out) {
    if(!self||!self->impl||!out)return ARGUMENT;auto *d=self->impl;
    std::lock_guard<std::recursive_mutex> api(d->api_mutex);std::lock_guard<std::mutex> l(d->mutex);
    out->offset=out->size=0;auto i=d->outputs.find(index);if(!d->codec||i==d->outputs.end()||!i->second.acquired)return ARGUMENT;
    auto &o=i->second;if(o.info.offset<0||o.info.size<0)return UNKNOWN;
    size_t n=0;uint8_t *p=AMediaCodec_getOutputBuffer(d->codec,size_t(index),&n);
    if(size_t(o.info.offset)>n||size_t(o.info.size)>n-size_t(o.info.offset))return UNKNOWN;
    if(!o.info.size)return OK;
    if(!p||!out->base)return UNKNOWN;
    AMediaFormat *f=AMediaCodec_getBufferFormat(d->codec,size_t(index));if(f){format_read(o.format,f);AMediaFormat_delete(f);}
    size_t written=0;uint32_t r=copy_nv12(o.format,p+o.info.offset,size_t(o.info.size),static_cast<uint8_t*>(out->base),out->capacity,written);
    if(r==OK)out->size=int32_t(written);else fprintf(stderr,"[movie] output copy failed status=%#x index=%d codecBytes=%zu infoBytes=%d capacity=%llu\n",r,index,n,o.info.size,(unsigned long long)out->capacity);return r;
}
uint32_t release_output(Decoder *self,int index) {
    if(!self||!self->impl)return ARGUMENT;auto *d=self->impl;
    std::lock_guard<std::recursive_mutex> api(d->api_mutex);AMediaCodec *codec;
    {std::lock_guard<std::mutex> l(d->mutex);auto i=d->outputs.find(index);if(i==d->outputs.end()||!i->second.acquired)return ARGUMENT;codec=d->codec;d->outputs.erase(i);}
    return codec&&AMediaCodec_releaseOutputBuffer(codec,size_t(index),false)==AMEDIA_OK?OK:UNKNOWN;
}
uint32_t last_error(Decoder *self) {if(!self||!self->impl)return ARGUMENT;std::lock_guard<std::mutex> l(self->impl->mutex);return self->impl->error;}
uint32_t buffer_format(Decoder *self,int index,MediaData *out) {
    if(!self||!self->impl||!out||!out->impl)return ARGUMENT;auto *d=self->impl;std::lock_guard<std::mutex> l(d->mutex);
    auto i=d->outputs.find(index);if(i==d->outputs.end())return ARGUMENT;output_metadata(out,i->second.format);return OK;
}
uint32_t output_with_format(Decoder *self,int index,Buffer *out,MediaData *format) {uint32_t r=get_output(self,index,out);return r?r:buffer_format(self,index,format);}
bool vp9_configuration(int w,int h,int fps,MediaData *out) {
    if(!out||!out->impl)return false;md_string(out,"mime","video/x-vnd.on2.vp9");md_i32(out,"width",w);md_i32(out,"height",h);md_i32(out,"frame-rate",fps);
    if(w>0&&h>0&&uint64_t(w)*h<=INT_MAX)md_i32(out,"max-input-size",w*h);return true;
}
/* main5e16f44 receives NAL bytes without the 3/4-byte Annex-B start code. */
bool h264_next(uint8_t **cursor,size_t *remaining,uint8_t **nal,size_t *nal_size) {
    if(!cursor||!remaining||!nal||!nal_size||!*cursor)return false;
    auto prefix=[](const uint8_t *p,size_t n)->size_t {if(n>=4&&p[0]==0&&p[1]==0&&p[2]==0&&p[3]==1)return 4;if(n>=3&&p[0]==0&&p[1]==0&&p[2]==1)return 3;return 0;};
    uint8_t *p=*cursor;size_t n=*remaining,begin=0,start=0;
    while(begin<n&&!(start=prefix(p+begin,n-begin)))++begin;
    if(!start){*cursor=p+n;*remaining=0;return false;}
    size_t payload=begin+start,end=payload;while(end<n&&!prefix(p+end,n-end))++end;
    *nal=p+payload;*nal_size=end-payload;*cursor=p+end;*remaining=n-end;return *nal_size!=0;
}
struct Bits {
    std::vector<uint8_t> data;size_t bit=0;bool good=true;
    explicit Bits(const uint8_t *p,size_t n){unsigned zeros=0;for(size_t i=1;i<n;i++){if(zeros>=2&&p[i]==3){zeros=0;continue;}data.push_back(p[i]);zeros=p[i]==0?zeros+1:0;}}
    uint32_t read(unsigned n){if(n>32||bit+n>data.size()*8){good=false;return 0;}uint32_t v=0;while(n--){v=(v<<1)|((data[bit/8]>>(7-bit%8))&1);++bit;}return v;}
    uint32_t ue(){unsigned zeros=0;while(good&&!read(1)){if(++zeros>30){good=false;return 0;}}return good?((1u<<zeros)-1+read(zeros)):0;}
    int32_t se(){uint32_t n=ue();return n&1?int32_t((n+1)/2):-int32_t(n/2);}
};
bool h264_dimensions(const uint8_t *sps,size_t size,int &width,int &height) {
    if(!sps||size<4||(sps[0]&31)!=7)return false;Bits b(sps,size);unsigned profile=b.read(8);b.read(16);b.ue();unsigned chroma=1;
    if(profile==100||profile==110||profile==122||profile==244||profile==44||profile==83||profile==86||profile==118||profile==128||profile==138||profile==139||profile==134||profile==135){
        chroma=b.ue();if(chroma>3)return false;if(chroma==3)b.read(1);b.ue();b.ue();b.read(1);
        if(b.read(1))for(unsigned i=0;i<(chroma==3?12u:8u);i++)if(b.read(1)){int last=8,next=8;for(unsigned j=0;j<(i<6?16u:64u);j++){if(next)next=(last+b.se()+256)%256;last=next?next:last;}}
    }
    b.ue();unsigned order=b.ue();if(order==0)b.ue();else if(order==1){b.read(1);b.se();b.se();unsigned n=b.ue();if(n>256)return false;while(n--)b.se();}else if(order>2)return false;
    b.ue();b.read(1);uint32_t w=b.ue(),h=b.ue(),frame=b.read(1);if(!frame)b.read(1);b.read(1);
    uint32_t left=0,right=0,top=0,bottom=0;if(b.read(1)){left=b.ue();right=b.ue();top=b.ue();bottom=b.ue();}
    if(!b.good||w>=512||h>=512)return false;
    unsigned cropx=chroma==0||chroma==3?1:2,cropy=(chroma==1?2:1)*(2-frame);
    int64_t ww=int64_t(w+1)*16-int64_t(left+right)*cropx,hh=int64_t(h+1)*16*(2-frame)-int64_t(top+bottom)*cropy;
    if(ww<=0||hh<=0||ww>8192||hh>8192)return false;width=int(ww);height=int(hh);return true;
}
bool h264_configuration(const uint8_t *sps,size_t sps_size,const uint8_t *pps,size_t pps_size,MediaData *out) {
    int w,h;if(!out||!out->impl||!pps||!pps_size||(pps[0]&31)!=8||!h264_dimensions(sps,sps_size,w,h))return false;
    md_string(out,"mime","video/avc");md_i32(out,"width",w);md_i32(out,"height",h);md_i32(out,"max-input-size",w*h);
    std::vector<uint8_t> s{0,0,0,1};s.insert(s.end(),sps,sps+sps_size);md_bytes(out,"csd-0",s);
    s={0,0,0,1};s.insert(s.end(),pps,pps+pps_size);md_bytes(out,"csd-1",s);return true;
}
#define V(fn,n,r) {reinterpret_cast<void(*)(void)>(fn),n,r}
const ffrs_movie_virtual methods[]={V(decoder_dtor,1,0),V(decoder_delete,1,0),V(configure,2,1),V(register_events,2,1),V(output_format,2,1),V(start,1,1),V(stop,1,1),V(reset,1,1),V(flush,1,1),V(acquire_input,3,1),V(input_buffer,3,1),V(send_input,6,1),V(acquire_output,5,1),V(get_output,3,1),V(buffer_size,3,1),V(release_output,2,1),V(last_error,1,1),V(output_with_format,4,1),V(buffer_format,3,1)};
#undef V
}

extern "C" const ffrs_movie_virtual *ffrs_movie_decoder_virtuals(size_t *count) {if(count)*count=sizeof(methods)/sizeof(methods[0]);return methods;}
extern "C" int ffrs_movie_decoder_publish_virtuals(void *const *gates,size_t count) {
    std::lock_guard<std::mutex> l(virtual_mutex);if(live_decoders)return -1;
    if(!gates&&!count){std::fill(std::begin(virtual_gates),std::end(virtual_gates),nullptr);return 0;}
    if(!gates||count!=19)return -1;for(size_t i=0;i<count;i++)if(!gates[i])return -1;
    std::copy(gates,gates+count,virtual_gates);return 0;
}
extern "C" int ffrs_movie_decoder_set_diagnostics(const char *directory) {
    std::lock_guard<std::mutex> l(diagnostics_mutex);if(!directory){diagnostics_directory.clear();return 0;}
    struct stat st;if(directory[0]!='/'||stat(directory,&st)||!S_ISDIR(st.st_mode))return -1;
    diagnostics_directory=directory;diagnostic_bytes=0;diagnostic_chunks=0;return 0;
}
extern "C" uint64_t ffrs_movie_decoder_lookup(const char *name,unsigned *arguments,unsigned *returns) {
    if(!name)return 0;struct Entry{const char *name;void(*fn)(void);unsigned args,ret;};
#define E(name,fn,a,r) {name,reinterpret_cast<void(*)(void)>(fn),a,r}
    static const Entry entries[]={
        E("_ZN5movie7DecoderC1ENS_11DecoderTypeENS_19DecoderOutputFormatENS_11DecoderModeEi",decoder_ctor,5,0),
        E("_ZN5movie9MediaDataC1Ev",md_ctor,1,0),E("_ZN5movie9MediaDataD1Ev",md_dtor,1,0),E("_ZN5movie9MediaData5ClearEv",md_clear,1,0),
        E("_ZN5movie9MediaDataC1ERKS0_",md_copy,2,0),E("_ZN5movie9MediaDataaSERKS0_",md_assign,2,1),
        E("_ZN5movie9MediaData8SetInt32EPKci",md_i32,3,0),E("_ZN5movie9MediaData8SetInt64EPKcl",md_i64,3,0),
        E("_ZNK5movie9MediaData9FindInt32EPKcPi",md_find32,3,1),E("_ZNK5movie9MediaData9FindInt64EPKcPl",md_find64,3,1),
        E("_ZNK5movie9MediaData10FindStringEPKcPS2_",md_findstring,3,1),E("_ZNK5movie9MediaData8FindRectEPKcPiS3_S3_S3_",md_findrect,6,1),
        E("_ZN5movie33CreateVp9DecoderConfigurationDataEiiiPNS_9MediaDataE",vp9_configuration,4,1),
        E("_ZN5movie34CreateH264DecoderConfigurationDataEPKhmS1_mPNS_9MediaDataE",h264_configuration,5,1),
        E("_ZN5movie37GetH264NalUnitStartPositionAndNalSizeEPPhPmS1_S2_",h264_next,4,1)};
#undef E
    for(auto &e:entries)if(!strcmp(name,e.name)){if(arguments)*arguments=e.args;if(returns)*returns=e.ret;return reinterpret_cast<uintptr_t>(e.fn);}return 0;
}
