"""Exercise the production pending-view and GPU-copy paths across slot reuse."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ENGINE = Path(__file__).resolve().parents[2] / 'src/graphics/nvn_vulkan/engine/xbg_game.cpp'

HARNESS = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>
#include "xbg_writeback_policy.h"

using VkDeviceSize = uint64_t;
struct Buffer {
    unsigned buffer = 0;
    uint64_t offset = 0, size = 0;
    void *mapped = nullptr;
};
struct Range { unsigned buffer; uint64_t begin, end; };
struct Slot {
    unsigned cmd;
    bool recording = false, xview = false;
    std::vector<Range> viewed;
    struct { std::vector<std::shared_ptr<void>> objects; } retire;
};
struct PendingWriteback {
    uint64_t addr, bytes;
    Buffer buffer;
    Slot *slot;
    std::shared_ptr<void> owner{};
    uint64_t version = 0;
};
Slot slots[2]{};
unsigned slot_index = 0;
std::vector<PendingWriteback> pending_writebacks;
struct Report { uint64_t addr; };
struct NVNcounterData { uint64_t count, timestamp; };
std::vector<Report> pending_reports;
uint8_t storage[2][64]{};
bool live[3] = {false, true, true};
unsigned opens = 0, xviews = 0;
unsigned retired_chunk[2]{};
bool stale_copy = false;
struct { unsigned pending_views = 0; } perf;
struct Counter { unsigned load() const { return 1; } };
struct { Counter presents; } stats;
void xbg_logf(const char *, ...) {}
// Reopening a previously submitted ring slot waits, resolves its writebacks,
// and can trim the old underlying readback chunk before recording new work.
Slot &recording_slot() {
    Slot &slot = slots[slot_index];
    if (slot.recording) return slot;
    ++opens;
    for (const auto &write : pending_writebacks)
        if (write.slot == &slot && !write.owner) retired_chunk[slot_index] = write.buffer.buffer;
    if (retired_chunk[slot_index]) live[retired_chunk[slot_index]] = false;
    pending_writebacks.erase(std::remove_if(pending_writebacks.begin(), pending_writebacks.end(),
        [&](const auto &write) { return write.slot == &slot; }), pending_writebacks.end());
    slot.recording = true;
    return slot;
}
void preflight_cpu_reads() {}
void invalidate_cpu_range(uint64_t address, uint64_t bytes) {
    for (const auto &report : pending_reports) {
        if (!xbg_wb_overlaps(address, bytes, report.addr, sizeof(NVNcounterData))) continue;
        // cpu_access(why=4) must resolve the pending report before a CPU write.
        // resolve_pending submits the recording consumer (advancing the ring)
        // and retires the producer. Its chunk can be trimmed on the next open.
        if (slots[slot_index].recording) {
            slots[slot_index].recording = false;
            slot_index = (slot_index + 1) % 2;
        }
        for (const auto &write : pending_writebacks)
            if (!write.owner) retired_chunk[write.slot - slots] = write.buffer.buffer;
        pending_writebacks.clear();
        return;
    }
}
void end_open_pass() {}
void full_barrier(unsigned) {}
struct VkBufferCopy { uint64_t srcOffset, dstOffset, size; };
Buffer readback_buffer(uint64_t size) { return {2, 0, size, storage[1]}; }
void vkCmdCopyBuffer(unsigned, unsigned src, unsigned, unsigned, const VkBufferCopy *) {
    stale_copy |= !live[src];
}
'''

SCENARIOS = r'''
int main() {
    constexpr uint64_t address = 0x1000;
    // Whole GPU-copy entry: lookup, then command recording on a wrapped ring.
    pending_writebacks.push_back({address, 32, {1, 0, 32, storage[0]}, &slots[0]});
    bool copied = gpu_buffer_copy(address, 0x2000, 16);
    if (stale_copy) {
        std::fprintf(stderr, "Recorded a GPU copy from a retired readback buffer\n");
        return 9;
    }
    assert(!copied && opens == 1 && !live[1] && pending_writebacks.empty());
    // A producer in another slot survives opening the consumer's slot. Verify
    // the normal lookup result, the borrowed range, and independent-owner pin.
    slots[0].recording = false;
    pending_writebacks.push_back({address, 32, {2, 4, 32, storage[1]}, &slots[1], {}, 17});
    Buffer out;
    uint64_t version = 0;
    assert(pending_view(address + 8, 16, 4, out, &version));
    assert(out.buffer == 2 && out.offset == 12 && out.size == 16);
    assert(out.mapped == storage[1] + 8 && version == 17);
    assert(slots[0].xview && slots[1].viewed.size() == 1);
    assert(slots[1].viewed[0].begin == 12 && slots[1].viewed[0].end == 28);
    assert(!pending_view(address + 1, 8, 4, out));
    assert(!pending_view(address + 20, 16, 4, out));
    // Destination invalidation can drain the producer after the source lookup.
    // Check a partial overlap at each end and a fully covered report.
    constexpr uint64_t dst = 0x2000;
    for (uint64_t start : {dst - 8, dst, dst + 12}) {
        pending_reports = {{start}};
        if (gpu_buffer_copy(address, dst, 16) || stale_copy) return 10;
        assert(live[2] && pending_writebacks.size() == 1);
    }
    pending_reports.clear();
    auto owner = std::make_shared<int>(3);
    pending_writebacks.back().owner = owner;
    assert(pending_view(address, 16, 4, out));
    assert(slots[0].retire.objects.size() == 1 && slots[0].retire.objects[0] == owner);
}
'''


def definition(source, signature):
    start = source.index(signature)
    return source[start:source.index('\n}', start) + 2]


class PendingViewLifetime(unittest.TestCase):
    def test_gpu_copy_after_slot_wrap(self):
        source = ENGINE.read_text()
        pending_view = definition(source, 'bool pending_view(')
        gpu_copy = definition(source, 'bool gpu_buffer_copy(')
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            cpp = path / 'pending_view.cpp'
            cpp.write_text('\n'.join([HARNESS, pending_view, gpu_copy, SCENARIOS]))
            binary = path / 'pending_view'
            subprocess.run(['clang++', '-std=c++17', '-fsanitize=address,undefined',
                            '-Wall', '-Wextra', '-Werror', '-I', str(ENGINE.parent),
                            str(cpp), '-o', str(binary)],
                           check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()
