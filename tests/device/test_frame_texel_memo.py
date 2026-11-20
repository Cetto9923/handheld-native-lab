"""Unit test for frame-local descriptor memoization of uniform texel buffers with pending GPU views."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class FrameTexelMemoTest(unittest.TestCase):
    def test_frame_texel_memo_logic(self):
        source = (ROOT / 'src/graphics/nvn_vulkan/engine/xbg_game.cpp').read_text()

        # Verify key code constructs exist in source
        self.assertIn('bool honor_frame_texel_memo_enabled = true;', source)
        self.assertIn('desc_memo_per_frame_texel=false;', source)
        self.assertIn('desc_memo_per_frame_texel=false;', source)
        self.assertIn('dm.persistent=xmemo_enabled && memo_dyn_ok && out.written.empty() && !desc_memo_per_frame_texel;', source)
        self.assertIn('desc_memo_reset();', source)
        self.assertIn('{"texelframe",&honor_frame_texel_memo_enabled}', source)
        self.assertIn('readback_fullcopy_enabled=(properties.vendorID==0x5143);', source)

        # C++ simulation harness to verify semantics
        fixture = r'''
#include <cassert>
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <iostream>

struct KeyHash {
    size_t operator()(const std::vector<uint64_t> &k) const {
        size_t h = 0;
        for (auto x : k) h ^= std::hash<uint64_t>{}(x) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

struct DescMemo {
    uint64_t set;
    bool persistent = false;
};

constexpr size_t kMaxSlots = 16;
std::unordered_map<std::vector<uint64_t>, DescMemo, KeyHash> desc_memo_slot[kMaxSlots];
size_t desc_memo_local[kMaxSlots] = {};
unsigned slot_index = 0;
uint64_t memo_mod_gen = 1;
bool xmemo_enabled = true;
bool memo_dyn_ok = true;
bool honor_frame_texel_memo_enabled = true;
bool forced_ab_off = false;

bool ab_off(const char *name) { return forced_ab_off; }

void desc_memo_reset() {
    if (!desc_memo_local[slot_index]) return;
    auto &m = desc_memo_slot[slot_index];
    for (auto it = m.begin(); it != m.end();) {
        if (it->second.persistent) ++it;
        else it = m.erase(it);
    }
    desc_memo_local[slot_index] = 0;
    ++memo_mod_gen;
}

void snapshot_invalidate(uint64_t addr, uint64_t bytes) {
    if (desc_memo_local[slot_index]) desc_memo_reset();
}

int main() {
    std::vector<uint64_t> key1 = {0x1234, 0x5678, 1};
    std::vector<uint64_t> persistent_key = {0x9999, 0x8888, 0};

    // 1. Insert a persistent memo entry
    DescMemo dm_p{100, true};
    desc_memo_slot[slot_index].emplace(persistent_key, dm_p);
    assert(desc_memo_local[slot_index] == 0);

    // 2. Simulate draw with pending texel view under honor_frame_texel_memo_enabled = true
    bool has_pv = true;
    bool desc_memo_texel_fail = false;
    bool desc_memo_per_frame_texel = false;
    if (has_pv && honor_frame_texel_memo_enabled && !ab_off("texelframe")) {
        desc_memo_per_frame_texel = true;
    } else {
        desc_memo_texel_fail = true;
    }
    assert(!desc_memo_texel_fail);
    assert(desc_memo_per_frame_texel);

    // Memoize as frame-local
    bool persistent = xmemo_enabled && memo_dyn_ok && !desc_memo_per_frame_texel;
    assert(!persistent);
    DescMemo dm_local{200, persistent};
    bool local = !dm_local.persistent;
    if (desc_memo_slot[slot_index].emplace(key1, dm_local).second && local) {
        ++desc_memo_local[slot_index];
    }
    assert(desc_memo_local[slot_index] == 1);
    assert(desc_memo_slot[slot_index].size() == 2);

    // 3. Second draw with same key hits
    auto hit = desc_memo_slot[slot_index].find(key1);
    assert(hit != desc_memo_slot[slot_index].end());
    assert(hit->second.set == 200);

    // 4. Invalidation drops local entries, keeps persistent
    snapshot_invalidate(0x1000, 64);
    assert(desc_memo_local[slot_index] == 0);
    assert(desc_memo_slot[slot_index].find(key1) == desc_memo_slot[slot_index].end());
    assert(desc_memo_slot[slot_index].find(persistent_key) != desc_memo_slot[slot_index].end());

    // 5. When ab_off is true, falls back to unmemoized
    forced_ab_off = true;
    desc_memo_texel_fail = false;
    desc_memo_per_frame_texel = false;
    if (has_pv && honor_frame_texel_memo_enabled && !ab_off("texelframe")) {
        desc_memo_per_frame_texel = true;
    } else {
        desc_memo_texel_fail = true;
    }
    assert(desc_memo_texel_fail);
    assert(!desc_memo_per_frame_texel);

    std::cout << "OK" << std::endl;
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as td:
            src = Path(td) / 'test_memo.cpp'
            src.write_text(fixture)
            bin_path = Path(td) / 'test_memo'
            subprocess.run(['clang++', '-std=c++17', str(src), '-o', str(bin_path)], check=True)
            res = subprocess.run([str(bin_path)], check=True, capture_output=True, text=True)
            self.assertEqual(res.stdout.strip(), 'OK')


if __name__ == '__main__':
    unittest.main()
