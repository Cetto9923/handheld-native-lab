"""Test gpu_clear_pending_buffer on Honor GT Pro device profile with cross-slot / subset buffer."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
ENGINE = ROOT / 'src/graphics/nvn_vulkan/engine'

HARNESS = r'''
#include "xbg_writeback_policy.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <memory>
#include <vector>

using VkDeviceSize = uint64_t;

constexpr size_t producer_size = 87040;
constexpr size_t clear_size = 512;
constexpr uint64_t guest_base = 0x6f98ea0000ull;

struct Buffer {
    int buffer;
    uint64_t offset, size;
    void *mapped;
};

std::array<uint8_t, producer_size> gpu_mem;
std::array<uint8_t, producer_size> guest_mem;

enum { fill_op, barrier_op, finish_op };
std::vector<int> recorded_events;
uint64_t write_seq = 0;
uint64_t pending_writeback_version = 0;

struct VkPhysicalDeviceProperties { unsigned vendorID, deviceID, driverVersion; };
VkPhysicalDeviceProperties properties{0x5143, 0x43030b00, 0x802fa02e}; // ROD2-W69
bool ordered_buffer_clear_enabled = true;
bool honor_pipeline_diagnostics = false;
bool provenance_file = false;
struct { bool active, probe, trace; } capture{};

bool ab_off(const char *) { return false; }
void xbg_logf(const char *, ...) {}

struct Stats {
    struct Counter { uint64_t load() const { return 1; } } presents;
} stats;
unsigned xviews = 0;

struct Range { int buffer; uint64_t begin, end; };
struct Slot {
    bool recording = true;
    bool xview = false;
    std::vector<Range> viewed;
    struct { std::vector<void *> objects; } retire;
} slots[2];
unsigned slot_index = 1; // current recording slot is 1

struct OwnedIndirect { uint64_t address = 0, bytes = 0; };

struct PendingWriteback {
    uint64_t addr, bytes;
    Buffer buffer;
    const Slot *slot;
    void *owner = nullptr;
    std::shared_ptr<OwnedIndirect> indirect{};
    uint64_t version = 1;
};
std::vector<PendingWriteback> pending_writebacks;

struct Report { uint64_t addr; };
struct NVNcounterData { uint64_t value, ts; };
std::vector<Report> pending_reports;

struct { uint64_t ordered_buffer_clears = 0, pending_views = 0; } perf;

uint64_t invalidated_addr = 0, invalidated_size = 0;
void snapshot_invalidate(uint64_t address, uint64_t bytes) {
    invalidated_addr = address;
    invalidated_size = bytes;
}

Slot &recording_slot() { return slots[slot_index]; }

struct Image {};
template<class Callback>
void for_images_overlapping(uint64_t, uint64_t, Callback) {}

// Vulkan mock types
constexpr unsigned VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER = 1;
constexpr unsigned VK_ACCESS_TRANSFER_WRITE_BIT = 2;
constexpr unsigned VK_ACCESS_SHADER_READ_BIT = 4;
constexpr unsigned VK_ACCESS_SHADER_WRITE_BIT = 8;
constexpr unsigned VK_ACCESS_INDIRECT_COMMAND_READ_BIT = 16;
constexpr unsigned VK_ACCESS_TRANSFER_READ_BIT = 32;
constexpr unsigned VK_QUEUE_FAMILY_IGNORED = ~0u;
constexpr unsigned VK_PIPELINE_STAGE_TRANSFER_BIT = 1;
constexpr unsigned VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT = 2;
constexpr unsigned VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT = 4;
constexpr unsigned VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT = 8;

struct VkBufferMemoryBarrier {
    unsigned sType = 0;
    const void *pNext = nullptr;
    unsigned srcAccessMask = 0, dstAccessMask = 0;
    unsigned srcQueueFamilyIndex = 0, dstQueueFamilyIndex = 0;
    int buffer = 0;
    uint64_t offset = 0, size = 0;
};

struct Commands {
    int command = 1;
    void finish() { recorded_events.push_back(finish_op); }
};

void vkCmdFillBuffer(int, int buffer, uint64_t offset, uint64_t size, uint32_t val) {
    assert(buffer == 7);
    assert(offset == 0);
    assert(size == clear_size);
    assert(val == 0);
    recorded_events.push_back(fill_op);
}

void vkCmdPipelineBarrier(int, unsigned, unsigned, unsigned, unsigned, void *,
                          unsigned count, const VkBufferMemoryBarrier *b, unsigned, void *) {
    assert(count == 1);
    assert(b->buffer == 7);
    assert(b->offset == 0);
    assert(b->size == clear_size);
    recorded_events.push_back(barrier_op);
}

struct p_clear_buf {
    uint64_t addr, size;
    uint32_t value;
};
'''

SCENARIO = r'''
int main() {
    guest_base = reinterpret_cast<uint64_t>(guest_mem.data());
    gpu_mem.fill(0xAA);
    guest_mem.fill(0x55);

    // Producer is in slot 0 (cross-slot), clear is executed in slot 1
    Buffer producer_buf{7, 0, producer_size, gpu_mem.data()};
    pending_writebacks.push_back({guest_base, producer_size, producer_buf, &slots[0], nullptr, {}, 42});

    p_clear_buf request{guest_base, clear_size, 0};
    bool handled = gpu_clear_pending_buffer(request);
    assert(handled);

    // Check events
    assert(recorded_events.size() == 3);
    assert(recorded_events[0] == fill_op);
    assert(recorded_events[1] == barrier_op);
    assert(recorded_events[2] == finish_op);

    // Check that gpu_mem (view.mapped) was cleared for first 512 bytes
    for (size_t i = 0; i < clear_size; ++i) {
        assert(gpu_mem[i] == 0);
        assert(reinterpret_cast<uint8_t *>(guest_base)[i] == 0);
    }
    // Check untouched bytes remain untouched
    for (size_t i = clear_size; i < producer_size; ++i) {
        assert(gpu_mem[i] == 0xAA);
        assert(reinterpret_cast<uint8_t *>(guest_base)[i] == 0x55);
    }

    // Check invalidation and perf count
    assert(invalidated_addr == guest_base);
    assert(invalidated_size == clear_size);
    assert(perf.ordered_buffer_clears == 1);

    // Check cross-slot registration
    assert(slots[1].xview);
    assert(slots[0].viewed.size() == 1);
    assert(slots[0].viewed[0].begin == 0 && slots[0].viewed[0].end == clear_size);

    return 0;
}
'''

def definition(source, signature):
    start = source.index(signature)
    return source[start:source.index('\n}', start) + 2]

class TestPendingClearCandidate(unittest.TestCase):
    def test_gpu_clear_pending_buffer(self):
        source = (ENGINE / 'xbg_game.cpp').read_text()
        pending_view = definition(source, 'bool pending_view(')
        gpu_clear = definition(source, 'bool gpu_clear_pending_buffer(')
        harness_fixed = HARNESS.replace('constexpr uint64_t guest_base = 0x6f98ea0000ull;', 'uint64_t guest_base = 0;')
        code = '\n'.join([harness_fixed, pending_view, gpu_clear, SCENARIO])
        with tempfile.TemporaryDirectory() as tmp:
            cpp_file = Path(tmp) / 'test.cpp'
            cpp_file.write_text(code)
            bin_file = Path(tmp) / 'test'
            subprocess.run(['clang++', '-std=c++17', '-fsanitize=address,undefined',
                            '-Wall', '-Wextra', '-Wno-missing-field-initializers',
                            '-I', str(ENGINE),
                            str(cpp_file), '-o', str(bin_file)], check=True)
            res = subprocess.run([str(bin_file)], check=True)
            self.assertEqual(res.returncode, 0)

if __name__ == '__main__':
    unittest.main()
