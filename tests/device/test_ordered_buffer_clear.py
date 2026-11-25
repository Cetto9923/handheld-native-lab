"""Check the command order and writeback publication for the partial clear."""
import unittest

from tests.device.test_pending_buffer_clear import ENGINE, function, run_program


FIXTURE = r'''
#include "xbg_writeback_policy.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <vector>

constexpr size_t result_size = 146432;
constexpr uint64_t guest_base = 0x100000;
struct Buffer { int buffer; size_t offset, size; };
std::array<unsigned char, result_size * 2> memory;
enum { copy_op, transfer_barrier, fill_op, host_barrier, finish_op, publish_op };
std::vector<int> events;
uint64_t write_seq, pending_writeback_version;
struct VkPhysicalDeviceProperties { unsigned vendorID, deviceID, driverVersion; };
VkPhysicalDeviceProperties properties{0x5143,0x44050001,0x80320050};
struct VkBufferCopy { size_t srcOffset, dstOffset, size; };
constexpr unsigned VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER = 1, VK_ACCESS_TRANSFER_WRITE_BIT = 2,
    VK_ACCESS_HOST_READ_BIT = 4, VK_QUEUE_FAMILY_IGNORED = ~0u, VK_PIPELINE_STAGE_TRANSFER_BIT = 8,
    VK_PIPELINE_STAGE_HOST_BIT = 16;
struct VkBufferMemoryBarrier {
    unsigned sType, srcAccessMask{}, dstAccessMask{}, srcQueueFamilyIndex{}, dstQueueFamilyIndex{};
    int buffer{};
    size_t offset{}, size{};
};
struct p_clear_buf { uint64_t addr, size; uint32_t value; };
struct NVNcounterData { uint64_t counter, timestamp; };
struct { bool active, probe, trace; } capture;
bool provenance_file, ordered_buffer_clear_enabled = true, image_alias;
bool honor_small_clear_experiment = false;
bool ab_off(const char *) { return false; }
struct Image {};
struct Slot {
    bool recording = true;
    struct Arena {
        Buffer alloc(size_t n) { assert(n == result_size); return {1, result_size, n}; }
    } readback;
} slots[2];
unsigned slot_index;
struct PendingWriteback {
    uint64_t addr, bytes;
    Buffer buffer;
    const Slot *slot;
    bool owner, indirect;
    uint64_t version;
};
std::vector<PendingWriteback> pending_writebacks;
struct Report { uint64_t addr; };
std::vector<Report> pending_reports;
struct { uint64_t ordered_buffer_clears; } perf;

template<class Callback> void for_images_overlapping(uint64_t address, uint64_t bytes, Callback cb) {
    assert(address == guest_base && bytes == result_size);
    Image image;
    if (image_alias) cb(image);
}
struct Commands { int command = 3; void finish() { events.push_back(finish_op); } };
void full_barrier(int cmd) { assert(cmd == 3 && events.empty()); }
void snapshot_invalidate(uint64_t address, uint64_t bytes) {
    assert(address == guest_base && bytes == result_size);
    assert(pending_writebacks[0].buffer.offset == 0);
    events.push_back(publish_op);
}
void vkCmdCopyBuffer(int cmd, int src, int dst, int count, const VkBufferCopy *range) {
    assert(cmd == 3 && src == 1 && dst == 1 && count == 1);
    assert(range->srcOffset == 0 && range->dstOffset == result_size && range->size == result_size);
    memcpy(memory.data()+range->dstOffset, memory.data()+range->srcOffset, range->size);
    events.push_back(copy_op);
}
void vkCmdPipelineBarrier(int cmd, unsigned src, unsigned dst, int flags, int nMem, const void *,
                          int count, const VkBufferMemoryBarrier *range, int nImage, const void *) {
    assert(cmd == 3 && src == VK_PIPELINE_STAGE_TRANSFER_BIT && !flags && !nMem && !nImage && count == 1);
    assert(range->srcQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED &&
           range->dstQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED);
    assert(range->srcAccessMask == VK_ACCESS_TRANSFER_WRITE_BIT && range->buffer == 1);
    if (dst == VK_PIPELINE_STAGE_TRANSFER_BIT) {
        assert(range->dstAccessMask == VK_ACCESS_TRANSFER_WRITE_BIT);
        assert(range->offset == result_size+8192 && range->size == 8192);
        events.push_back(transfer_barrier);
    } else {
        assert(dst == VK_PIPELINE_STAGE_HOST_BIT && range->dstAccessMask == VK_ACCESS_HOST_READ_BIT);
        assert(range->offset == result_size && range->size == result_size);
        events.push_back(host_barrier);
    }
}
void vkCmdFillBuffer(int cmd, int dst, size_t offset, size_t size, uint32_t value) {
    assert(cmd == 3 && dst == 1 && offset == result_size+8192 && size == 8192);
    for (size_t i = 0; i < size; i += 4) memcpy(memory.data()+offset+i,&value,4);
    events.push_back(fill_op);
}
'''

