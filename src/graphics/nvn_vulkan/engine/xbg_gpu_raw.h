#pragma once
#include <cstdint>
#include <cstddef>
// Engine mutex owns every access. No API calls, clocks, heap or I/O here.
struct XbgGpuRaw {
    static constexpr uint32_t Capacity=4096,Invalid=UINT32_MAX;
    struct Handle {uint64_t session=0,serial=0;uint32_t row=Invalid;};
    struct Row {
        uint64_t serial=0,covers=0,present=0,hud_frame=0,submit_begin=0,submit_end=0,retire_entry=0,query_observed=0,t0=0,t1=0;
        uint32_t ring=0,commands=0;int32_t submit_result=0,query_result=0;
        bool submitted=false,retired=false,has_query=false,query_read=false,legacy_accepted=false;
    };
    Row rows[Capacity]{};
    uint64_t session=0,first=0,end=0,dropped=0;
    uint32_t limit=0,used=0,pending=0;
    bool enabled=false,closed=false,exported=false;
    void configure(uint64_t from,uint64_t count,uint32_t maximum){
        ++session;if(!session)++session;
        enabled=false;closed=false;exported=false;first=end=dropped=0;limit=used=pending=0;
        if(!count || count>120 || from>UINT64_MAX-count || !maximum || maximum>Capacity)return;
        first=from;end=from+count;limit=maximum;enabled=true;
    }
    void tick(uint64_t frame){if(enabled && frame>=end)closed=true;}
    Handle admit(uint64_t frame,uint64_t serial,uint64_t covers,uint32_t ring,uint32_t commands,bool query){
        tick(frame);if(!enabled || closed || frame<first)return {};
        if(used==limit){++dropped;return {};}
        Handle h{session,serial,used++};rows[h.row]=Row{};
        auto &r=rows[h.row];r.serial=serial;r.covers=covers;r.present=frame;r.ring=ring;r.commands=commands;r.has_query=query;
        return h;
    }
    Row *get(Handle h){return enabled && h.session==session && h.row<used && rows[h.row].serial==h.serial?&rows[h.row]:nullptr;}
    void submit_done(Handle h,int32_t result,uint64_t end_ns,uint64_t hud){
        auto *r=get(h);if(!r || r->submitted || r->retired)return;
        r->submit_result=result;r->submit_end=end_ns;r->hud_frame=hud;
        if(result==0){r->submitted=true;++pending;}else r->retired=true;
    }
    void query(Handle h,int32_t result,uint64_t t0,uint64_t t1,uint64_t observed){
        auto *r=get(h);if(!r || !r->submitted || r->retired || !r->has_query)return;
        r->query_read=true;r->query_result=result;r->t0=t0;r->t1=t1;r->query_observed=observed;r->legacy_accepted=result==0 && t1>=t0;
    }
    void retire(Handle h){auto *r=get(h);if(r && r->submitted && !r->retired){r->retired=true;--pending;}}
    bool ready(uint64_t frame){tick(frame);return enabled && closed && !pending && !exported;}
};
