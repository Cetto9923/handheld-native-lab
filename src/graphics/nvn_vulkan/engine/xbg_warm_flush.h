// The learned pipeline recipes and the schedule they are written on.
//
// The learned list (vk-pipeline-list.bin) is the device's own record of every
// pipeline it had to build. It is written on its own schedule, independent of
// the VkPipelineCache: a pipeline the driver built in under 2 ms came out of
// the driver's own cache, but the recipe is still worth learning, because the
// next launch (on this or another driver) has to build it again.
//
// The engine used to flush the list only from save_pipeline_cache(), which
// returns early when no pipeline took 2 ms or more. Those recipes were
// recorded in memory and never reached the disk.
//
// This header holds the two decisions so they can be checked on the host:
//   - when the list is due to be written (xbg_warm_flush::due);
//   - how the pending bytes are drained and given back (xbg_warm_flush::Pending).
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

namespace xbg_warm_flush {

// The learned list is written every this many presented frames. The same
// interval the VkPipelineCache uses, but the two decisions are independent.
constexpr uint64_t kIntervalFrames = 300;

// The list is due when the interval has passed since the last write. The
// decision depends on nothing else: no pipeline count, no cache state and no
// compile time. A session that only ever built fast pipelines still writes.
inline bool due(uint64_t presents, uint64_t last_frame) {
    return presents >= last_frame + kIntervalFrames;
}

// The recipes that were learned but not yet written.
class Pending {
public:
    void append(const uint8_t *data, size_t size) {
        if (!data || !size) return;
        std::lock_guard<std::mutex> lock(mu_);
        pending_.insert(pending_.end(), data, data + size);
    }
    bool empty() const {
        std::lock_guard<std::mutex> lock(mu_);
        return pending_.empty();
    }
    size_t size() const {
        std::lock_guard<std::mutex> lock(mu_);
        return pending_.size();
    }
    // Take the bytes to write. Nothing is lost when the write fails: the
    // caller hands them back with restore().
    bool take(std::vector<uint8_t> &out) {
        std::lock_guard<std::mutex> lock(mu_);
        if (pending_.empty()) return false;
        out.swap(pending_);
        return true;
    }
    // Put the bytes back at the front, so the next write tries them again.
    void restore(const std::vector<uint8_t> &data) {
        if (data.empty()) return;
        std::lock_guard<std::mutex> lock(mu_);
        pending_.insert(pending_.begin(), data.begin(), data.end());
    }
private:
    mutable std::mutex mu_;
    std::vector<uint8_t> pending_;
};

// Append the bytes to the file. Returns false when the file could not be
// written, so the caller keeps the recipes for the next attempt.
inline bool append_file(const std::string &path, const std::vector<uint8_t> &data) {
    if (data.empty()) return true;
    FILE *f = std::fopen(path.c_str(), "ab");
    if (!f) return false;
    bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    ok = std::fclose(f) == 0 && ok;
    return ok;
}

}  // namespace xbg_warm_flush
