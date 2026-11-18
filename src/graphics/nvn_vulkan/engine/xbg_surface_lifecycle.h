#pragma once
#include <cstdint>
#include <mutex>
#include <utility>

// Producer-side admission only. Never call from a renderer callback while an
// external closer may hold admission waiting for that callback to finish.
// Integration supplies serialized setup/retirement; this class performs no
// GPU waits, queue operations, resource destruction or guest-state locking.
class XbgSurfaceLifecycle {
public:
    struct Token {
        uintptr_t owner=0;
        uint64_t generation=0;
        bool operator==(const Token &v) const { return owner==v.owner && generation==v.generation; }
    };
    struct State { Token token; bool open=false,retired=true; };
private:
    std::recursive_mutex mutex_;
    State state_{};
    bool transitioning_=false;
public:
    template<class F> auto scoped(F &&fn) -> decltype(fn()) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        return fn();
    }
    State state() { return scoped([&]{return state_;}); }
    // Setup must return true only after safe ownership publication. On failure
    // its integration must dispose/retain partial resources separately; no open
    // admission is granted here. Generation is consumed even on setup failure.
    template<class F> bool reopen(uintptr_t owner,F &&setup,Token *result=nullptr) {
        return scoped([&]{
            if(!owner || transitioning_ || !state_.retired || state_.token.generation==UINT64_MAX)return false;
            state_.token={owner,state_.token.generation+1};
            state_.open=false;state_.retired=false;transitioning_=true;
            bool ok=false;
            try { ok=setup(state_.token); }
            catch(...) { transitioning_=false;throw; }
            transitioning_=false;
            state_.open=ok;
            // Failed setup stays closed but unretired until explicit cleanup.
            if(ok && result)*result=state_.token;
            return ok;
        });
    }
    // Covers state lookup through copying/enqueueing. owner=0 is the legacy
    // present path: allowed only when the current logical owner is open.
    template<class F> bool admit(uintptr_t owner,F &&producer) {
        return scoped([&]{
            if(transitioning_ || !state_.open || (owner && owner!=state_.token.owner))return false;
            producer(state_.token);return true;
        });
    }
    // Previously admitted queued work must finish inside retire(token) BEFORE
    // resources are destroyed. Worker execution must not reacquire admission.
    // False/exception retains closed ownership and permits cleanup retry; never
    // implicitly reopens or authorizes destruction of a different generation.
    template<class F> bool close(Token expected,F &&retire) {
        return scoped([&]{
            if(transitioning_ || !expected.owner || !(expected==state_.token))return false;
            if(state_.retired)return true;
            state_.open=false;transitioning_=true;
            bool ok=false;
            try { ok=retire(expected); }
            catch(...) { transitioning_=false;throw; }
            transitioning_=false;
            if(ok)state_.retired=true;
            return ok;
        });
    }
};
