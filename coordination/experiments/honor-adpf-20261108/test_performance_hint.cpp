#include "xbg_performance_hint.h"
#include <cassert>
#include <cstdio>

struct APerformanceHintManager {} manager;
struct APerformanceHintSession {} session;
static unsigned creates=0,closes=0,updates=0,reports=0;
static int error=0;
static int32_t seen_tids[2];static size_t seen_count=0;
static int64_t seen_duration=0,seen_target=0;
static APerformanceHintSession *create(APerformanceHintManager *m,const int32_t *t,size_t n,int64_t target){
    assert(m==&manager);++creates;seen_count=n;for(size_t i=0;i<n;i++)seen_tids[i]=t[i];seen_target=target;return &session;
}
static void close(APerformanceHintSession *s){assert(s==&session);++closes;}
static int update(APerformanceHintSession *s,int64_t target){assert(s==&session);++updates;seen_target=target;return error;}
static int report(APerformanceHintSession *s,int64_t duration){assert(s==&session);++reports;seen_duration=duration;return error;}
int main(){
    XbgPerformanceHint h;h.api={&manager,create,close,update,report};
    h.submit(7,100);assert(!h.frame(7,200,16666666));assert(!creates && !reports);
    h.enable(true);h.submit(9,100);h.submit(7,130);assert(h.frame(9,300,16666666));
    assert(creates==1 && reports==1 && seen_duration==200 && seen_target==16666666);
    assert(seen_count==2 && seen_tids[0]==7 && seen_tids[1]==9);
    // Reversed caller order is the same group; the first submission owns the start.
    h.submit(9,400);assert(h.frame(7,650,16666666));assert(creates==1 && seen_duration==250);
    h.submit(9,700);assert(h.frame(7,950,33333333));assert(updates==1 && seen_target==33333333);
    // Role changes must not leave a dead title thread in a new scene's session.
    h.submit(11,1000);assert(h.frame(11,1200,16666666));assert(creates==2 && closes==1 && seen_count==1);
    h.submit(11,1300);assert(!h.frame(11,1500,0));assert(closes==2 && !h.session);
    h.submit(11,1600);error=32;assert(!h.frame(11,1800,16666666));assert(h.failed && h.last_error==32 && !h.session);
    unsigned prior=reports;h.submit(11,2000);assert(!h.frame(11,2100,16666666));assert(reports==prior);
    h.enable(false);h.enable(true);error=0;h.submit(11,2200);assert(h.frame(11,2400,16666666));h.enable(false);assert(!h.session);
    XbgPerformanceHint absent;absent.enable(true);absent.submit(1,100);assert(!absent.frame(1,200,16666666));assert(absent.failed && absent.last_error==-1);
    puts("performance hint lifecycle, real intervals, role/target changes, default-off and bounded failure: PASS");
}
