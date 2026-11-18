#pragma once
#ifndef FFRS_API_BREADCRUMB_DIAGNOSTIC
#define FFRS_API_BREADCRUMB_DIAGNOSTIC 0
#endif
#if FFRS_API_BREADCRUMB_DIAGNOSTIC != 0 && FFRS_API_BREADCRUMB_DIAGNOSTIC != 1
#error "FFRS_API_BREADCRUMB_DIAGNOSTIC must be 0 or 1"
#endif
#include <atomic>
#include <mutex>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <cerrno>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#if defined(__ANDROID__)
#include <sys/syscall.h>
#endif
// Configure only at quiescent initialization, before warm workers. One pair per
// API category, one descriptor batch and one dispatch payload for the target.
// Never calls Vulkan or reads resource contents. At most 21 lines per session.
class XbgApiBreadcrumb {
public:
    enum Site { RuntimeModule, Descriptors, RuntimePipeline, WarmPipeline, Dispatch, Count };
    static constexpr const char *Target="073ce354a4f65e2f";
    static constexpr size_t DescriptorLimit=8;
    struct Descriptor {
        enum Kind { Other, Buffer, Image };
        uint32_t binding=0,element=0,type=0;
        Kind kind=Other;
        uint64_t offset=0,range=0,dynamic_offset=0;
        bool dynamic=false,image_metadata=false;
        uint32_t format=0,width=0,height=0,depth=0;
        uint32_t base_mip=0,mip_count=0,base_layer=0,layer_count=0;
        uint32_t view_type=0,aspects=0,usage=0;
    };
private:
    std::atomic<unsigned> seen{0};
    std::atomic<bool> descriptors_seen{false};
    std::mutex mu;
    FILE *file=nullptr;
    bool enabled=false;
    uint64_t sequence=0;
    static uint64_t monotonic_ns(){
        timespec now{};
        clock_gettime(CLOCK_MONOTONIC,&now);
        return uint64_t(now.tv_sec)*1000000000ull+uint64_t(now.tv_nsec);
    }
    static uint64_t tid(){
#if defined(__ANDROID__)
        return uint64_t(syscall(SYS_gettid));
#elif defined(__APPLE__)
        uint64_t id=0;pthread_threadid_np(nullptr,&id);return id;
#else
        return 0; // host contract test only, not Android evidence
#endif
    }
    static const char *name(Site s){
        static const char *names[]={"runtime_module","descriptors","runtime_pipeline","warm_pipeline","dispatch_record"};
        return names[unsigned(s)];
    }
    bool matches(const char *key) const {
        return enabled && key && std::strcmp(key,Target)==0;
    }
public:
    ~XbgApiBreadcrumb(){if(file)fclose(file);}
    void configure(bool on,const char *path){
        std::lock_guard<std::mutex> lock(mu);
        if(file)fclose(file);
        file=nullptr;enabled=false;seen=0;descriptors_seen=false;sequence=0;
        if(!on)return;
        // Preserve an incomplete preceding session if rotation fails.
        const std::string previous=std::string(path)+".prev";
        if(rename(path,previous.c_str())!=0 && errno!=ENOENT)return;
        file=fopen(path,"wb");enabled=file!=nullptr;
        if(enabled){
            const uint64_t ns=monotonic_ns();
            fprintf(file,"API_BREADCRUMB_SESSION v=1 pid=%llu init_monotonic_ns=%llu monotonic_ns=%llu target=%s max_lines=21 descriptor_limit=8\n",
                    (unsigned long long)getpid(),(unsigned long long)ns,(unsigned long long)ns,Target);
            fflush(file);
        }
    }
    uint64_t begin(const char *key,Site site,uint64_t frame,const uint32_t *words=nullptr,size_t count=0){
        if(!matches(key) || unsigned(site)>=Count)return 0;
        unsigned bit=1u<<unsigned(site);
        if(seen.load(std::memory_order_relaxed)&bit)return 0;
        if(seen.fetch_or(bit,std::memory_order_relaxed)&bit)return 0;
        const int saved=errno;
        std::lock_guard<std::mutex> lock(mu);
        const uint64_t seq=++sequence;
        uint64_t hash=0;
        if(words && count){
            hash=14695981039346656037ull;
            for(size_t i=0;i<count;++i)for(unsigned b=0;b<4;++b)
                hash=(hash^uint8_t(words[i]>>(8*b)))*1099511628211ull;
        }
        fprintf(file,"API_BREADCRUMB v=1 seq=%llu phase=begin tid=%llu monotonic_ns=%llu frame=%llu shader=%s site=%s words=%zu fnv64=%016llx\n",
                (unsigned long long)seq,(unsigned long long)tid(),(unsigned long long)monotonic_ns(),
                (unsigned long long)frame,Target,name(site),count,(unsigned long long)hash);
        fflush(file);errno=saved;return seq;
    }
    void end(uint64_t seq,Site site,uint64_t frame,int result){
        if(!seq)return;
        const int saved=errno;
        std::lock_guard<std::mutex> lock(mu);
        fprintf(file,"API_BREADCRUMB v=1 seq=%llu phase=end tid=%llu monotonic_ns=%llu frame=%llu shader=%s site=%s result=%d\n",
                (unsigned long long)seq,(unsigned long long)tid(),(unsigned long long)monotonic_ns(),
                (unsigned long long)frame,Target,name(site),result);
        fflush(file);errno=saved;
    }
    bool wants_descriptors(const char *key) const {
        return matches(key) && !descriptors_seen.load(std::memory_order_relaxed);
    }
    // Called only after a real descriptor update returned. Cache hits do not
    // consume the batch. total exposes truncation without logging more entries.
    void descriptor_write(const char *key,uint64_t frame,const Descriptor *entries,size_t count,size_t total){
        if(!wants_descriptors(key) || !entries || !count)return;
        if(descriptors_seen.exchange(true,std::memory_order_relaxed))return;
        const int saved=errno;
        std::lock_guard<std::mutex> lock(mu);
        const size_t captured=count<DescriptorLimit?count:DescriptorLimit;
        fprintf(file,"API_DESCRIPTOR_WRITE v=1 tid=%llu monotonic_ns=%llu frame=%llu shader=%s total=%zu captured=%zu\n",
                (unsigned long long)tid(),(unsigned long long)monotonic_ns(),(unsigned long long)frame,Target,total,captured);
        for(size_t i=0;i<captured;++i){
            const Descriptor &d=entries[i];
            fprintf(file,"API_DESCRIPTOR v=1 tid=%llu monotonic_ns=%llu frame=%llu shader=%s binding=%u element=%u type=%u kind=%u",
                    (unsigned long long)tid(),(unsigned long long)monotonic_ns(),(unsigned long long)frame,Target,d.binding,d.element,d.type,unsigned(d.kind));
            if(d.kind==Descriptor::Buffer)
                fprintf(file," offset=%llu range=%llu dynamic=%u dynamic_offset=%llu",
                        (unsigned long long)d.offset,(unsigned long long)d.range,unsigned(d.dynamic),(unsigned long long)d.dynamic_offset);
            if(d.kind==Descriptor::Image){
                fprintf(file," metadata=%u",unsigned(d.image_metadata));
                if(d.image_metadata)
                    fprintf(file," format=%u image_extent=%u,%u,%u base_mip=%u mip_count=%u base_layer=%u layer_count=%u view_type=%u aspects=%u usage=%u",
                            d.format,d.width,d.height,d.depth,d.base_mip,d.mip_count,d.base_layer,d.layer_count,d.view_type,d.aspects,d.usage);
            }
            fputc('\n',file);
        }
        fflush(file);errno=saved;
    }
    uint64_t begin_dispatch(const char *key,uint64_t frame,bool gpu_indirect,uint64_t offset,uint32_t x,uint32_t y,uint32_t z){
        const uint64_t seq=begin(key,Dispatch,frame);
        if(!seq)return 0;
        const int saved=errno;
        std::lock_guard<std::mutex> lock(mu);
        fprintf(file,"API_DISPATCH v=1 seq=%llu tid=%llu monotonic_ns=%llu frame=%llu shader=%s gpu_indirect=%u",
                (unsigned long long)seq,(unsigned long long)tid(),(unsigned long long)monotonic_ns(),(unsigned long long)frame,Target,unsigned(gpu_indirect));
        if(gpu_indirect)fprintf(file," offset=%llu",(unsigned long long)offset);
        else fprintf(file," xyz=%u,%u,%u",x,y,z);
        fputc('\n',file);fflush(file);errno=saved;return seq;
    }
};
