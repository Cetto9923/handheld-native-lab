/* Android component contract. Uses real NDK decode and a caller-supplied IVF
 * VP9 fixture; no original-game bytes or encoded video embedded in source.
 * Raw function targets stand in for root's native-boundary gates ONLY in this
 * host-native component executable; this does not validate guest x18 gateways.
 * Usage: movie_decoder_contract fixture.ivf expected-first-frame.nv12
 * Optional reference is decoded from that same IVF, never its pre-encode input.
 */
#include "../../src/runtime/hle/movie_decoder.h"
#include "../../src/runtime/hle/movie_buffer.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <thread>
#include <vector>

extern "C" void nso_host_os_signal_event(void *p) {if(p)static_cast<std::atomic<unsigned>*>(p)->fetch_add(1);}
extern "C" int guest_process_interposed_free(void *p) {free(p);return 1;}
extern "C" uint64_t ffrs_movie_core_mask(void) {return 7;}
extern "C" int ffrs_movie_thread_priority(void) {return 23;}
struct B {int32_t offset,size,data32;uint32_t udata32;uint8_t owned,pad[7];uint64_t capacity,data64;void *base,*device,*sync;};
static void set_data(B *b,void *data,uint64_t bytes) {if(b->owned)abort();b->base=data;b->capacity=bytes;}
extern "C" uint64_t ffrs_movie_buffer_lookup(const char *name,unsigned *,unsigned *) {return !strcmp(name,"_ZN5movie6Buffer18SetDataAndCapacityEPvm")?reinterpret_cast<uintptr_t>(&set_data):0;}
struct M {void *impl;};struct D {void **vtable;void *impl;};
static int errors;
#define CHECK(e) do {if(!(e)){fprintf(stderr,"FAIL line%d: %s\n",__LINE__,#e);++errors;}} while(0)
template<class F> F named(const char *name) {auto p=ffrs_movie_decoder_lookup(name,nullptr,nullptr);if(!p){fprintf(stderr,"missing %s\n",name);abort();}return reinterpret_cast<F>(p);}
template<class F> F method(D &d,unsigned slot) {return reinterpret_cast<F>(d.vtable[slot]);}
static uint32_t le32(const uint8_t *p) {return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
static uint64_t le64(const uint8_t *p) {return le32(p)|(uint64_t(le32(p+4))<<32);}
static std::vector<uint8_t> read_file(const char *path) {std::ifstream f(path,std::ios::binary);return std::vector<uint8_t>(std::istreambuf_iterator<char>(f),{});}
struct Frame {std::vector<uint8_t> bytes;int64_t pts;};
static bool decode(D &d,const std::vector<Frame> &frames,const std::vector<uint8_t> &reference,int w,int h) {
    unsigned sent=0,received=0;bool input_eos=false,output_eos=false;size_t copied=0;
    std::vector<uint8_t> pixels(size_t(w)*h*3/2+32,0x6b);
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    while(!output_eos&&std::chrono::steady_clock::now()<deadline) {
        int index=-1,remaining=0;
        if(!input_eos){uint32_t r=method<uint32_t(*)(void*,int*,int*)>(d,9)(&d,&index,&remaining);
            if(!r){B b{};CHECK(method<uint32_t(*)(void*,int,B*)>(d,10)(&d,index,&b)==0);
                bool eos=sent==frames.size();size_t n=eos?0:frames[sent].bytes.size();
                CHECK(b.base&&n<=b.capacity);if(!b.base||n>b.capacity)break;
                if(n)memcpy(b.base,frames[sent].bytes.data(),n);
                CHECK(method<uint32_t(*)(void*,int,int,int,int64_t,uint32_t)>(d,11)(&d,index,0,int(n),eos?0:frames[sent].pts,eos?4u:0u)==0);
                if(eos)input_eos=true;else ++sent;
            }else CHECK(r==0x1011);
        }
        int64_t pts=0;uint32_t flags=0;
        uint32_t r=method<uint32_t(*)(void*,int*,int64_t*,uint32_t*,int*)>(d,12)(&d,&index,&pts,&flags,&remaining);
        if(!r){
            if(flags&4)output_eos=true;
            else {B b{};b.base=pixels.data()+16;b.capacity=pixels.size()-32;
                CHECK(method<uint32_t(*)(void*,int,B*)>(d,13)(&d,index,&b)==0);
                CHECK(b.offset==0&&size_t(b.size)==size_t(w)*h*3/2);
                for(unsigned i=0;i<16;i++){CHECK(pixels[i]==0x6b);CHECK(pixels[pixels.size()-1-i]==0x6b);}
                bool all_reference=reference.size()==frames.size()*size_t(w)*h*3/2;
                if(!reference.empty()&&(received==0||all_reference)){
                    size_t offset=all_reference?size_t(received)*size_t(b.size):0;
                    CHECK(offset<=reference.size()&&size_t(b.size)<=reference.size()-offset);
                    if(offset<=reference.size()&&size_t(b.size)<=reference.size()-offset){size_t differences=0;for(int i=0;i<b.size;i++)if(static_cast<uint8_t*>(b.base)[i]!=reference[offset+size_t(i)])++differences;
                        printf("frame=%u decoded reference differences=%zu bytes=%d\n",received,differences,b.size);CHECK(differences==0);}}
                CHECK(received<frames.size());if(received<frames.size())CHECK(pts==frames[received].pts);
                copied+=size_t(b.size);++received;
            }
            CHECK(method<uint32_t(*)(void*,int)>(d,15)(&d,index)==0);
        }else CHECK(r==0x100e);
        if(errors)break;std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(output_eos);CHECK(received==frames.size());CHECK(copied>0);
    printf("decode sent=%u received=%u eos=%d copied=%zu\n",sent,received,output_eos,copied);return !errors;
}
int main(int argc,char **argv) {
    if(argc<2){fprintf(stderr,"usage: %s fixture.ivf [expected-first.nv12]\n",argv[0]);return 2;}
    std::vector<uint8_t> file=read_file(argv[1]),reference=argc>2?read_file(argv[2]):std::vector<uint8_t>{};
    if(argc>2&&reference.empty()){fprintf(stderr,"reference file missing/empty\n");return 2;}
    if(file.size()<32||memcmp(file.data(),"DKIF",4)||memcmp(file.data()+8,"VP90",4)){fprintf(stderr,"not VP9 IVF\n");return 2;}
    int w=file[12]|file[13]<<8,h=file[14]|file[15]<<8;uint32_t rate=le32(file.data()+16),scale=le32(file.data()+20);
    if(!w||!h||!rate||!scale)return 2;
    std::vector<Frame> frames;size_t at=file[6]|size_t(file[7])<<8;
    while(at+12<=file.size()){uint32_t bytes=le32(file.data()+at);uint64_t tick=le64(file.data()+at+4);at+=12;if(bytes>file.size()-at)return 2;
        frames.push_back({std::vector<uint8_t>(file.begin()+at,file.begin()+at+bytes),int64_t(tick*1000000*scale/rate)});at+=bytes;}
    if(at!=file.size()||frames.empty()||frames.size()>120)return 2;
    auto ctor=named<void(*)(M*)>("_ZN5movie9MediaDataC1Ev"),dtor=named<void(*)(M*)>("_ZN5movie9MediaDataD1Ev");
    auto find32=named<bool(*)(const M*,const char*,int32_t*)>("_ZNK5movie9MediaData9FindInt32EPKcPi");
    auto set32=named<void(*)(M*,const char*,int32_t)>("_ZN5movie9MediaData8SetInt32EPKci");
    struct {uint64_t pre;M m;uint64_t post;} guarded{0x12345678,{},0xabcdef};ctor(&guarded.m);
    int32_t unchanged=123;CHECK(!find32(&guarded.m,"absent",&unchanged)&&unchanged==123);
    auto vp9=named<bool(*)(int,int,int,M*)>("_ZN5movie33CreateVp9DecoderConfigurationDataEiiiPNS_9MediaDataE");
    CHECK(vp9(w,h,int(uint64_t(rate)*1000/scale),&guarded.m));CHECK(find32(&guarded.m,"width",&unchanged)&&unchanged==w);
    M copy{};named<void(*)(M*,const M*)>("_ZN5movie9MediaDataC1ERKS0_")(&copy,&guarded.m);set32(&guarded.m,"width",w+2);CHECK(find32(&copy,"width",&unchanged)&&unchanged==w);
    CHECK(guarded.pre==0x12345678&&guarded.post==0xabcdef);dtor(&guarded.m);
    size_t count=0;auto *defs=ffrs_movie_decoder_virtuals(&count);CHECK(count==19);std::vector<void*> gates;for(size_t i=0;i<count;i++)gates.push_back(reinterpret_cast<void*>(defs[i].target));
    CHECK(ffrs_movie_decoder_publish_virtuals(gates.data(),gates.size())==0);
    D decoder{};named<void(*)(D*,int,int,int,int)>("_ZN5movie7DecoderC1ENS_11DecoderTypeENS_19DecoderOutputFormatENS_11DecoderModeEi")(&decoder,3,4,1,23);
    CHECK(ffrs_movie_decoder_publish_virtuals(nullptr,0)==-1);
    std::atomic<unsigned> events[4]{};void *event_pointers[4]={events,events+1,events+2,events+3};
    CHECK(method<uint32_t(*)(void*,void*const*)>(decoder,3)(&decoder,event_pointers)==0);
    CHECK(method<uint32_t(*)(void*,const M*)>(decoder,2)(&decoder,&copy)==0);
    int bytes=0,aux=0;CHECK(method<uint32_t(*)(void*,int*,int*)>(decoder,14)(&decoder,&bytes,&aux)==0);CHECK(bytes==w*h*3/2&&aux==1);
    CHECK(method<uint32_t(*)(void*)>(decoder,5)(&decoder)==0);
    if(!errors)decode(decoder,frames,reference,w,h);
    if(!errors){CHECK(method<uint32_t(*)(void*)>(decoder,8)(&decoder)==0);if(!errors)decode(decoder,frames,reference,w,h);}
    M format{};ctor(&format);CHECK(method<uint32_t(*)(void*,M*)>(decoder,4)(&decoder,&format)==0);
    CHECK(find32(&format,"nv12-y-stride",&unchanged)&&unchanged==w);CHECK(find32(&format,"nv12-uv-offset",&unchanged)&&unchanged==w*h);dtor(&format);
    method<void(*)(void*)>(decoder,0)(&decoder);CHECK(ffrs_movie_decoder_publish_virtuals(nullptr,0)==0);dtor(&copy);
    CHECK(events[0].load()>0&&events[1].load()>0&&events[2].load()>0&&events[3].load()==0);
    printf("movie_decoder_contract errors=%d fixture_frames=%zu input_events=%u output_events=%u format_events=%u error_events=%u\n",errors,frames.size(),events[0].load(),events[1].load(),events[2].load(),events[3].load());return errors!=0;
}
