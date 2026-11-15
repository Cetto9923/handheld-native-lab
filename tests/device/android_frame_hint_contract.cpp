#include "../../src/graphics/nvn_vulkan/engine/xbg_android_frame_hint.h"

#include <cassert>
#include <vector>

struct APerformanceHintManager {};
struct APerformanceHintSession {};

static APerformanceHintManager manager;
static APerformanceHintSession session;
static int manager_calls, creates, closes, updates;
static int report_error, target_error;
static bool create_failure, manager_failure;
static int32_t last_thread;
static int64_t last_target;
static std::vector<int64_t> work;

static APerformanceHintManager *get_manager() {
    ++manager_calls;
    return manager_failure ? nullptr : &manager;
}
static APerformanceHintSession *create(APerformanceHintManager *m, const int32_t *threads,
                                       size_t count, int64_t target) {
    assert(m == &manager && count == 1 && target > 0);
    ++creates;
    last_thread = threads[0];
    last_target = target;
    return create_failure ? nullptr : &session;
}
static int update(APerformanceHintSession *s, int64_t target) {
    assert(s == &session && target > 0);
    ++updates;
    last_target = target;
    return target_error;
}
static int report(APerformanceHintSession *s, int64_t duration) {
    assert(s == &session && duration > 0);
    work.push_back(duration);
    return report_error;
}
static void close(APerformanceHintSession *s) {
    assert(s == &session);
    ++closes;
}
static XbgFrameHintApi api() { return {get_manager, create, update, report, close}; }
static void clear() {
    manager_calls = creates = closes = updates = report_error = target_error = 0;
    create_failure = manager_failure = false;
    work.clear();
}

int main() {
    {
        XbgFrameHint hints(api());
        hints.finish_frame(false, 7, 33, 100, 0);
        hints.finish_frame(true, 0, 33, 100, 0);
        hints.finish_frame(true, 7, 0, 100, 0);
        hints.finish_frame(true, 7, UINT64_MAX, 100, 0);
        assert(manager_calls == 0);
        hints.finish_frame(true, 7, 33, 100, 0);
        assert(hints.active() && creates == 1 && last_thread == 7 && last_target == 33);
        assert(work.empty()); // First frame establishes the reporting interval.
        hints.finish_frame(true, 7, 33, 133, 10);
        assert(work == std::vector<int64_t>{23} && hints.last_work_ns() == 23);
        hints.finish_frame(true, 7, 33, 166, 33);
        hints.finish_frame(true, 7, 33, 165, 0);
        hints.finish_frame(true, 7, 33, 166, 2);
        assert(work.size() == 1); // Zero work, backwards clock, excessive pacing.
        hints.finish_frame(true, 7, 16, 200, 0);
        assert(updates == 1 && last_target == 16 && work.back() == 34);
        hints.finish_frame(true, 8, 16, 216, 0);
        assert(closes == 1 && creates == 2 && last_thread == 8 && work.size() == 2);
        hints.finish_frame(false, 8, 16, 5000, 0);
        assert(!hints.active() && closes == 2);
        hints.finish_frame(true, 8, 16, 9000, 0);
        assert(creates == 3 && work.size() == 2); // Resume excludes the background gap.
        hints.finish_frame(true, 8, 16, 9016, 1);
        assert(work.back() == 15 && hints.report_count() == 3 && manager_calls == 1);
    }
    assert(closes == 3);

    clear();
    {
        auto missing = api();
        missing.report = nullptr;
        XbgFrameHint hints(missing);
        hints.finish_frame(true, 1, 33, 100, 0);
        hints.finish_frame(true, 1, 33, 133, 0);
        assert(hints.unavailable() && !hints.active() && manager_calls == 0);
    }
    clear();
    {
        manager_failure = true;
        XbgFrameHint hints(api());
        hints.finish_frame(true, 1, 33, 100, 0);
        hints.finish_frame(true, 1, 33, 133, 0);
        assert(hints.unavailable() && manager_calls == 1 && creates == 0);
    }
    clear();
    {
        create_failure = true;
        XbgFrameHint hints(api());
        hints.finish_frame(true, 1, 33, 100, 0);
        hints.finish_frame(false, 1, 33, 133, 0);
        hints.finish_frame(true, 1, 33, 166, 0);
        assert(hints.unavailable() && creates == 1 && closes == 0);
    }
    clear();
    {
        XbgFrameHint hints(api());
        hints.finish_frame(true, 1, 33, 100, 0);
        report_error = 22;
        hints.finish_frame(true, 1, 33, 133, 0);
        hints.finish_frame(true, 1, 33, 166, 0);
        assert(hints.unavailable() && !hints.active() && closes == 1 && work.size() == 1);
    }
    clear();
    {
        XbgFrameHint hints(api());
        hints.finish_frame(true, 1, 33, 100, 0);
        target_error = 22;
        hints.finish_frame(true, 1, 16, 133, 0);
        assert(hints.unavailable() && !hints.active() && closes == 1 && work.empty());
    }
    clear();
    {
        XbgFrameHint hints(api());
        hints.finish_frame(true, 1, 33, 1, 0);
        hints.finish_frame(true, 1, 33, UINT64_MAX, 0);
        assert(work.empty()); // A duration must fit the Android signed ABI.
        hints.reset();
        hints.reset();
        assert(closes == 1);
    }
}
