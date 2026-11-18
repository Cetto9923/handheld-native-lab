#pragma once
#include <cstdint>
// Diagnostic only; keys never decide readiness. One instance per readiness call.
struct XbgIndexedReadyStats {
    uint64_t calls=0,requests=0,reports_steps=0,writebacks_steps=0;
    uint64_t vertex_requests=0,vertex_repeats=0;
    uint64_t exact_calls=0,exact_requests=0,unique=0,duplicates=0,overflow_calls=0;
    uint64_t duplicate_reports_steps=0,duplicate_writebacks_steps=0;
};
struct XbgIndexedReadyProbe {
    struct Key {uint64_t address,bytes,alignment;};
    XbgIndexedReadyStats *out;
    Key keys[128]; // No heap; intentionally not initialized when tracing is off.
    unsigned used=0;
    uint16_t vertex_mask=0;
    uint64_t requests=0,duplicates=0;
    bool overflow=false,duplicate=false;
    explicit XbgIndexedReadyProbe(XbgIndexedReadyStats *p):out(p){}
    XbgIndexedReadyProbe(const XbgIndexedReadyProbe&)=delete;
    void vertex_stream(unsigned stream){
        if(!out)return;
        ++out->vertex_requests;
        const uint16_t bit=uint16_t(1u<<stream);
        if(vertex_mask&bit)++out->vertex_repeats;
        vertex_mask|=bit;
    }
    void request(uint64_t a,uint64_t b,uint64_t align){
        if(!out)return;
        ++requests;++out->requests;duplicate=false;
        for(unsigned i=0;i<used;++i)if(keys[i].address==a && keys[i].bytes==b && keys[i].alignment==align){duplicate=true;++duplicates;return;}
        if(used<128)keys[used++]={a,b,align};else overflow=true;
    }
    void report_step(){if(out){++out->reports_steps;if(duplicate)++out->duplicate_reports_steps;}}
    void writeback_step(){if(out){++out->writebacks_steps;if(duplicate)++out->duplicate_writebacks_steps;}}
    ~XbgIndexedReadyProbe(){
        if(!out)return;
        ++out->calls;
        if(overflow){++out->overflow_calls;return;}
        ++out->exact_calls;out->exact_requests+=requests;out->unique+=used;out->duplicates+=duplicates;
    }
};
