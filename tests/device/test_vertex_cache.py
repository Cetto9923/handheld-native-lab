"""Execute the production vertex/index cache with counted immutable allocations."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class VertexCache(unittest.TestCase):
    def test_content_versions_budget_and_retirement(self):
        source = (ROOT / 'src/graphics/nvn_vulkan/engine/xbg_game.cpp').read_text()
        start = source.index('namespace vcache {')
        cache = source[start:source.index('Buffer snapshot_slow(', start)]
        shutdown = source[source.index('extern "C" int xbg_game_shutdown()'):]
        self.assertLess(shutdown.index('vkQueueWaitIdle('), shutdown.index('destroy_slots();'))
        self.assertLess(shutdown.index('destroy_slots();'), shutdown.index('clear_descriptor_caches();'))
        start = source.index('void clear_descriptor_caches(){')
        clear = source[start:source.index('VkDescriptorSet allocate_set(', start)]
        self.assertIn('vcache::release();', clear)
        start = source.index('Buffer snapshot_slow(uint64_t address,size_t bytes,VkDeviceSize align){')
        snapshots = source[start:source.index('struct Commands {', start)]
        self.assertLess(snapshots.index('pending_view('), snapshots.index('cpu_access('))
        self.assertLess(snapshots.index('cpu_access('), snapshots.index('vcache::lookup('))
        fixture = r'''
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>
using VkDeviceSize = uint64_t;
uint64_t slot_serial = 100;
struct { std::atomic<uint64_t> presents{100}; } stats;
bool tracked_memory = false, written = false;
unsigned protected_calls = 0;
int dirty_since(const void *, size_t, uint64_t) { return written; }
int dirty_tracked(const void *, size_t) { return tracked_memory; }
void dirty_protect(const void *, size_t) { ++protected_calls; }
uint64_t dirty_now() { return slot_serial; }
int (*nvn_dirty_since)(const void *, size_t, uint64_t) = nullptr;
int (*nvn_dirty_tracked)(const void *, size_t) = nullptr;
void (*nvn_dirty_protect)(const void *, size_t) = nullptr;
uint64_t (*nvn_dirty_now)() = nullptr;
namespace wtrack {
    struct Region { std::atomic<uint64_t> begin{0}, end{0}; };
    std::atomic<unsigned> nregions{0};
    Region regions[1];
}
size_t live_allocations = 0;
struct ArenaChunk {
    std::shared_ptr<std::vector<uint8_t>> storage;
};
struct Buffer {
    void *mapped = nullptr;
    uintptr_t buffer = 0;
    void flush() {}
};
struct Arena {
    std::vector<ArenaChunk> chunks;
    size_t current = 0;
    VkDeviceSize used_total = 0, peak = 0;
    Buffer alloc(size_t bytes) {
        auto data = std::make_shared<std::vector<uint8_t>>(bytes);
        ++live_allocations;
        chunks.push_back({data});
        used_total += bytes;
        peak = std::max(peak, used_total);
        return {data->data(), reinterpret_cast<uintptr_t>(data->data())};
    }
    static void destroy(ArenaChunk &chunk) {
        assert(chunk.storage && live_allocations);
        chunk.storage.reset(); --live_allocations;
    }
    void release() {
        for (auto &chunk : chunks) destroy(chunk);
        chunks.clear(); current = 0; used_total = 0;
    }
};
std::vector<std::function<void()>> retired;
void retire_handle(std::function<void()> callback) { retired.push_back(std::move(callback)); }
void drain_retired() { for (auto &callback : retired) callback(); retired.clear(); }
''' + cache + r'''
void trackers(bool enable) {
    nvn_dirty_since = enable ? dirty_since : nullptr;
    nvn_dirty_tracked = enable ? dirty_tracked : nullptr;
    nvn_dirty_protect = enable ? dirty_protect : nullptr;
    nvn_dirty_now = enable ? dirty_now : nullptr;
}
int main(int argc, char **argv) {
    assert(argc == 2);
    int test = std::atoi(argv[1]);
    std::vector<uint8_t> data(512, 7);
    uint64_t address = reinterpret_cast<uintptr_t>(data.data());
    Buffer first, second;
    if (test == 0) {
        // The port's lone dirty_since stub must never certify unchanged data.
        nvn_dirty_since = dirty_since;
        assert(vcache::lookup(address, data.size(), first));
        assert(vcache::mirror_bytes == data.size());
        assert(vcache::lookup(address, data.size(), second));
        assert(second.buffer == first.buffer && vcache::hits == 1);
        slot_serial += 8; stats.presents += 8;
        data.front() = 9;
        assert(vcache::lookup(address, data.size(), second));
        assert(second.buffer != first.buffer);
        assert(static_cast<uint8_t *>(first.mapped)[0] == 7);
        assert(!std::memcmp(second.mapped, data.data(), data.size()));
        slot_serial += 8; stats.presents += 8;
        data.back() = 11;
        assert(vcache::lookup(address, data.size(), second));
        assert(!std::memcmp(second.mapped, data.data(), data.size()));
        assert(vcache::refills == 2 && protected_calls == 0);
        assert(vcache::comparisons == 3 && vcache::compared_bytes == 1536);
    } else if (test == 1) {
        assert(vcache::lookup(address, data.size(), first));
        // Many submissions in one frame must still classify as dynamic.
        slot_serial += 100; ++stats.presents; data[300] = 8;
        assert(!vcache::lookup(address, data.size(), second));
        assert(vcache::mirror_bytes == 0 && vcache::volatiles == 1);
        assert(static_cast<uint8_t *>(first.mapped)[300] == 7);
        slot_serial += 599;
        assert(!vcache::lookup(address, data.size(), second));
        ++slot_serial;
        assert(vcache::lookup(address, data.size(), second));
        assert(!std::memcmp(second.mapped, data.data(), data.size()));
    } else if (test == 2) {
        trackers(true); tracked_memory = true;
        assert(vcache::lookup(address, data.size(), first));
        assert(protected_calls == 1 && vcache::mirror_bytes == 0);
        assert(vcache::lookup(address, data.size(), second));
        assert(first.buffer == second.buffer && vcache::comparisons == 0);
        slot_serial += 8; written = true; data[30] = 2;
        assert(vcache::lookup(address, data.size(), second));
        assert(protected_calls == 2 && first.buffer != second.buffer);
        assert(static_cast<uint8_t *>(first.mapped)[30] == 7);
        assert(static_cast<uint8_t *>(second.mapped)[30] == 2);
    } else if (test == 3) {
        trackers(true); tracked_memory = false;
        assert(vcache::lookup(address, data.size(), first));
        assert(protected_calls == 0 && vcache::mirror_bytes == data.size());
        assert(vcache::lookup(address, data.size(), second));
        assert(vcache::comparisons == 1);
        // Erase the CPU mirror on pool deletion while GPU reads retain theirs.
        vcache::drop_pool(address + 100, 1);
        assert(vcache::live() == 0 && vcache::mirror_bytes == 0);
        data[0] = 6;
        assert(vcache::lookup(address, data.size(), second));
        assert(first.buffer != second.buffer);
        assert(static_cast<uint8_t *>(first.mapped)[0] == 7);
        assert(static_cast<uint8_t *>(second.mapped)[0] == 6);
        vcache::drop_pool(address + 512, 1);
        assert(vcache::live() == 1); // Nonoverlapping adjacent pool.
    } else if (test == 4) {
        constexpr size_t size = 24ull << 20;
        std::vector<uint8_t> large(size, 1), another(size, 2);
        uint64_t a = reinterpret_cast<uintptr_t>(large.data());
        uint64_t b = reinterpret_cast<uintptr_t>(another.data());
        assert(vcache::lookup(a, size, first));
        assert(vcache::arena_bytes() + vcache::mirror_bytes == 2 * size);
        assert(vcache::lookup(b, size, second));
        assert(vcache::flushes == 1 && retired.size() == 1);
        assert(vcache::arena_bytes() + vcache::mirror_bytes <= vcache::budget);
        assert(static_cast<uint8_t *>(first.mapped)[size - 1] == 1);
        assert(static_cast<uint8_t *>(second.mapped)[size - 1] == 2);
        assert(live_allocations == 2);
        drain_retired(); assert(live_allocations == 1);
        assert(!vcache::lookup(a, vcache::budget / 2 + 1, second));
        assert(!vcache::lookup(UINT64_MAX - 10, 512, second));
        assert(!vcache::lookup(a, 255, second));
    } else if (test == 5) {
        wtrack::nregions = 1;
        wtrack::regions[0].begin = address;
        wtrack::regions[0].end = address + 512;
        assert(!vcache::lookup(address, 512, first));
        assert(vcache::untracked == 1 && vcache::mirror_bytes == 0);
        wtrack::nregions = 0;
        slot_serial += 600;
        assert(vcache::lookup(address, 512, first));
        vcache::flush();
        assert(vcache::mirror_bytes == 0 && vcache::live() == 0);
        assert(static_cast<uint8_t *>(first.mapped)[511] == 7);
        drain_retired(); assert(live_allocations == 0);
        assert(vcache::lookup(address, 512, second));
    } else if (test == 6) {
        for (unsigned i = 0; i < 1000; ++i) {
            data[0] = static_cast<uint8_t>(i);
            assert(vcache::lookup(address, 512, first));
            vcache::drop_pool(address, 512);
            assert(vcache::mirror_bytes == 0 && vcache::live() == 0);
            vcache::flush(); drain_retired();
            assert(live_allocations == 0);
        }
    } else if (test == 7) {
        assert(vcache::lookup(address, 512, first));
        slot_serial += 100; ++stats.presents; data[30] = 9;
        assert(!vcache::lookup(address, 512, second));
        vcache::flush();
        assert(vcache::live() == 1 && vcache::mirror_bytes == 0);
        assert(!vcache::lookup(address, 512, second));
        assert(static_cast<uint8_t *>(first.mapped)[30] == 7);
        drain_retired(); assert(live_allocations == 0);
        slot_serial += 600; stats.presents += 10;
        assert(vcache::lookup(address, 512, second));
        assert(static_cast<uint8_t *>(second.mapped)[30] == 9);
    } else {
        assert(test == 8);
        assert(vcache::lookup(address, 512, first));
        for (size_t i = 1; i < vcache::max_entries; ++i)
            vcache::map.emplace(std::make_pair(uint64_t(i), uint64_t(512)), vcache::Entry{});
        assert(vcache::live() == vcache::max_entries);
        // A full metadata table still serves existing entries without growth.
        assert(vcache::lookup(address, 512, second));
        assert(first.buffer == second.buffer);
        assert(!vcache::lookup(address + 1, 512, second));
        assert(vcache::live() == vcache::max_entries);
    }
    vcache::release(); drain_retired();
    assert(vcache::mirror_bytes == 0 && vcache::live() == 0);
    assert(vcache::arena_bytes() == 0 && live_allocations == 0);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / 'test.cpp').write_text(fixture)
            subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', str(path / 'test.cpp'),
                            '-o', str(path / 'test')], check=True)
            for case in range(9):
                with self.subTest(case=case):
                    subprocess.run([str(path / 'test'), str(case)], check=True)


if __name__ == '__main__':
    unittest.main()
