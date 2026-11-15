"""Execute the version-checked production render policy and GPU time converter."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class RenderPolicy(unittest.TestCase):
    def test_shader_reuse_profile_default_and_explicit_overrides(self):
        source = (ROOT / 'src/graphics/nvn_vulkan/engine/xbg_game.cpp').read_text()
        start = source.index('     {shader_reuse=0;')
        configure = source[start:source.index('\n     scene::init(base);', start)]
        fixture = r'''
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
int shader_reuse = -1;
bool honor_inline_enabled = false;
namespace warm { bool keep_rewritten = false; }
void xbg_logf(const char *, ...) {}
void configure(const std::string &base) {
''' + configure + r'''
}
int main(int argc, char **argv) {
    assert(argc == 2);
    const std::string base = argv[1];
    const std::string marker = base + "/gfx-shader-reuse";
    assert(unsetenv("DM_GFX_SHADER_REUSE") == 0);
    for (bool profile : {false, true}) {
        honor_inline_enabled = profile;
        configure(base);
#ifdef XBG_HAVE_SPIRV_OPT
        assert(shader_reuse == int(profile));
#else
        assert(shader_reuse == 0);
#endif
        assert(warm::keep_rewritten == (shader_reuse != 0));
        for (int mode : {0, 1, 2}) {
            assert(setenv("DM_GFX_SHADER_REUSE", std::to_string(mode).c_str(), 1) == 0);
            configure(base);
            assert(shader_reuse == mode && warm::keep_rewritten == (mode != 0));
            for (int explicit_mode : {0, 1, 2}) {
                FILE *file = fopen(marker.c_str(), "wb");
                assert(file);
                assert(fputc('0' + explicit_mode, file) != EOF);
                assert(fclose(file) == 0);
                configure(base);
                assert(shader_reuse == explicit_mode);
                assert(warm::keep_rewritten == (explicit_mode != 0));
                assert(remove(marker.c_str()) == 0);
            }
        }
        assert(unsetenv("DM_GFX_SHADER_REUSE") == 0);
    }
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / 'test.cpp').write_text(fixture)
            for defines in ([], ['-DXBG_HAVE_SPIRV_OPT']):
                subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                                '-fsanitize=address,undefined', *defines,
                                str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
                subprocess.run([str(path / 'test'), str(path)], check=True)

    def test_descriptor_buffer_retirement_invalidates_referencing_slots(self):
        source = (ROOT / 'src/graphics/nvn_vulkan/engine/xbg_game.cpp').read_text()
        start = source.index('std::unordered_map<VkBuffer,uint32_t> memo_buffer_slots;')
        registry = source[start:source.index('constexpr VkBufferUsageFlags arena_usage=', start)]
        start = source.index('void memo_reference_buffer(')
        retirement = source[start:source.index('bool ab_enabled()', start)]
        start = source.index('void memo_check_epoch(){')
        check = source[start:source.index('// End of a submit', start)]
        self.assertIn('if(c.buffer)memo_forget_buffer(c.buffer);', source)
        self.assertIn('if(desc_memo_recording)memo_reference_buffer(buffer->buffer,slot_index);', source)
        self.assertIn('memo_reference_buffer(res->buffer.buffer,slot_index);', source)
        self.assertIn('memo_buffer_slots.clear();memo_buffer_invalid_slots=0;', source)
        start = source.index('void destroy_slots(){')
        shutdown = source[start:source.index('} // namespace', start)]
        self.assertIn('for(unsigned i=0;i<kMaxSlots;i++){\n        memo_unpin_all(i);', shutdown)
        self.assertLess(shutdown.index('memo_unpin_all(i)'), shutdown.index('texel_evict(i,true)'))
        self.assertLess(shutdown.index('memo_unpin_all(i)'), shutdown.index('vkDestroyDescriptorPool'))
        fixture = r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>
using VkBuffer = uintptr_t;
std::recursive_mutex mutex;
std::atomic<uint64_t> memo_epoch{1};
bool control = false;
bool ab_off(const char *name) { assert(!std::strcmp(name,"memobuffer")); return control; }
''' + registry + retirement + r'''
unsigned slot_index = 0;
uint64_t memo_slot_epoch[16];
std::vector<int> memo_pinned[16], desc_memo_slot[16];
uint64_t xmemo_drops = 0, memo_mod_gen = 1;
unsigned unpins[16]{};
void memo_unpin_all(unsigned si) {
    ++unpins[si]; ++memo_mod_gen;
    memo_pinned[si].clear(); desc_memo_slot[si].clear();
}
''' + check + r'''
int main() {
    for (unsigned i = 0; i < 16; ++i) {
        memo_slot_epoch[i] = 1;
        desc_memo_slot[i].push_back(1);
    }
    memo_forget_buffer(999); // Vertex/index-only allocation never entered a cached set.
    for (slot_index = 0; slot_index < 16; ++slot_index) memo_check_epoch();
    assert(memo_epoch == 1 && xmemo_drops == 0 && memo_buffer_invalid_slots == 0);
    // A shared snapshot can be referenced from distant slots, including slot 15.
    memo_reference_buffer(100,0); memo_reference_buffer(100,15); memo_reference_buffer(100,15);
    memo_reference_buffer(200,7);
    memo_forget_buffer(100);
    assert(memo_buffer_invalid_slots == ((1u << 15) | 1));
    assert(!memo_buffer_slots.count(100) && memo_buffer_slots.at(200) == (1u << 7));
    slot_index = 0; memo_check_epoch();
    assert(unpins[0] == 1 && memo_mod_gen == 2 && xmemo_drops == 1);
    assert(memo_buffer_invalid_slots == (1u << 15));
    // Driver handle reuse preserves the old invalidation and records new dependencies.
    memo_reference_buffer(100,3);
    slot_index = 3; memo_check_epoch(); assert(unpins[3] == 0);
    slot_index = 15; memo_check_epoch(); assert(unpins[15] == 1);
    memo_forget_buffer(100);
    slot_index = 3; memo_check_epoch(); assert(unpins[3] == 1);
    memo_forget_buffer(200);
    slot_index = 7; memo_check_epoch(); assert(unpins[7] == 1);
    assert(memo_buffer_slots.empty() && memo_buffer_invalid_slots == 0);
    assert(memo_buffer_slot_drops == 4 && memo_buffers_destroyed == 4 && memo_buffers_referenced == 3);
    // Sampler/global changes still invalidate every slot, including previously empty caches.
    ++memo_epoch;
    for (slot_index = 0; slot_index < 16; ++slot_index) {
        unsigned before = unpins[slot_index]; memo_check_epoch();
        assert(unpins[slot_index] == before + 1 && memo_slot_epoch[slot_index] == memo_epoch);
    }
    // The live A/B control remains identical to the former all-buffer global epoch.
    control = true;
    memo_reference_buffer(300,4); memo_forget_buffer(300);
    memo_forget_buffer(400);
    assert(memo_epoch == 4);
    control = false;
    for (slot_index = 0; slot_index < 16; ++slot_index) memo_check_epoch();
    assert(memo_buffer_invalid_slots == 0);
    slot_index = 12; memo_pinned[12].resize(8193);
    unsigned before = unpins[12]; memo_check_epoch();
    assert(unpins[12] == before + 1 && memo_pinned[12].empty());
    for (unsigned i = 0; i < 1000; ++i) {
        memo_reference_buffer(500,i % 16); memo_forget_buffer(500);
        slot_index = i % 16; memo_check_epoch();
    }
    assert(memo_buffer_slots.empty() && memo_buffer_invalid_slots == 0);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / 'test.cpp').write_text(fixture)
            subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
            subprocess.run([str(path / 'test')], check=True)

    def test_integer_format_lookup_uses_live_registered_records(self):
        source = (ROOT / 'src/graphics/nvn_vulkan/engine/xbg_game.cpp').read_text()
        start = source.index('struct TextureRecordFront {')
        registry = source[start:source.index('std::map<uint32_t,RegisteredTexture> null_textures;', start)]
        start = source.index('VkFormat integer_buffer_binding_format(')
        selection = source[start:source.index('Shader *integer_buffer_variant(', start)]
        fixture = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
using VkFormat = uint32_t;
constexpr VkFormat VK_FORMAT_UNDEFINED = 0;
constexpr unsigned VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER = 4;
constexpr unsigned XBG_SOURCE_TEXTURE = 1, XBG_SOURCE_IMAGE = 2;
struct Texture {
    struct { int target = 10; } d;
    uintptr_t mem = 1;
    struct { uint64_t size = 16; } L;
};
struct RegisteredTexture { Texture texture; VkFormat format = 2; };
struct Pool { void *pool = reinterpret_cast<void*>(1); int n = 4096; int key = 1; };
struct {
    Pool texture_pool;
    uint64_t tex[6][256]{};
    uint64_t img[6][16]{};
} state;
struct Shader {
    unsigned stage = 0;
    mutable const std::map<int,RegisteredTexture> *integer_texture_table = nullptr;
    mutable int integer_texture_id = 0;
    mutable const RegisteredTexture *integer_texture_record = nullptr;
};
struct xbg_shader_binding {
    unsigned type = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    unsigned count = 2, source = XBG_SOURCE_TEXTURE, slot = 0;
};
unsigned nvn_stage[6] = {0, 1, 2, 3, 4, 5};
std::map<int,std::map<int,RegisteredTexture>> textures;
std::map<int,RegisteredTexture> legacy_textures;
uint64_t integer_texture_lookup_hits = 0, integer_texture_lookup_misses = 0;
bool legacy_lookup_enabled = false;
bool registry_lookup_disabled = false;
bool ab_off(const char *name) {
    return std::strcmp(name, "iblookup") == 0 ? legacy_lookup_enabled : registry_lookup_disabled;
}
int pool_key(const Pool *pool) { return pool->key; }
bool integer_buffer_format(VkFormat format) { return format == 2 || format == 3; }
VkFormat texel_view_format(const RegisteredTexture &record) { return record.format; }
VkFormat texel_binding_format(const RegisteredTexture &record, const xbg_shader_binding &, const void *) {
    return record.format;
}
''' + registry + selection + r'''
int main() {
    Shader shader;
    xbg_shader_binding binding;
    auto &first = textures[1];
    first[7] = {}; first[8] = {};
    state.tex[0][0] = 7; state.tex[0][1] = 8;
    assert(integer_buffer_binding_format(shader, binding) == 2);
    uint64_t hits = texture_record_hits;
    // The per-shader single record alternates on every array element.
    // Both elements must now reuse the existing multi-entry registry cache.
    assert(integer_buffer_binding_format(shader, binding) == 2);
    assert(texture_record_hits == hits + 2);
    first[7].format = 3;
    assert(integer_buffer_binding_format(shader, binding) == VK_FORMAT_UNDEFINED);
    first[8].format = 3;
    assert(integer_buffer_binding_format(shader, binding) == 3);
    legacy_lookup_enabled = true;
    hits = texture_record_hits;
    assert(integer_buffer_binding_format(shader, binding) == 3);
    assert(texture_record_hits == hits);
    legacy_lookup_enabled = false;
    registry_lookup_disabled = true;
    assert(integer_buffer_binding_format(shader, binding) == 3);
    assert(texture_record_hits == hits);
    binding.count = 1;
    assert(integer_buffer_binding_format(shader, binding) == 3);
    uint64_t shader_hits = integer_texture_lookup_hits;
    assert(integer_buffer_binding_format(shader, binding) == 3);
    assert(integer_texture_lookup_hits == shader_hits + 1);
    registry_lookup_disabled = false;
    binding.count = 2;
    textures[2][7] = {}; textures[2][8] = {};
    state.texture_pool.key = 2;
    assert(integer_buffer_binding_format(shader, binding) == 2);
    state.texture_pool.key = 1;
    assert(integer_buffer_binding_format(shader, binding) == 3);
    state.tex[0][1] = 9;
    assert(integer_buffer_binding_format(shader, binding) == VK_FORMAT_UNDEFINED);
    first[9].format = 3;
    assert(integer_buffer_binding_format(shader, binding) == 3);
    first[9].texture.mem = 0;
    assert(integer_buffer_binding_format(shader, binding) == VK_FORMAT_UNDEFINED);
    first[9].texture.mem = 1;
    state.texture_pool.n = 9;
    assert(integer_buffer_binding_format(shader, binding) == VK_FORMAT_UNDEFINED);
    state.texture_pool.n = 4096;
    binding.count = 1;
    binding.source = XBG_SOURCE_IMAGE;
    state.img[0][0] = 7;
    assert(integer_buffer_binding_format(shader, binding) == 3);
    binding.slot = 16;
    assert(integer_buffer_binding_format(shader, binding) == VK_FORMAT_UNDEFINED);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / 'test.cpp').write_text(fixture)
            subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', str(path / 'test.cpp'),
                            '-o', str(path / 'test')], check=True)
            subprocess.run([str(path / 'test')], check=True)

    def test_texture_record_front_lifetime(self):
        source = (ROOT / 'src/graphics/nvn_vulkan/engine/xbg_game.cpp').read_text()
        start = source.index('struct TextureRecordFront {')
        body = source[start:source.index('std::map<uint32_t,RegisteredTexture> null_textures;', start)]
        fixture = r'''
#include <cassert>
#include <cstdint>
#include <map>
struct RegisteredTexture { int revision = 0; };
''' + body + r'''
int main() {
    std::map<int,RegisteredTexture> first, second;
    assert(find_texture_record(first,7) == nullptr);
    first[7].revision = 1;
    const RegisteredTexture *record = find_texture_record(first,7);
    assert(record == &first.at(7));
    uint64_t hits = texture_record_hits;
    assert(find_texture_record(first,7) == record && texture_record_hits == hits + 1);
    first[7] = {2};
    assert(find_texture_record(first,7)->revision == 2);
    second[7].revision = 3;
    assert(find_texture_record(second,7) == &second.at(7));
    assert(find_texture_record(first,7) == record);
    // IDs separated by the bucket count collide, and must still use the map.
    first[7+2048].revision = 4;
    assert(find_texture_record(first,7+2048) == &first.at(7+2048));
    assert(find_texture_record(first,7) == record);
    texture_record_cache_enabled = false;
    hits = texture_record_hits;
    assert(find_texture_record(first,7) == record && texture_record_hits == hits);
    first[7].revision = 5;
    texture_record_cache_enabled = true;
    assert(find_texture_record(first,7)->revision == 5);
    first.clear();
    clear_texture_record_cache();
    assert(find_texture_record(first,7) == nullptr);
    first[7].revision = 6;
    assert(find_texture_record(first,7) == &first.at(7));
    assert(find_texture_record(first,7)->revision == 6);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / 'test.cpp').write_text(fixture)
            subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', str(path / 'test.cpp'),
                            '-o', str(path / 'test')], check=True)
            subprocess.run([str(path / 'test')], check=True)
        self.assertIn('textures.clear();clear_texture_record_cache();', source)
        self.assertIn('record=find_texture_record(*texture_pool_cache,id);if(!record)xbg_fatalf(', source)
        self.assertIn('{"texrecord",&texture_record_cache_enabled}', source)

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
