"""Execute the version-checked production render policy and GPU time converter."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class RenderPolicy(unittest.TestCase):
    def test_descriptor_image_front_lifetime(self):
        source = (ROOT / 'src/graphics/nvn_vulkan/engine/xbg_game.cpp').read_text()
        start = source.index('struct ImgFront{')
        body = source[start:source.index('Image &get_image_slow(', start)]
        self.assertIn('Image *live=it==img_cache.end()?nullptr:find_image(it->second.tex->serial);', source)
        fixture = r'''
#include <cassert>
#include <cstdint>
#include <map>
#include <memory>
struct Image { int value; };
std::map<uint64_t,std::shared_ptr<Image>> images;
''' + body + r'''
int main() {
    assert(find_image(1) == nullptr);
    auto old = std::make_shared<Image>(); images.emplace(1,old);
    assert(find_image(1) == old.get());
    images.erase(1); ++images_epoch;
    assert(find_image(1) == nullptr);
    auto replacement = std::make_shared<Image>(); images.emplace(1,replacement);
    assert(find_image(1) == replacement.get());
    uint64_t other = 2;
    while (((other*0x9E3779B97F4A7C15ull)>>54) != ((1*0x9E3779B97F4A7C15ull)>>54)) ++other;
    auto collision = std::make_shared<Image>(); images.emplace(other,collision);
    assert(find_image(other) == collision.get());
    assert(find_image(1) == replacement.get());
    images.clear(); ++images_epoch;
    assert(find_image(1) == nullptr && find_image(other) == nullptr);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / 'test.cpp').write_text(fixture)
            subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
            subprocess.run([str(path / 'test')], check=True)

    def test_runtime_settings_validation(self):
        source = (ROOT / 'src/runtime/hle/guest_process.c').read_text()
        start = source.index('static uintptr_t render_policy_data(')
        body = source[start:source.index('int guest_process_current_tls_template(', start)]
        fixture = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <sys/mman.h>
typedef struct { void *module; } guest_process;
static guest_process process, *current_process = &process, *exclusive_process;
static unsigned char *base, *objects[4];
static int wrong_build;
static uintptr_t nso_base(void *module) { return (uintptr_t)module; }
static size_t nso_image_size(void *module) { (void)module; return 0x9c00000; }
static void nso_build_id(void *module, char out[33]) {
    (void)module;
    strcpy(out, wrong_build ? "00000000000000000000000000000000" : "736837afd390442ed7c74a7de730e71b");
}
static int guest_process_is_guest_memory(uintptr_t address) {
    for (unsigned i = 0; i < 4; ++i)
        if (address >= (uintptr_t)objects[i] && address - (uintptr_t)objects[i] < 88) return 1;
    return 0;
}
''' + body + r'''
int main(int argc, char **argv) {
    assert(argc == 2);
    int test = atoi(argv[1]);
    setenv("FFRS_RENDER_POLICY", test == 1 ? "0" : "1", 1);
    base = mmap(NULL, 0x9c00000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    assert(base != MAP_FAILED);
    process.module = base;
    const uintptr_t offsets[] = {0x8c28a90, 0x8be70d0, 0x8be70e8, 0x8be7118};
    for (unsigned i = 0; i < 4; ++i) {
        objects[i] = calloc(1, 88); assert(objects[i]);
        uintptr_t *cache = (uintptr_t *)(base + offsets[i]);
        cache[0] = (uintptr_t)base + (i ? 0x67dd958 : 0x67d9530);
        cache[1] = (uintptr_t)objects[i]; cache[2] = cache[1] + 80;
        *(uintptr_t *)objects[i] = (uintptr_t)base + (i ? 0x67dd6f8 : 0x67dd5c8);
        if (!i) { int values[2] = {2,2}; memcpy(objects[i]+80, values, 8); }
        else { float values[2] = {80,80}; memcpy(objects[i]+80, values, 8); }
    }
    if (test == 2) wrong_build = 1;
    if (test == 3) ((uintptr_t *)(base + offsets[1]))[2] += 8;
    if (test == 4) *(uintptr_t *)objects[2] = 0;
    if (test == 5) { float bad = NAN; memcpy(objects[3]+84, &bad, 4); }
    if (test == 6) ((uintptr_t *)(base + offsets[2]))[1] = UINTPTR_MAX - 7;
    if (test == 7) { float low[2] = {75,75}; memcpy(objects[2]+80, low, 8); }
    unsigned char before[4][88];
    for (unsigned i = 0; i < 4; ++i) memcpy(before[i], objects[i], 88);
    guest_process_update_render_policy();
    if (test > 0 && test < 7) {
        for (unsigned i = 0; i < 4; ++i) assert(!memcmp(before[i], objects[i], 88));
    } else {
        const float expected[] = {60,test == 7 ? 75.0f : 80.0f,1000.0f/30};
        for (unsigned i = 1; i < 4; ++i) {
            float values[2]; memcpy(values, objects[i]+80, 8);
            assert(values[0] == expected[i-1] && values[1] == expected[i-1]);
            assert(!memcmp(before[i], objects[i], 80));
        }
        for (unsigned k = 0; k < 61; ++k) guest_process_update_render_policy();
    }
    for (unsigned i = 0; i < 4; ++i) free(objects[i]);
    assert(!munmap(base, 0x9c00000));
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / 'test.c').write_text(fixture)
            subprocess.run(['clang', '-std=c11', '-D_DARWIN_C_SOURCE', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', str(path / 'test.c'), '-o', str(path / 'test')], check=True)
            for case in range(8):
                subprocess.run([str(path / 'test'), str(case)], check=True)

    def test_recorded_gpu_time(self):
        source = (ROOT / 'src/graphics/nvn_device/nvn_device.c').read_text()
        start = source.index('uint64_t nvn_device_get_timestamp_in_nanoseconds(')
        body = source[start:source.index('\nvoid nvn_device_apply_deferred_finalizes(', start)]
        fixture = r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
static struct { unsigned calls; } s_device;
''' + body + r'''
int main(void) {
    const uint64_t ticks[] = {0,1,11,12,13,19200000,UINT64_MAX};
    for (unsigned i = 0; i < sizeof(ticks)/sizeof(ticks[0]); ++i) {
        uint64_t data[2] = { UINT64_MAX, ticks[i] };
        uint64_t expected = (uint64_t)(((__uint128_t)ticks[i] * 625) / 12);
        assert(nvn_device_get_timestamp_in_nanoseconds(NULL,data) == expected);
        assert(nvn_device_get_timestamp_in_nanoseconds(NULL,data) == expected);
    }
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / 'test.c').write_text(fixture)
            subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                            str(path / 'test.c'), '-o', str(path / 'test')], check=True)
            subprocess.run([str(path / 'test')], check=True)


if __name__ == '__main__':
    unittest.main()