CHECK = r'''
int main() {
    const VkPhysicalDeviceProperties target{0x5143,0x44050001,0x80320050};
    assert(ordered_clear_device("PKJ110", target, ""));
    assert(!ordered_clear_device("V2301A", target, ""));
    assert(ordered_clear_device("V2301A", target, "SM8750"));
    assert(ordered_clear_device("OPD2413", target, "SM8750P"));
    assert(ordered_clear_device("V2301A", target, "sm8750-ac"));
    assert(ordered_clear_device("V2301A", target, "sm8850"));
    assert(ordered_clear_device("V2301A", target, "SM8850P"));
    assert(!ordered_clear_device("V2301A", target, "SM87500"));
    assert(!ordered_clear_device("V2301A", {0x13b5,0x44050001,0x80320050}, "SM8750"));
    assert(ordered_clear_device("ROD2-W69", {0x5143,0x43030b00,0x802fa02e}, ""));
    assert(!ordered_clear_device("ROD2-W69", {0x5143,0x43030b00,0x802fa02f}, ""));
    assert(!ordered_clear_device("other", {0x5143,0x43030b00,0x802fa02e}, ""));
    assert(!ordered_clear_device("PKJ110", {0x5143,0x44050001,0x8032004b}, ""));
    assert(ordered_clear_device("25098PN5AC", {0x5143,0x44050a31,0x8034a02c}, ""));
    assert(!ordered_clear_device("25098PN5AC", {0x5143,0x44050a31,0x8034a035}, ""));
    assert(ordered_clear_device("RG477M", {0x13b5,0xb8a31030,0x0b001000}, ""));
    assert(!ordered_clear_device("RG477M", {0x13b5,0xb8a31030,0x0b001001}, ""));
    assert(!ordered_clear_device("PMS110", {0x5143,0x44050a31,0x8034a035}, ""));
    assert(!ordered_clear_device("other", {0x13b5,0xb8a31030,0x0b001000}, ""));
    const p_clear_buf request{guest_base+8192,8192,0x1a52f6bf};
    PendingWriteback producer{guest_base,result_size,{1,0,result_size},&slots[0],false,false,40};
    for (int scenario = 0; scenario < 9; ++scenario) {
        std::fill_n(memory.begin(), result_size, 0x66);
        std::fill_n(memory.begin()+result_size, result_size, 0xcb);
        const auto before = memory;
        pending_writebacks = {producer};
        pending_reports.clear();
        events.clear();
        ordered_buffer_clear_enabled = scenario != 1;
        slots[0].recording = scenario != 2;
        image_alias = scenario == 3;
        pending_writebacks[0].slot = scenario == 4 ? &slots[1] : &slots[0];
        pending_writebacks[0].indirect = scenario == 5;
        if (scenario == 6) pending_reports.push_back({guest_base+32768});
        if (scenario == 7) pending_writebacks.push_back({guest_base+32768,4,{2,0,4},&slots[0],false,false,39});
        capture.trace = scenario == 8;
        pending_writeback_version = producer.version;
        if (scenario) {
            assert(!gpu_ordered_buffer_clear(request));
            assert(memory == before && events.empty());
            assert(pending_writebacks[0].version == producer.version);
            continue;
        }
        assert(gpu_ordered_buffer_clear(request));
        assert(std::equal(memory.begin(), memory.begin()+result_size, before.begin()));
        const size_t clear_offset = request.addr - guest_base;
        for (size_t i = 0; i < result_size; i += 4) {
            uint32_t word;
            memcpy(&word,memory.data()+result_size+i,4);
            assert(word == (i >= clear_offset && i < clear_offset + request.size ? request.value : 0x66666666));
        }
        assert((events == std::vector<int>{copy_op,transfer_barrier,fill_op,host_barrier,finish_op,publish_op}));
        assert(pending_writebacks[0].buffer.offset == result_size && pending_writebacks[0].version == 41);
        assert(perf.ordered_buffer_clears == 1);
    }
}
'''


class OrderedBufferClear(unittest.TestCase):
    def test_original_span_and_untouched_bytes_survive(self):
        source = (ENGINE / 'xbg_game.cpp').read_text()
        policy = function(source, 'bool ordered_clear_device(')
        clear = function(source, 'bool gpu_ordered_buffer_clear(')
        self.assertEqual(run_program(FIXTURE + policy + clear + CHECK), 0)
        rg_fixture = FIXTURE.replace('result_size = 146432', 'result_size = 87040')
        rg_fixture = rg_fixture.replace('properties{0x5143,0x44050001,0x80320050}',
                                        'properties{0x13b5,0xb8a31030,0x0b001000}')
        rg_fixture = rg_fixture.replace('8192', '4096')
        rg_check = CHECK.replace('8192', '4096')
        self.assertEqual(run_program(rg_fixture + policy + clear + rg_check), 0)
        honor_fixture = rg_fixture.replace('0x13b5,0xb8a31030,0x0b001000', '0x5143,0x43030b00,0x802fa02e')
        self.assertEqual(run_program(honor_fixture + policy + clear + rg_check), 0)
        small_fixture = honor_fixture.replace('result_size = 87040', 'result_size = 43008')
        small_fixture = small_fixture.replace('4096', '2048')
        small_fixture = small_fixture.replace('honor_small_clear_experiment = false',
                                              'honor_small_clear_experiment = true')
        self.assertEqual(run_program(small_fixture + policy + clear + rg_check.replace('4096', '2048')), 0)
        walk_fixture = honor_fixture.replace('result_size = 87040', 'result_size = 73728')
        check_experiment = rg_check.replace(
            '        if (scenario) {',
            '''        if (!scenario) {
            assert(!gpu_ordered_buffer_clear(request));
            assert(memory == before && events.empty());
            honor_small_clear_experiment = true;
        }
        if (scenario) {''',
        )
        self.assertEqual(run_program(walk_fixture + policy + clear + check_experiment), 0)


if __name__ == '__main__':
    unittest.main()
