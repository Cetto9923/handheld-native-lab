#pragma once
#include <stddef.h>
#include <stdint.h>

// Optional Android NDK API 33 C ABI. Resolve at runtime; older hosts need no
// Android library. The engine serializes this state with its existing mutex.
struct APerformanceHintManager;
struct APerformanceHintSession;

struct XbgPerformanceHint {
    struct Api {
        APerformanceHintManager *manager=nullptr;
        APerformanceHintSession *(*create)(APerformanceHintManager*,const int32_t*,size_t,int64_t)=nullptr;
        void (*close)(APerformanceHintSession*)=nullptr;
        int (*update)(APerformanceHintSession*,int64_t)=nullptr;
        int (*report)(APerformanceHintSession*,int64_t)=nullptr;
        bool ready()const{return manager && create && close && update && report;}
    } api;
    APerformanceHintSession *session=nullptr;
    bool enabled=false,failed=false;
    int32_t submit_tid=0,session_tids[2]{};
    size_t session_count=0;
    uint64_t start_ns=0,target_ns=0,reports=0,creates=0;
    int last_error=0;

    void close(){if(session){api.close(session);session=nullptr;}session_count=0;target_ns=0;}
    void enable(bool on){
        if(enabled==on)return;
        close();enabled=on;failed=false;last_error=0;start_ns=0;submit_tid=0;
    }
    void submit(int32_t tid,uint64_t now){
        if(!enabled || failed)return;
        if(!start_ns)start_ns=now;
        submit_tid=tid;
    }
    // Called before frame pacing: report real work, never idle time or a made-up
    // duration. No affinity, priority, clock, fence or game timing changes.
    bool frame(int32_t present_tid,uint64_t now,uint64_t target){
        uint64_t begin=start_ns;start_ns=0;
        if(!enabled || failed)return false;
        if(!target){close();return false;}
        if(!begin || now<=begin || submit_tid<=0 || present_tid<=0)return false;
        if(!api.ready()){failed=true;last_error=-1;return false;}
        int32_t tids[2]={submit_tid,present_tid};size_t count=tids[0]==tids[1]?1:2;
        if(count==2 && tids[0]>tids[1]){int32_t v=tids[0];tids[0]=tids[1];tids[1]=v;}
        bool changed=session_count!=count;
        for(size_t i=0;i<count && !changed;i++)changed=session_tids[i]!=tids[i];
        if(changed)close();
        if(!session){
            session=api.create(api.manager,tids,count,int64_t(target));
            if(!session){failed=true;last_error=-2;return false;}
            ++creates;session_count=count;target_ns=target;
            for(size_t i=0;i<count;i++)session_tids[i]=tids[i];
        }else if(target_ns!=target){
            last_error=api.update(session,int64_t(target));
            if(last_error){close();failed=true;return false;}
            target_ns=target;
        }
        last_error=api.report(session,int64_t(now-begin));
        if(last_error){close();failed=true;return false;}
        ++reports;return true;
    }
};
