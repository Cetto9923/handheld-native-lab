#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

namespace xbg_memory_pressure {
constexpr uint64_t mib = 1024ull * 1024;
constexpr uint64_t poll_interval_ns = 500000000;
constexpr uint64_t default_resident_limit = 2560 * mib;

inline unsigned resident_budget_mib_for_model(const char *model) {
    // RG477M has sustained a working set above 4 GiB in real-scene tests.
    return model && std::strcmp(model, "RG477M") == 0 ? 5120 : 2560;
}

struct Snapshot {
    uint64_t resident = 0;
    uint64_t total = 0;
    uint64_t available = 0;
    bool available_known = false;
};

inline bool thor_small_memory(const char *model, uint32_t vendor, uint32_t device,
                              uint64_t total) {
    return model && std::strcmp(model, "AYN Thor") == 0 &&
           vendor == 0x5143 && device == 0x43050a01 &&
           total >= 6 * 1024 * mib && total <= 8 * 1024 * mib;
}

inline bool general_compact_memory(uint64_t total) {
    return total > 0 && total <= 8500 * mib;
}

inline Snapshot read_snapshot(const char *status = "/proc/self/status",
                              const char *meminfo = "/proc/meminfo") {
    Snapshot snapshot;
    char line[256];
    unsigned long long kib = 0;
    if (FILE *file = std::fopen(status, "r")) {
        while (std::fgets(line, sizeof(line), file)) {
            if (std::sscanf(line, "VmRSS: %llu kB", &kib) == 1 &&
                kib <= std::numeric_limits<uint64_t>::max() / 1024) {
                snapshot.resident = uint64_t(kib) * 1024;
                break;
            }
        }
        std::fclose(file);
    }
    if (FILE *file = std::fopen(meminfo, "r")) {
        while (std::fgets(line, sizeof(line), file)) {
            if (std::sscanf(line, "MemTotal: %llu kB", &kib) == 1 &&
                kib <= std::numeric_limits<uint64_t>::max() / 1024)
                snapshot.total = uint64_t(kib) * 1024;
            if (std::sscanf(line, "MemAvailable: %llu kB", &kib) == 1 &&
                kib <= std::numeric_limits<uint64_t>::max() / 1024) {
                snapshot.available = uint64_t(kib) * 1024;
                snapshot.available_known = true;
            }
        }
        std::fclose(file);
    }
    return snapshot;
}

inline unsigned system_severity(const Snapshot &snapshot) {
    unsigned level = 0;
    if (snapshot.available_known && snapshot.total && snapshot.available <= snapshot.total) {
        if (snapshot.available <= snapshot.total / 20)
            level = 2;
        else if (snapshot.available <= snapshot.total / 10)
            level = 1;
    }
    return level;
}

inline uint64_t resident_limit_bytes(const Snapshot &snapshot,
                                    uint64_t configured = default_resident_limit) {
    // A device-specific budget also requires physical RAM headroom. Missing
    // or insufficient capacity uses the reported phone-exit budget.
    // RG477M reports MemTotal just under 12 GiB (11.83 GiB after carve-outs);
    // a 12 GiB threshold skipped this branch, so its model budget fell through
    // and the engine ran its scene working set (~4.6-5.2 GiB) inside the warn
    // band, reclaiming every few frames. 11 GiB covers 11.5-12 GiB handhelds
    // while 8 GiB phones keep the configured path.
    // The 720P airship working set peaks at ~6.0 GiB RSS once memo and arenas
    // stay warm, so a total/2 cap (5912 MiB here) put the steady state at or
    // past the level-2 line: the wipe cost 653 ms, landed in a slower rebuilt
    // state, and RSS re-crossed within minutes. Budget to 2/3 of physical RAM
    // capped at 7.5 GiB; the handheld runs the game alone, so ~4.3 GiB still
    // stays outside the process at peak.
    if (snapshot.total >= 11ull * 1024 * mib) {
        return std::min<uint64_t>(7680ull * mib, snapshot.total / 3 * 2);
    }
    if (configured > default_resident_limit && configured <= snapshot.total / 2)
        return configured;
    return default_resident_limit;
}

inline unsigned severity(const Snapshot &snapshot,
                         uint64_t configured = default_resident_limit) {
    // The reported phone exits had last RSS samples of 3.04 and 3.64 GiB.
    const uint64_t limit = resident_limit_bytes(snapshot, configured);
    unsigned resident_level = 0;
    if (snapshot.resident >= limit)
        resident_level = 2;
    else if (snapshot.resident >= limit - 512 * mib)
        resident_level = 1;
    return std::max(resident_level, system_severity(snapshot));
}

inline bool should_preserve_warm_cache(const Snapshot &snapshot, uint64_t configured,
                                       unsigned level, unsigned requested) {
    // Only the tested device budget can retain prewarming during a soft RSS
    // warning. Unknown headroom and actual system pressure require full trim.
    return level == 1 && requested == 0 &&
           resident_limit_bytes(snapshot, configured) > default_resident_limit &&
           snapshot.available_known && snapshot.total &&
           snapshot.available <= snapshot.total && snapshot.available > snapshot.total / 10;
}

struct Policy {
    uint64_t resident_limit = default_resident_limit;
    uint64_t last_poll = 0;
    uint64_t last_reclaim = 0;
    unsigned last_level = 0;
    bool polled = false;
    bool reclaimed = false;
    uint64_t resident_before = 0;
    uint64_t resident_after = 0;
    uint64_t retry_interval = 0;
    bool measured = false;

    bool poll_due(uint64_t now, unsigned requested) {
        if (!requested && polled && now >= last_poll && now - last_poll < poll_interval_ns)
            return false;
        polled = true;
        last_poll = now;
        return true;
    }

    unsigned reclaim_level(uint64_t now, const Snapshot &snapshot, unsigned requested) {
        const unsigned level = std::max(severity(snapshot, resident_limit), std::min(requested, 2u));
        if (!level) {
            last_level = 0;
            return 0;
        }
        const uint64_t interval = level == 2 ? 2000000000ull : 5000000000ull;
        if (reclaimed && level <= last_level && now >= last_reclaim) {
            const uint64_t baseline = std::max(resident_before, resident_after);
            // Resident jitters by ~±80 MiB around the baseline; a 64 MiB growth
            // test kept re-arming the loop and repeated a reclaim that had just
            // been measured as useless, every retry_interval.
            const bool grew = measured && snapshot.resident >= baseline &&
                              snapshot.resident - baseline >= 256 * mib;
            // A large live working set can remain above the RSS threshold.
            // Rebuilding and discarding it every few seconds adds repeated stalls.
            if (measured && !requested && !system_severity(snapshot) && !grew)
                return 0;
            const uint64_t delay = grew || requested ? interval : std::max(interval, retry_interval);
            if (now - last_reclaim < delay)
                return 0;
        }
        reclaimed = true;
        last_reclaim = now;
        last_level = level;
        resident_before = snapshot.resident;
        return level;
    }

    void record_reclaim(const Snapshot &snapshot) {
        resident_after = snapshot.resident ? snapshot.resident : resident_before;
        const bool useful = resident_before >= resident_after &&
                            resident_before - resident_after >= 16 * mib;
        const uint64_t interval = last_level == 2 ? 2000000000ull : 5000000000ull;
        retry_interval = useful ? interval : std::min(uint64_t(30000000000ull),
                                                    std::max(interval, retry_interval) * 2);
        measured = true;
    }
};
} // namespace xbg_memory_pressure
