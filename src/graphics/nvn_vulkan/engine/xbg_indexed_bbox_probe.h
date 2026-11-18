#pragma once
#include <cstdint>
#include "xbg_writeback_policy.h"
// Shadow-only metadata envelopes, never a readiness decision.
struct XbgIndexedBBox {
    uint64_t low=0,high=0;
    bool empty=true,usable=true;
    void add(uint64_t address,uint64_t bytes){
        if(!xbg_wb_range_valid(address,bytes)){usable=false;return;}
        const uint64_t end=address+bytes;
        if(empty){low=address;high=end;empty=false;}
        else {if(address<low)low=address;if(end>high)high=end;}
    }
    bool outside(uint64_t address,uint64_t bytes)const{
        if(!usable || !address || !xbg_wb_range_valid(address,bytes))return false;
        return empty || address+bytes<=low || address>=high;
    }
};
struct XbgIndexedBBoxStats {
    uint64_t calls=0,report_build_steps=0,writeback_build_steps=0;
    uint64_t report_empty=0,writeback_empty=0,report_unusable=0,writeback_unusable=0;
    uint64_t queries=0,invalid_queries=0,report_outside=0,writeback_outside=0;
    uint64_t report_scan_steps=0,writeback_scan_steps=0,report_avoidable_steps=0,writeback_avoidable_steps=0;
};
struct XbgIndexedBBoxProbe {
    XbgIndexedBBoxStats *out;
    XbgIndexedBBox reports,writebacks;
    bool report_outside=false,writeback_outside=false;
    explicit XbgIndexedBBoxProbe(XbgIndexedBBoxStats *s):out(s){}
    template<class Reports,class Writebacks>
    void build(const Reports &r,const Writebacks &w,uint64_t report_bytes){
        if(!out)return;
        ++out->calls;
        for(const auto &x:r){++out->report_build_steps;reports.add(x.addr,report_bytes);}
        for(const auto &x:w){++out->writeback_build_steps;writebacks.add(x.addr,x.bytes);}
        out->report_empty+=r.empty();out->writeback_empty+=w.empty();
        out->report_unusable+=!reports.usable;out->writeback_unusable+=!writebacks.usable;
    }
    void request(uint64_t a,uint64_t b){
        if(!out)return;
        ++out->queries;report_outside=writeback_outside=false;
        if(!a || !xbg_wb_range_valid(a,b)){++out->invalid_queries;return;}
        report_outside=reports.outside(a,b);writeback_outside=writebacks.outside(a,b);
        out->report_outside+=report_outside;out->writeback_outside+=writeback_outside;
    }
    void report_step(){if(out){++out->report_scan_steps;out->report_avoidable_steps+=report_outside;}}
    void writeback_step(){if(out){++out->writeback_scan_steps;out->writeback_avoidable_steps+=writeback_outside;}}
};
