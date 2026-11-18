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
#include <unistd.h>
#include <sys/syscall.h>
#endif
// Quiescent initialization only, before warm workers. Diagnostic file I/O is
// bounded to one pair per API category for one shader. Never calls Vulkan.
class XbgApiBreadcrumb {
public:
    enum Site { RuntimeModule, Descriptors, RuntimePipeline, WarmPipeline, Dispatch, Count };
    static constexpr const char *Target="073ce354a4f65e2f";
private:
    std::atomic<unsigned> seen{0};std::mutex mu;FILE *file=nullptr;bool enabled=false;uint64_t sequence=0;
    static uint64_t tid(){
#if defined(__ANDROID__)
        return uint64_t(syscall(SYS_gettid));
#elif defined(__APPLE__)
        uint64_t id=0;pthread_threadid_np(nullptr,&id);return id;
#else
        return 0; // host contract test only, not Android evidence
#endif
    }
    static const char *name(Site s){static const char *names[]={"runtime_module","descriptors","runtime_pipeline","warm_pipeline","dispatch_record"};return names[unsigned(s)];}
public:
    ~XbgApiBreadcrumb(){if(file)fclose(file);}
    void configure(bool on,const char *path){std::lock_guard<std::mutex> lock(mu);if(file)fclose(file);file=nullptr;enabled=false;seen=0;sequence=0;if(on){
        // Preserve the preceding session before a restart can replace its fault evidence.
        // Failed rotation disables diagnostics rather than truncating the only copy.
        const std::string previous=std::string(path)+".prev";
        if(rename(path,previous.c_str())!=0 && errno!=ENOENT)return;
        file=fopen(path,"wb");enabled=file!=nullptr;if(enabled){timespec now{};clock_gettime(CLOCK_MONOTONIC,&now);const uint64_t ns=uint64_t(now.tv_sec)*1000000000ull+uint64_t(now.tv_nsec);fprintf(file,"API_BREADCRUMB_SESSION v=1 pid=%llu init_monotonic_ns=%llu target=%s max_lines=11\n",(unsigned long long)getpid(),(unsigned long long)ns,Target);fflush(file);}}}
    uint64_t begin(const char *key,Site site,uint64_t frame,const uint32_t *words=nullptr,size_t count=0){
        if(!enabled || !key || unsigned(site)>=Count || std::strcmp(key,Target))return 0;
        unsigned bit=1u<<unsigned(site);if(seen.load(std::memory_order_relaxed)&bit)return 0;
        if(seen.fetch_or(bit,std::memory_order_relaxed)&bit)return 0;
        const int saved=errno;std::lock_guard<std::mutex> lock(mu);const uint64_t seq=++sequence;
        uint64_t hash=0;if(words && count){hash=14695981039346656037ull;for(size_t i=0;i<count;++i)for(unsigned b=0;b<4;++b)hash=(hash^uint8_t(words[i]>>(b*8)))*1099511628211ull;}
        fprintf(file,"API_BREADCRUMB v=1 seq=%llu phase=begin tid=%llu frame=%llu shader=%s site=%s words=%zu fnv64=%016llx\n",(unsigned long long)seq,(unsigned long long)tid(),(unsigned long long)frame,Target,name(site),count,(unsigned long long)hash);fflush(file);errno=saved;return seq;
    }
    void end(uint64_t seq,Site site,uint64_t frame,int result){if(!seq)return;const int saved=errno;std::lock_guard<std::mutex> lock(mu);
        fprintf(file,"API_BREADCRUMB v=1 seq=%llu phase=end tid=%llu frame=%llu shader=%s site=%s result=%d\n",(unsigned long long)seq,(unsigned long long)tid(),(unsigned long long)frame,Target,name(site),result);fflush(file);errno=saved;
    }
};
