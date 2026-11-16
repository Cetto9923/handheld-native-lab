"""Check static eligibility without memoizing any runtime view format."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class TypedStorageStatic(unittest.TestCase):
    def test_static_skip_and_dynamic_format_fallbacks(self):
        source = (ROOT / 'src/graphics/nvn_vulkan/engine/xbg_game.cpp').read_text()
        field = next(line for line in source.splitlines()
                     if 'int8_t typed_storage_dynamic=' in line)
        helper = source[source.index('bool typed_storage_static_skip('):
                        source.index('Shader *typed_storage_variant(')]
        start = source.index('Shader *typed_storage_variant(')
        prefix = source[start:source.index('    TypedStorageFormats formats;', start)]
        fixture = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <vector>
enum { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER=5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE=3,
       VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER=6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER=7,
       XBG_IMAGE_FORMAT_UNKNOWN=0 };
struct xbg_shader_binding { uint32_t type,count,flags,binding; };
struct Shader {
    std::vector<xbg_shader_binding> bindings;
    std::map<uint32_t,uint32_t> storage_image_formats;
    std::vector<uint32_t> module_words;
FIELD
};
bool honor_inline_enabled=true,old_path=false;
uint64_t typed_storage_static_skips=0,legacy_calls=0;
unsigned bound_format=0;
Shader variants[2];
bool ab_off(const char *name) { assert(!strcmp(name,"typedstatic"));return old_path; }
bool legacy_eligible(const Shader &base) {
    for(const auto &binding:base.bindings){
        if(binding.type!=VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER &&
           binding.type!=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)continue;
        auto declared=base.storage_image_formats.find(binding.binding);
        uint32_t format=declared==base.storage_image_formats.end()?0:declared->second;
        if(binding.count!=1)continue;
        if(format!=0)continue;
        if(!(binding.flags&1))continue;
        return true;
    }
    return false;
}
'''.replace('FIELD', field)
        fixture += helper + prefix + r'''
    ++legacy_calls;
    // The existing format-dependent body remains in production. This counted
    // stand-in makes accidental caching of a selected view observable here.
    if(legacy_eligible(base) && bound_format)return &variants[bound_format-1];
    return &base;
}
int main() {
    for(uint32_t type: {3u,5u,6u,7u,0u})
    for(uint32_t count: {0u,1u,2u,128u})
    for(uint32_t flags: {0u,1u,2u,3u})
    for(int declared: {-1,0,1,65535}){
        Shader shader;
        shader.module_words={1};shader.bindings.push_back({type,count,flags,9});
        if(declared>=0)shader.storage_image_formats[9]=uint32_t(declared);
        bool dynamic=legacy_eligible(shader);
        legacy_calls=typed_storage_static_skips=0;bound_format=0;
        assert(typed_storage_variant(shader)==&shader);
        assert(legacy_calls==1 && typed_storage_static_skips==0);
        for(unsigned format: {1u,2u,0u}){
            bound_format=format;
            Shader *expected=dynamic && format?&variants[format-1]:&shader;
            assert(typed_storage_variant(shader)==expected);
        }
        assert(legacy_calls==(dynamic?4u:1u));
        assert(typed_storage_static_skips==(dynamic?0u:3u));
        // The old A/B arm and other profiles always execute the original body.
        for(unsigned mode: {0u,1u}){
            honor_inline_enabled=mode!=0;old_path=mode==1;
            uint64_t before=legacy_calls;
            assert(typed_storage_variant(shader)==&shader);
            assert(legacy_calls==before+1);
        }
        honor_inline_enabled=true;old_path=false;
    }
    Shader late;
    legacy_calls=0;
    assert(typed_storage_variant(late)==&late && legacy_calls==0);
    assert(late.typed_storage_dynamic==-1);
    late.module_words={1};
    assert(typed_storage_variant(late)==&late && legacy_calls==1);
    assert(typed_storage_variant(late)==&late && legacy_calls==1);
    // An eligible binding at the end of a large table still takes the dynamic
    // body. A distinct typed variant starts with its own unexamined facts.
    Shader mixed;
    mixed.module_words={1};mixed.bindings.resize(256,{6,1,0,0});
    mixed.bindings.back()={5,1,1,255};bound_format=1;
    assert(typed_storage_variant(mixed)==&variants[0]);
    bound_format=2;assert(typed_storage_variant(mixed)==&variants[1]);
    Shader typed;
    typed.module_words=mixed.module_words;typed.bindings=mixed.bindings;
    typed.storage_image_formats[255]=1;bound_format=0;legacy_calls=0;
    assert(typed.typed_storage_dynamic==-1);
    assert(typed_storage_variant(typed)==&typed && legacy_calls==1);
    assert(typed_storage_variant(typed)==&typed && legacy_calls==1);
    bound_format=1;assert(typed_storage_variant(mixed)==&variants[0]);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / 'test.cpp').write_text(fixture)
            subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', str(path / 'test.cpp'),
                            '-o', str(path / 'test')], check=True)
            subprocess.run([str(path / 'test')], check=True)


if __name__ == '__main__':
    unittest.main()
