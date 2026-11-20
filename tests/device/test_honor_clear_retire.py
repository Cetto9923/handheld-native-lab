"""Verify generalized retire_honor_clear_producer matches multi-size clears safely."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
ENGINE = ROOT / 'src/graphics/nvn_vulkan/engine'

PREAMBLE = r'''
#include "xbg_writeback_policy.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <vector>

enum { SITE_GPU_SYNC };
struct SiteScope { SiteScope(int) {} };
#define xbg_logf(...)

struct Buffer {
    void *mapped;
    void invalidate() {}
};
struct Slot {
    bool recording = false;
    bool pending = false;
};
struct Writeback {
    uint64_t addr, bytes;
    Buffer buffer;
    bool indirect = false;
    const void *owner = nullptr;
    const void *slot = nullptr;
};
struct Image {
    struct { uint8_t *mem; struct {size_t size;} L; } source;
};

Slot slots[16];
unsigned slot_index = 0;
bool honor_clear_retire_experiment = true;
bool clear_sync_census = false;
bool capture_active = false;
struct {
    uint32_t vendorID = 0x5143;
    uint32_t deviceID = 0x43030b00;
    uint32_t driverVersion = 0x802fa02e;
} properties;
struct {
    bool active = false;
    bool probe = false;
    bool trace = false;
} capture;
const void *provenance_file = nullptr;
struct {
    struct { uint64_t load() { return 100; } } presents;
} stats;

std::vector<Writeback> pending_writebacks;
std::vector<Image> images;
const Slot *retired_producer = nullptr;

bool retire_through(const Slot *target, bool) {
    retired_producer = target;
    return true;
}

template<class Visitor>
void for_images_overlapping(uint64_t address, size_t bytes, Visitor visit) {
    for (auto &im : images) {
        if (xbg_wb_overlaps(address, bytes, uintptr_t(im.source.mem), im.source.L.size))
            visit(im);
    }
}
'''

CHECK = r'''
int main() {
    Slot slot1{false, true}; // in-flight pending slot
    Slot slot2{false, true};
    slots[slot_index].recording = true;

    // Test 1: 1024-byte clear overlapping 87040-byte and 4096-byte writebacks from slot1
    {
        retired_producer = nullptr;
        pending_writebacks.clear();
        images.clear();
        uint64_t base = 0x6f584b0000ull;
        pending_writebacks.push_back({base, 87040, {}, false, nullptr, &slot1});
        pending_writebacks.push_back({base, 4096, {}, false, nullptr, &slot1});
        retire_honor_clear_producer(base, 1024);
        assert(retired_producer == &slot1);
    }

    // Test 2: Single 2048-byte writeback overlapping 512-byte clear
    {
        retired_producer = nullptr;
        pending_writebacks.clear();
        images.clear();
        uint64_t base = 0x6f584b0000ull;
        pending_writebacks.push_back({base, 2048, {}, false, nullptr, &slot1});
        retire_honor_clear_producer(base + 64, 512);
        assert(retired_producer == &slot1);
    }

    // Test 3: Overlapping writebacks from two different slots -> fallback (no retire)
    {
        retired_producer = nullptr;
        pending_writebacks.clear();
        images.clear();
        uint64_t base = 0x6f584b0000ull;
        pending_writebacks.push_back({base, 4096, {}, false, nullptr, &slot1});
        pending_writebacks.push_back({base, 87040, {}, false, nullptr, &slot2});
        retire_honor_clear_producer(base, 1024);
        assert(retired_producer == nullptr);
    }

    // Test 4: Overlapping writeback belongs to current recording slot -> fallback
    {
        retired_producer = nullptr;
        pending_writebacks.clear();
        images.clear();
        uint64_t base = 0x6f584b0000ull;
        pending_writebacks.push_back({base, 4096, {}, false, nullptr, &slots[slot_index]});
        retire_honor_clear_producer(base, 512);
        assert(retired_producer == nullptr);
    }

    // Test 5: Overlapping writeback with image alias -> fallback
    {
        retired_producer = nullptr;
        pending_writebacks.clear();
        images.clear();
        uint64_t base = 0x6f584b0000ull;
        pending_writebacks.push_back({base, 4096, {}, false, nullptr, &slot1});
        images.push_back({{reinterpret_cast<uint8_t *>(base), {4096}}});
        retire_honor_clear_producer(base, 512);
        assert(retired_producer == nullptr);
    }

    // Test 6: honor_clear_retire_experiment disabled -> fallback
    {
        retired_producer = nullptr;
        honor_clear_retire_experiment = false;
        pending_writebacks.clear();
        images.clear();
        uint64_t base = 0x6f584b0000ull;
        pending_writebacks.push_back({base, 4096, {}, false, nullptr, &slot1});
        retire_honor_clear_producer(base, 512);
        assert(retired_producer == nullptr);
        honor_clear_retire_experiment = true;
    }

    return 0;
}
'''

def extract_retire_fn(source):
    start = source.index('void retire_honor_clear_producer(uint64_t address, uint64_t bytes) {')
    end = source.index('\n}\n', start) + 3
    return source[start:end]

class HonorClearRetireTest(unittest.TestCase):
    def test_generalized_honor_clear_retire(self):
        source = (ENGINE / 'xbg_game.cpp').read_text()
        fn = extract_retire_fn(source)
        code = '\n'.join((PREAMBLE, fn, CHECK))
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary)
            cpp = path / 'test_retire.cpp'
            cpp.write_text(code)
            binary = path / 'test_retire'
            subprocess.run(['c++', '-std=c++17', '-fsanitize=address,undefined',
                            '-I', str(ENGINE), str(cpp), '-o', str(binary)], check=True)
            res = subprocess.run([str(binary)], check=True)
            self.assertEqual(res.returncode, 0)

if __name__ == '__main__':
    unittest.main()
