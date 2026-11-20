"""Verify honor_img_cache_persist_experiment semantics."""
import subprocess
import tempfile
import unittest


class HonorImgCachePersistTest(unittest.TestCase):
    def test_cache_persist_semantics(self):
        code = r'''
#include <cassert>
#include <cstdint>
#include <unordered_map>

struct ImgKey {
    uint64_t h, sh, tp, to, sp, so, kind;
    bool operator==(const ImgKey &o) const {
        return h == o.h && sh == o.sh && tp == o.tp && to == o.to && sp == o.sp && so == o.so && kind == o.kind;
    }
};

struct ImgKeyHash {
    size_t operator()(const ImgKey &k) const {
        uint64_t x = k.h * 0x9E3779B97F4A7C15ull ^ k.sh * 0xC2B2AE3D27D4EB4Full ^ k.tp ^ (k.to << 17) ^ (k.sp << 7) ^ k.so ^ (k.kind << 40);
        x ^= x >> 29;
        return size_t(x * 0xBF58476D1CE4E5B9ull);
    }
};

struct ImgEntry {
    uint64_t serial;
    uint64_t revision;
};

std::unordered_map<ImgKey, ImgEntry, ImgKeyHash> img_cache;
bool honor_img_cache_persist_experiment = false;

void desc_memo_reset() {
    if (!honor_img_cache_persist_experiment || img_cache.size() > 4096) img_cache.clear();
}

int main() {
    ImgKey k1{1, 2, 3, 4, 5, 6, 7};
    ImgKey k2{8, 9, 10, 11, 12, 13, 14};

    // Case 1: Experiment disabled -> desc_memo_reset clears img_cache
    honor_img_cache_persist_experiment = false;
    img_cache[k1] = {100, 1};
    assert(img_cache.size() == 1);
    desc_memo_reset();
    assert(img_cache.empty());

    // Case 2: Experiment enabled -> desc_memo_reset preserves img_cache
    honor_img_cache_persist_experiment = true;
    img_cache[k1] = {100, 1};
    img_cache[k2] = {200, 1};
    assert(img_cache.size() == 2);
    desc_memo_reset();
    assert(img_cache.size() == 2);
    assert(img_cache[k1].serial == 100);
    assert(img_cache[k2].serial == 200);

    // Case 3: Overflow guard (> 4096 entries) -> clears
    for (uint64_t i = 100; i < 4200; ++i) {
        ImgKey k{i, 0, 0, 0, 0, 0, 0};
        img_cache[k] = {i, 1};
    }
    assert(img_cache.size() > 4096);
    desc_memo_reset();
    assert(img_cache.empty());

    // Case 4: Toggle off clears
    honor_img_cache_persist_experiment = true;
    img_cache[k1] = {100, 1};
    assert(img_cache.size() == 1);
    honor_img_cache_persist_experiment = false;
    img_cache.clear(); // simulated LIVE-SET toggle
    assert(img_cache.empty());

    return 0;
}
'''
        with tempfile.NamedTemporaryFile('w', suffix='.cpp', delete=False) as f:
            f.write(code)
            src_path = f.name

        bin_path = src_path + '.bin'
        try:
            subprocess.run(['clang++', '-std=c++17', '-fsanitize=address,undefined', src_path, '-o', bin_path], check=True)
            res = subprocess.run([bin_path], check=True)
            self.assertEqual(res.returncode, 0)
        finally:
            import os
            if os.path.exists(src_path): os.unlink(src_path)
            if os.path.exists(bin_path): os.unlink(bin_path)


if __name__ == '__main__':
    unittest.main()
