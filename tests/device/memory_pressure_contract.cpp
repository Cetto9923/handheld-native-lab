#include "xbg_memory_pressure.h"
#include <atomic>
#include <cassert>
#include <string>
#include <thread>
#include <vector>

using namespace xbg_memory_pressure;
std::atomic<unsigned> memory_trim_requested{0};
#include REQUEST_HEADER

int main(int argc, char **argv) {
    assert(argc == 3);
    Snapshot snapshot;
    assert(severity(snapshot) == 0);
    snapshot.resident = 2048 * mib - 1;
    assert(severity(snapshot) == 0);
    ++snapshot.resident;
    assert(severity(snapshot) == 1);
    snapshot.resident = 2560 * mib;
    assert(severity(snapshot) == 2);
    snapshot = {512 * mib, 10000 * mib, 1000 * mib, true};
    assert(severity(snapshot) == 1);
    snapshot.available = 500 * mib;
    assert(severity(snapshot) == 2);
    snapshot.available = 0;
    assert(severity(snapshot) == 2);
    snapshot.available_known = false;
    assert(severity(snapshot) == 0);
    snapshot.available_known = true;
    snapshot.available = snapshot.total + 1;
    assert(severity(snapshot) == 0);

    Policy policy;
    assert(policy.poll_due(0, 0));
    assert(!policy.poll_due(poll_interval_ns - 1, 0));
    assert(policy.poll_due(poll_interval_ns, 0));
    assert(policy.poll_due(poll_interval_ns + 1, 2));
    assert(!policy.poll_due(poll_interval_ns + 2, 0));
    assert(policy.poll_due(0, 0));
    assert(policy.reclaim_level(0, {}, 0) == 0);
    assert(policy.reclaim_level(0, {}, 1) == 1);
    assert(policy.reclaim_level(1, {}, 1) == 0);
    assert(policy.reclaim_level(1, {}, 2) == 2);
    assert(policy.reclaim_level(2000000000ull, {}, 2) == 0);
    assert(policy.reclaim_level(2000000001ull, {}, 2) == 2);
    assert(policy.reclaim_level(0, {}, 2) == 2);

    Policy steady;
    Snapshot working_set{2300 * mib, 12000 * mib, 6000 * mib, true};
    assert(steady.reclaim_level(0, working_set, 0) == 1);
    steady.record_reclaim(working_set);
    assert(steady.reclaim_level(60000000000ull, working_set, 0) == 0);
    working_set.resident += 64 * mib - 1;
    assert(steady.reclaim_level(60000000000ull, working_set, 0) == 0);
    ++working_set.resident;
    assert(steady.reclaim_level(60000000000ull, working_set, 0) == 1);
    steady.record_reclaim(working_set);
    // Escalation bypasses the cooldown even without another 64 MiB growth.
    working_set.resident = 2560 * mib;
    assert(steady.reclaim_level(60000000001ull, working_set, 0) == 2);
    steady.record_reclaim(working_set);
    assert(steady.reclaim_level(62000000001ull, working_set, 0) == 0);
    assert(steady.reclaim_level(62000000001ull, working_set, 2) == 2);

    Policy rebuild;
    working_set = {3000 * mib, 12000 * mib, 6000 * mib, true};
    assert(rebuild.reclaim_level(0, working_set, 0) == 2);
    working_set.resident = 2700 * mib;
    rebuild.record_reclaim(working_set);
    working_set.resident = 3000 * mib;
    assert(rebuild.reclaim_level(60000000000ull, working_set, 0) == 0);
    working_set.resident += 64 * mib;
    assert(rebuild.reclaim_level(60000000000ull, working_set, 0) == 2);

    Policy system_pressure;
    working_set = {1024 * mib, 10000 * mib, 500 * mib, true};
    assert(system_pressure.reclaim_level(0, working_set, 0) == 2);
    system_pressure.record_reclaim(working_set);
    assert(system_pressure.reclaim_level(2000000000ull, working_set, 0) == 0);
    assert(system_pressure.reclaim_level(4000000000ull, working_set, 0) == 2);
    system_pressure.record_reclaim(working_set);
    assert(system_pressure.reclaim_level(8000000000ull, working_set, 0) == 0);
    assert(system_pressure.reclaim_level(12000000000ull, working_set, 0) == 2);
    working_set.resident -= 32 * mib;
    system_pressure.record_reclaim(working_set);
    assert(system_pressure.reclaim_level(14000000000ull, working_set, 0) == 2);
    system_pressure.record_reclaim({}); // Missing after-RSS retains the previous measurement.
    assert(system_pressure.resident_after == working_set.resident);
    assert(system_pressure.reclaim_level(0, working_set, 0) == 2);

    // Recovery below both thresholds arms a later, new pressure episode.
    working_set = {};
    assert(steady.reclaim_level(63000000000ull, working_set, 0) == 0);
    working_set.resident = 2100 * mib;
    assert(steady.reclaim_level(63000000001ull, working_set, 0) == 1);

    Snapshot parsed = read_snapshot(argv[1], argv[2]);
    assert(parsed.resident == 3072000ull * 1024);
    assert(parsed.total == 12000000ull * 1024);
    assert(parsed.available == 400000ull * 1024 && parsed.available_known);
    assert(severity(parsed) == 2);
    parsed = read_snapshot("/missing-status", "/missing-meminfo");
    assert(!parsed.resident && !parsed.available_known && severity(parsed) == 0);

    std::vector<std::thread> senders;
    for (unsigned i = 0; i < 16; ++i)
        senders.emplace_back([i] { xbg_request_memory_trim(i % 3); });
    for (auto &sender : senders)
        sender.join();
    assert(memory_trim_requested.exchange(0) == 2);
    xbg_request_memory_trim(1);
    xbg_request_memory_trim(0);
    assert(memory_trim_requested.exchange(0) == 1);
    xbg_request_memory_trim(UINT32_MAX);
    assert(memory_trim_requested.exchange(0) == 2);
}
