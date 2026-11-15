"""Run production arena trimming against counted resource destruction."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'src/graphics/nvn_vulkan/engine/xbg_game.cpp'


class ArenaMemoryPolicy(unittest.TestCase):
    def test_guarded_buffers_survive_and_unused_resources_are_invalidated(self):
        source = SOURCE.read_text()
        policy = source.index('constexpr VkDeviceSize arena_chunk_bytes=')
        start = source.rindex('#if defined(__ANDROID__)', 0, policy)
        constants = source[start:source.index('constexpr VkDeviceSize arena_flush_bytes=', start)]
        start = source.index('    template<class Protected> void trim(')
        methods = source[start:source.index('    void release()', start)]
        fixture = r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <set>
#include <vector>
using VkBuffer = uint64_t;
using VkDeviceSize = uint64_t;
std::atomic<uint64_t> memo_epoch{1};
struct { int device = 1; } cfg;
std::set<uint64_t> destroyed, freed;
void vkUnmapMemory(int device, uint64_t memory) { assert(device == 1 && memory); }
void vkDestroyBuffer(int device, uint64_t buffer, void *) {
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
    assert(memo_epoch == 2);
    arena.keep_chunks = 0;
    arena.trim([](VkBuffer buffer) { return buffer == 3; });
    assert(arena.chunks.size() == 1 && arena.chunks[0].buffer == 3);
    assert(memo_epoch == 4);
    arena.trim();
    assert(arena.chunks.empty() && arena.current == 0 && memo_epoch == 5);
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
