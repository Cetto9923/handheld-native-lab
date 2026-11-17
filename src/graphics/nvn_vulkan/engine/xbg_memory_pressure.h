#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <limits>

namespace xbg_memory_pressure {
constexpr uint64_t mib = 1024ull * 1024;
constexpr uint64_t poll_interval_ns = 500000000;

struct Snapshot {
    uint64_t resident = 0;
    uint64_t total = 0;
    uint64_t available = 0;
    bool available_known = false;
};

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

inline unsigned severity(const Snapshot &snapshot) {
    // The reported low-memory exits had last RSS samples of 3.04 and 3.64 GiB.
    // Begin reclaiming earlier. System pressure can also trigger below that RSS.
    unsigned level = 0;
    if (snapshot.resident >= 2560 * mib)
        level = 2;
    else if (snapshot.resident >= 2048 * mib)
        level = 1;
    if (snapshot.available_known && snapshot.total && snapshot.available <= snapshot.total) {
        if (snapshot.available <= snapshot.total / 20)
            level = 2;
        else if (snapshot.available <= snapshot.total / 10)
            level = std::max(level, 1u);
    }
    return level;
}

struct Policy {
    uint64_t last_poll = 0;
    uint64_t last_reclaim = 0;
    unsigned last_level = 0;
    bool polled = false;
    bool reclaimed = false;

    bool poll_due(uint64_t now, unsigned requested) {
        if (!requested && polled && now >= last_poll && now - last_poll < poll_interval_ns)
            return false;
        polled = true;
        last_poll = now;
        return true;
    }

    unsigned reclaim_level(uint64_t now, const Snapshot &snapshot, unsigned requested) {
        const unsigned level = std::max(severity(snapshot), std::min(requested, 2u));
        if (!level)
            return 0;
        const uint64_t interval = level == 2 ? 2000000000ull : 5000000000ull;
        if (reclaimed && level <= last_level && now >= last_reclaim &&
            now - last_reclaim < interval)
            return 0;
        reclaimed = true;
        last_reclaim = now;
        last_level = level;
        return level;
    }
};
} // namespace xbg_memory_pressure
