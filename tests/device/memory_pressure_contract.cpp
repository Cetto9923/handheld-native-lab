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
    assert(thor_small_memory("AYN Thor", 0x5143, 0x43050a01, 7540305920ull));
    assert(!thor_small_memory("AYN Thor", 0x5143, 0x43050a01, 15996096512ull));
    assert(!thor_small_memory("AYN Thor", 0x5143, 0x43050a01, 0));
    assert(!thor_small_memory("AYN Thor", 0x5143, 0x44050001, 7540305920ull));
    assert(!thor_small_memory("RG477M", 0x5143, 0x43050a01, 7540305920ull));
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

    assert(resident_budget_mib_for_model("RG477M") == 5120);
    assert(resident_budget_mib_for_model("FLC-AN00") == 2560);
    assert(resident_budget_mib_for_model("RG477M-other") == 2560);
    assert(resident_budget_mib_for_model("") == 2560);
    assert(resident_budget_mib_for_model(nullptr) == 2560);
    Snapshot rg_working_set{2687221760ull, 12400549888ull, 6763016192ull, true};
    const uint64_t rg_limit = 5120 * mib;
    assert(severity(rg_working_set) == 2);
    assert(resident_limit_bytes(rg_working_set, rg_limit) == rg_limit);
    assert(severity(rg_working_set, rg_limit) == 0);
    rg_working_set.resident = 4608 * mib - 1;
    assert(severity(rg_working_set, rg_limit) == 0);
    ++rg_working_set.resident;
    assert(severity(rg_working_set, rg_limit) == 1);
    rg_working_set.resident = rg_limit - 1;
    assert(severity(rg_working_set, rg_limit) == 1);
    ++rg_working_set.resident;
    assert(severity(rg_working_set, rg_limit) == 2);
    rg_working_set.total = rg_limit * 2 - 1;
    assert(resident_limit_bytes(rg_working_set, rg_limit) == default_resident_limit);
    ++rg_working_set.total;
    assert(resident_limit_bytes(rg_working_set, rg_limit) == rg_limit);
    assert(resident_limit_bytes({}, rg_limit) == default_resident_limit);
    assert(resident_limit_bytes(rg_working_set, UINT64_MAX) == default_resident_limit);
    assert(resident_limit_bytes(rg_working_set, 0) == default_resident_limit);

    Policy rg_policy;
    rg_policy.resident_limit = rg_limit;
    rg_working_set = {3700 * mib, 12000 * mib, 6000 * mib, true};
    assert(rg_policy.reclaim_level(0, rg_working_set, 0) == 0);
    assert(rg_policy.reclaim_level(1, rg_working_set, 1) == 1);
    assert(rg_policy.reclaim_level(2, rg_working_set, 2) == 2);
    assert(severity({3000 * mib, 12000 * mib, 1200 * mib, true}, rg_limit) == 1);
    assert(severity({3000 * mib, 12000 * mib, 600 * mib, true}, rg_limit) == 2);
    assert(severity({3000 * mib, 12000 * mib, 0, true}, rg_limit) == 2);
    // The reported 12 GB phone uses the default profile even with ample RAM.
    assert(severity({3191264ull * 1024, 11844259840ull, 4407549952ull, true}) == 2);

    const Snapshot rg_soft_warning{4941631488ull, 12400549888ull, 4542865408ull, true};
    assert(severity(rg_soft_warning, rg_limit) == 1);
    assert(should_preserve_warm_cache(rg_soft_warning, rg_limit, 1, 0));
    assert(!should_preserve_warm_cache(rg_soft_warning, default_resident_limit, 1, 0));
    assert(!should_preserve_warm_cache(rg_soft_warning, rg_limit, 2, 0));
    assert(!should_preserve_warm_cache(rg_soft_warning, rg_limit, 1, 1));
    assert(!should_preserve_warm_cache(rg_soft_warning, rg_limit, 1, 2));
    Snapshot unknown_headroom = rg_soft_warning;
    unknown_headroom.available_known = false;
    assert(!should_preserve_warm_cache(unknown_headroom, rg_limit, 1, 0));
    unknown_headroom = rg_soft_warning;
    unknown_headroom.available = unknown_headroom.total + 1;
    assert(!should_preserve_warm_cache(unknown_headroom, rg_limit, 1, 0));
    unknown_headroom.available = unknown_headroom.total / 10;
    assert(!should_preserve_warm_cache(unknown_headroom, rg_limit, 1, 0));
    unknown_headroom.available = unknown_headroom.total / 20;
    assert(!should_preserve_warm_cache(unknown_headroom, rg_limit, 1, 0));
    assert(!should_preserve_warm_cache({}, rg_limit, 1, 0));

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
