// The retirement of a readback.
//
// A readback copies a GPU result into the guest's memory. The copy and the buffer it writes belong to one slot: the one
// that was recording when they were recorded. That slot's submission is the one that owns the readback, and the guest's
// bytes are final when that submission's fence has signalled - not before, and not because of anything else.
//
// The engine answered a readback with a drain of every submission in flight: it waited for all of them and did their
// retirement work (descriptor release, arena reset, query fetch, writeback resolution, the guest's fence publication) at
// the readback's point. The guest asked for a range of bytes, not for a queue to be empty.
//
// The rule here is the boundary of that wait:
//   - the owner is the slot that carries the copy,
//   - the host waits for that submission's fence and for nothing else,
//   - a submission that is not in flight needs no wait at all: its data is final,
//   - the state of the other submissions never changes the answer.
#pragma once
#include <cstddef>
#include <cstdint>

struct XbgReadbackDecision{
    int owner;      // the ring index of the submission that carries the readback, -1 when there is none
    bool wait;      // the host has to wait for that submission's fence
    bool fallback;  // the owner could not be identified: the caller has to drain, as it did before
};

// The decision for a readback whose copy belongs to the slot at `owner`.
// `pending[i]` tells whether the submission of the ring slot `i` is in flight.
inline XbgReadbackDecision xbg_readback_decide(const bool *pending,size_t count,int owner){
    XbgReadbackDecision decision{-1,false,true};
    if(!pending||owner<0||(size_t)owner>=count)return decision;   // no owner: the caller drains
    decision.owner=owner;
    decision.fallback=false;
    decision.wait=pending[owner];   // the owner is in flight: its fence has to be waited for
    return decision;
}
