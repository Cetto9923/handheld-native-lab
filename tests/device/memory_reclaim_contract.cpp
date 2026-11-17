#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <memory>
#include <vector>

unsigned phase = 0, unpinned = 0, destroyed_pools = 0, pipeline_clears = 0, warm_trims = 0;
bool expect_preserved_warm = false;
using VkBuffer = int;
struct Arena {
    size_t keep_chunks = 1;
    std::vector<int> buffers;
    void release() {
        assert(phase == 3 && unpinned == 4);
        buffers.clear();
    }
    template <class Protect> void trim(Protect protect) {
        assert(phase == 3 && !keep_chunks);
        buffers.erase(std::remove_if(buffers.begin(), buffers.end(),
                                     [&](int buffer) { return !protect(buffer); }),
                      buffers.end());
    }
};
struct Slot {
    bool pending = false, recording = false;
    struct {
        std::atomic<unsigned> n{0};
    } waiters;
    std::vector<int> copy_readers;
    std::map<int, int> set_cache, spare_sets, used_sets;
    std::vector<int> pools;
    size_t pool_next = 1;
    Arena stream, uniform, readback;
    struct XRange {
        int buffer;
    };
    std::vector<XRange> guard;
};
std::array<Slot, 4> slots;
unsigned slot_count = 4;
uint64_t slot_serial = 100;
auto &active_slots() {
    return slots;
}
void complete_all() {
    assert(phase == 0);
    phase = 1;
}
namespace poolreset {
void drain() {
    assert(phase == 1);
    phase = expect_preserved_warm ? 3 : 2;
}
} // namespace poolreset
namespace warm {
std::atomic<bool> memory_constrained{false};
std::atomic<unsigned> active_workers{0};
std::condition_variable async_cv;
size_t trim_for_memory_pressure(bool idle) {
    ++warm_trims;
    if (!idle) {
        assert(phase == 0);
        memory_constrained.store(true);
        return 7;
    }
    assert(phase == 2 && memory_constrained.load());
    phase = 3;
    return 0;
}
} // namespace warm
std::vector<int> img_cache, sampled_rg16_images, storage_rg16_images, image_index;
struct Image {
    bool gpu_written;
    uint64_t slot_use;
    int64_t mem_bytes;
};
std::map<int, std::unique_ptr<Image>> images;
bool image_index_dirty = false;
uint64_t images_epoch = 0, memo_check_gen = 0, memo_epoch = 0;
struct {
    int device;
} cfg;
void xbg_logf(const char *, ...) {}
void memo_unpin_all(unsigned) {
    assert(phase == 3);
    ++unpinned;
}
void snapshot_reset() {
    assert(unpinned == slot_count);
}
void texel_evict(unsigned i, bool all) {
    assert(all && unpinned == slot_count && !slots[i].pending && !slots[i].recording);
}
void vkDestroyDescriptorPool(int, int, void *) {
    assert(phase == 3 && unpinned == slot_count);
    ++destroyed_pools;
}
void clear_pipeline_objects() {
    assert(phase == 3);
    ++pipeline_clears;
}

#include PRODUCTION_HEADER

void initialize() {
    phase = unpinned = destroyed_pools = pipeline_clears = warm_trims = 0;
    expect_preserved_warm = false;
    warm::memory_constrained = false;
    image_index_dirty = false;
    images_epoch = memo_check_gen = memo_epoch = 0;
    images.clear();
    for (unsigned i = 0; i < slots.size(); ++i) {
        Slot &slot = slots[i];
        slot.pending = slot.recording = false;
        slot.waiters.n = 0;
        slot.copy_readers.clear();
        slot.pools = {int(i)};
        slot.stream.buffers = {10 + int(i)};
        slot.uniform.buffers = {20 + int(i)};
        slot.readback.buffers = {30 + int(i), 40 + int(i)};
        slot.readback.keep_chunks = 1;
        slot.guard = {{40 + int(i)}};
    }
    images.emplace(1, std::make_unique<Image>(Image{true, 0, 4096}));
    images.emplace(2, std::make_unique<Image>(Image{false, 99, 4096}));
    images.emplace(3, std::make_unique<Image>(Image{false, 92, 8192}));
    images.emplace(4, std::make_unique<Image>(Image{false, 101, 4096}));
}

int main() {
    for (unsigned level : {1u, 2u}) {
        initialize();
        reclaim_memory_caches(level);
        if (!images.count(1))
            return 9;
        assert(images.count(2) && !images.count(3) && images.count(4));
        assert(image_index_dirty && images_epoch == 1 && memo_check_gen == 1);
        assert(destroyed_pools == 4 && pipeline_clears == (level == 2));
        assert(warm_trims == 2 && warm::memory_constrained);
        for (unsigned i = 0; i < slots.size(); ++i) {
            assert(slots[i].stream.buffers.empty() && slots[i].uniform.buffers.empty());
            assert((slots[i].readback.buffers == std::vector<int>{40 + int(i)}));
            assert(slots[i].readback.keep_chunks == 1);
        }
    }
    initialize();
    expect_preserved_warm = true;
    reclaim_memory_caches(1, true);
    assert(warm_trims == 0 && !warm::memory_constrained);
    assert(destroyed_pools == 4 && pipeline_clears == 0);
    assert(images.count(1) && images.count(2) && !images.count(3) && images.count(4));
    assert(image_index_dirty && images_epoch == 1 && memo_check_gen == 1);
    for (unsigned state = 0; state < 4; ++state) {
        initialize();
        if (state == 0)
            slots[2].pending = true;
        if (state == 1)
            slots[2].recording = true;
        if (state == 2)
            slots[2].waiters.n = 1;
        if (state == 3)
            slots[2].copy_readers.push_back(1);
        reclaim_memory_caches(2);
        if (unpinned || destroyed_pools || images.size() != 4 || pipeline_clears)
            return 11;
    }
}
