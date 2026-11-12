// The retirement of a deferred event.
//
// A deferred event is published when the GPU has finished the submission that carries its signal. The guest reads the
// value of an event, and every signal it has already issued for that address has to be published before that read,
// otherwise the guest would read a value older than the one it signalled. The newest signal to that address therefore
// fixes the submission that has to retire; the submissions queued after it stay in flight.
//
// This is the rule a driver follows when it retires a fence: the value a host waits for is the one its own last signal
// carried, and the work that was queued after that signal does not have to be waited for. Waiting for it is what makes
// the guest's own clock run slower than the game's, because the game only moves on when the value it waits for is there.
//
// The order of the signals is the order they were issued in, so the newest one to an address is the last one issued for
// it. The addresses are the guest's event words; one address can carry several signals (the same event signalled again
// with the add mode), and then the newest of them decides the value the guest is entitled to read.
#pragma once
#include <cstddef>
#include <cstdint>

struct XbgEventSignal{uint64_t addr;uint64_t order;};

// The index of the newest signal to `addr`, or -1 when the guest has not signalled that address.
inline long xbg_event_newest_signal(const XbgEventSignal *signals,size_t count,uint64_t addr){
    long best=-1;uint64_t best_order=0;
    for(size_t i=0;i<count;++i){
        if(signals[i].addr!=addr)continue;
        if(best<0||signals[i].order>best_order){best=(long)i;best_order=signals[i].order;}
    }
    return best;
}
