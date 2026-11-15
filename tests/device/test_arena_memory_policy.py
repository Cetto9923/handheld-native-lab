"""Run production arena trimming against counted resource destruction."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'src/graphics/nvn_vulkan/engine/xbg_game.cpp'


class ArenaMemoryPolicy(unittest.TestCase):
    def run_texel_collection(self, restore_bug=False):
        source = SOURCE.read_text()
        line = next(line.strip() for line in source.splitlines()
                    if 'if(texel_memo_enabled &&' in line and 'texel_evict(slot_index,false)' in line)
        if restore_bug:
            line = 'if(texel_memo_enabled && (slot_serial&15)<2)texel_evict(slot_index,false);'
        recording = source[source.index('Slot &recording_slot(){'):source.index('void submit_slots(', source.index('Slot &recording_slot(){'))]
        self.assertLess(recording.index('retire_slot(s)'), recording.index('texel_evict(slot_index,false)'))
        fixture = r'''
#include <array>
#include <cassert>
#include <cstdint>
unsigned slot_count=16,slot_index=0;
uint64_t slot_serial=1;
bool texel_memo_enabled=true;
std::array<unsigned,16> visits{};
std::array<bool,16> pending{};
void texel_evict(unsigned index,bool all) {
    assert(!all && !pending[index]);
    ++visits[index];
}
void poll() {
''' + line + r'''
}
int main() {
    for(slot_count=2;slot_count<=16;++slot_count) {
        visits={};slot_index=0;slot_serial=1;
        for(unsigned step=0;step<512*slot_count;++step) {
            pending[slot_index]=false;
            poll();
            pending[slot_index]=true;
            slot_index=(slot_index+1)%slot_count;
            ++slot_serial;
        }
        for(unsigned index=0;index<slot_count;++index)assert(visits[index]==64);
    }
    visits={};texel_memo_enabled=false;slot_count=16;
    for(slot_index=0;slot_index<slot_count;++slot_index)poll();
    for(unsigned count:visits)assert(count==0);
}
'''
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'texel.cpp'
            path.write_text(fixture)
            binary = Path(temporary) / 'texel'
            subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', str(path), '-o', str(binary)],
                           check=True, capture_output=True, text=True)
            return subprocess.run([str(binary)], capture_output=True, text=True)

    def test_texel_collection_reaches_every_configured_slot(self):
        result = self.run_texel_collection()
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_fixed_serial_phase_starvation_is_detected(self):
        result = self.run_texel_collection(restore_bug=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('visits[index]==64', result.stderr)

    def test_guarded_buffers_survive_and_unused_resources_are_invalidated(self):
        source = SOURCE.read_text()
        policy = source.index('constexpr VkDeviceSize arena_chunk_bytes=')
        start = source.rindex('#if defined(__ANDROID__)', 0, policy)
        constants = source[start:source.index('constexpr VkDeviceSize arena_flush_bytes=', start)]
        start = source.index('    template<class Protected> void trim(')
        methods = source[start:source.index('    void release()', start)]
        fixture = r'''
#include <cassert>
#include <cstdint>
#include <set>
#include <vector>
using VkBuffer = uint64_t;
using VkDeviceSize = uint64_t;
struct { int device = 1; } cfg;
std::set<uint64_t> destroyed, freed, invalidated;
void memo_forget_buffer(VkBuffer buffer) { assert(invalidated.insert(buffer).second); }
void vkUnmapMemory(int device, uint64_t memory) { assert(device == 1 && memory); }
void vkDestroyBuffer(int device, uint64_t buffer, void *) {
    assert(invalidated.count(buffer));
    assert(device == 1 && destroyed.insert(buffer).second);
}
void vkFreeMemory(int device, uint64_t memory, void *) {
    assert(device == 1 && freed.insert(memory).second);
}
struct ArenaChunk {
    uint64_t buffer, memory;
    uint8_t *mapped;
    VkDeviceSize size;
};
'''
        body = r'''
struct Arena {
    std::vector<ArenaChunk> chunks;
    size_t current = 7, keep_chunks = 1;
''' + methods + r'''
};
int main() {
    Arena arena;
    for (unsigned i = 1; i <= 4; ++i)
        arena.chunks.push_back({i, i + 100, reinterpret_cast<uint8_t *>(1),
                               i == 3 ? 2 * arena_chunk_bytes : arena_chunk_bytes});
    arena.trim([](VkBuffer buffer) { return buffer == 2 || buffer == 3; });
    assert(arena.current == 0 && arena.chunks.size() == 3);
    assert(arena.chunks[0].buffer == 1 && arena.chunks[1].buffer == 2 &&
           arena.chunks[2].buffer == 3);
    assert(destroyed == std::set<uint64_t>{4});
    assert(invalidated == std::set<uint64_t>{4});
    arena.keep_chunks = 0;
    arena.trim([](VkBuffer buffer) { return buffer == 3; });
    assert(arena.chunks.size() == 1 && arena.chunks[0].buffer == 3);
    assert((invalidated == std::set<uint64_t>{1, 2, 4}));
    arena.trim();
    assert(arena.chunks.empty() && arena.current == 0);
    assert((invalidated == std::set<uint64_t>{1, 2, 3, 4}));
    assert((destroyed == std::set<uint64_t>{1, 2, 3, 4}));
    assert((freed == std::set<uint64_t>{101, 102, 103, 104}));
}
'''
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            for android, expected in [(True, 4), (False, 32)]:
                platform = '#define __ANDROID__ 1\n' if android else ''
                policy_assertion = f'static_assert(arena_chunk_bytes == {expected}ull * 1024 * 1024);\n'
                code = directory / 'arena.cpp'
                code.write_text(fixture + platform + constants + policy_assertion + body)
                binary = directory / 'arena'
                subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                                '-fsanitize=address,undefined', str(code), '-o', str(binary)],
                               check=True, capture_output=True, text=True)
                subprocess.run([str(binary)], check=True, capture_output=True, text=True)
