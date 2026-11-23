#pragma once
#include <stdint.h>

// CPU-side elapsed time, not GPU timestamps or thread CPU utilization.
// Caller serializes a recorder; scopes must close before reporting/resetting.
struct XbgCpuTiming {
    enum Kind { Other, Draw, Dispatch, Descriptor, Pass, Buffer, Pipeline,
        Submit, Fence, Present, Cleanup, Hash, Image, Log, Program, CpuWrite, Snapshot,
        // V9 diagnostic sub-domains (2026-11-23): the unattributed remainder of the submission
        // callback and of the present callback, plus the snapshot lookup split. Purely additive:
        // existing kind indices are unchanged, and every scope is free on untimed frames.
        SubMaint, SubExec, SubSubmit, SubAff, SubPresPrep, SubPresSubmit, SubPace, SubSnapFast, SubSnapSlow,
        // V10 diagnostic sub-domains (2026-11-23): the NVN command interpreter inside execute().
        SubCmdDecode, SubPollRetire, SubArenaCheck,
        // V11 diagnostic sub-domains (2026-11-23): the indexed-draw path and the snapshot slow path.
        SubDrawIdxExec, SubDrawGpuIdx, SubDrawPrologue, SubSnapMap, SubSnapStream,
        // V12 diagnostic sub-domain (2026-11-23): the indirect-template lookup, priced on its own.
        SubFindTemplate,
        // V13 diagnostic sub-domains (2026-11-23): the snapshot slow path split into its pending-view
        // lookup and the CPU access it waits on, so the rest of that path can be priced on its own.
        SubSnapPv, SubSnapAccess,
        Count };
    struct Counter { uint64_t calls=0, exclusive_ns=0, inclusive_ns=0, max_ns=0; } counters[Count];
    struct Scope;
    uint64_t (*clock)();
    Scope *top=nullptr;
    explicit XbgCpuTiming(uint64_t (*read_clock)()):clock(read_clock){}
    struct Scope {
        XbgCpuTiming *recorder; Scope *parent=nullptr; Kind kind;
        uint64_t start=0,children=0;
        Scope(XbgCpuTiming *r,Kind k):recorder(r),kind(k){
            if(r){start=r->clock();parent=r->top;r->top=this;}
        }
        ~Scope(){
            if(!recorder)return;
            uint64_t elapsed=recorder->clock()-start;
            auto &c=recorder->counters[kind];++c.calls;
            c.inclusive_ns+=elapsed;c.exclusive_ns+=elapsed-children;
            if(elapsed>c.max_ns)c.max_ns=elapsed;
            if(parent)parent->children+=elapsed;
            recorder->top=parent;
        }
        Scope(const Scope&)=delete;
        Scope& operator=(const Scope&)=delete;
    };
};
