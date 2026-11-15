#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

struct APerformanceHintManager;
struct APerformanceHintSession;

struct XbgFrameHintApi {
    APerformanceHintManager *(*manager)() = nullptr;
    APerformanceHintSession *(*create)(APerformanceHintManager *, const int32_t *, size_t, int64_t) = nullptr;
    int (*target)(APerformanceHintSession *, int64_t) = nullptr;
    int (*report)(APerformanceHintSession *, int64_t) = nullptr;
    void (*close)(APerformanceHintSession *) = nullptr;
};

// One periodic render-thread workload. The native Android symbols are loaded
// separately so an older Android release can use the same library.
class XbgFrameHint {
    XbgFrameHintApi api;
    APerformanceHintManager *manager = nullptr;
    APerformanceHintSession *session = nullptr;
    bool attempted = false;
    bool failed = false;
    int32_t thread = 0;
    int64_t target = 0;
    uint64_t previous_end = 0;
    uint64_t reports = 0;
    int64_t last_work = 0;

public:
    explicit XbgFrameHint(XbgFrameHintApi functions) : api(functions) {}
    XbgFrameHint(const XbgFrameHint &) = delete;
    XbgFrameHint &operator=(const XbgFrameHint &) = delete;
    ~XbgFrameHint() { reset(); }

    void reset() {
        if (session) api.close(session);
        session = nullptr;
        thread = 0;
        target = 0;
        previous_end = 0;
        last_work = 0;
    }

    void finish_frame(bool enabled, int32_t tid, uint64_t target_ns,
                      uint64_t now_ns, uint64_t pacing_ns) {
        if (!enabled || tid <= 0 || !target_ns ||
            target_ns > uint64_t(std::numeric_limits<int64_t>::max())) {
            reset();
            return;
        }
        if (failed) return;
        if (!attempted) {
            attempted = true;
            if (api.manager && api.create && api.target && api.report && api.close)
                manager = api.manager();
            if (!manager) failed = true;
        }
        if (!manager) return;
        if (session && thread != tid) reset();
        if (!session) {
            session = api.create(manager, &tid, 1, int64_t(target_ns));
            if (!session) {
                failed = true;
                return;
            }
            thread = tid;
            target = int64_t(target_ns);
            previous_end = now_ns;
            return;
        }
        if (target != int64_t(target_ns)) {
            if (api.target(session, int64_t(target_ns)) != 0) {
                failed = true;
                reset();
                return;
            }
            target = int64_t(target_ns);
        }
        const uint64_t start = previous_end;
        previous_end = now_ns;
        // The game's own frame limiter sleeps inside this interval. Reporting
        // that sleep as work would ask the scheduler to compensate for it.
        if (now_ns <= start || now_ns - start <= pacing_ns) return;
        const uint64_t work = now_ns - start - pacing_ns;
        if (work > uint64_t(std::numeric_limits<int64_t>::max())) return;
        last_work = int64_t(work);
        if (api.report(session, last_work) != 0) {
            failed = true;
            reset();
            return;
        }
        ++reports;
    }

    bool active() const { return session != nullptr; }
    bool unavailable() const { return failed; }
    uint64_t report_count() const { return reports; }
    int64_t last_work_ns() const { return last_work; }
};
