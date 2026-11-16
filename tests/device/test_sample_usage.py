"""Check SPIR-V resource uses and the production sampler cache entry point."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
ENGINE = ROOT / 'src/graphics/nvn_vulkan/engine'


class SampleUsage(unittest.TestCase):
    def test_image_cache_separates_instruction_uses(self):
        source = (ENGINE / 'xbg_game.cpp').read_text()
        start = source.index('struct ImgKey{')
        key_type = source[start:source.index('struct ImgEntry', start)]
        start = source.index('                        ikey={handle,sh,')
        assignment = source[start:source.index(';img_cacheable=true;', start)+1]
        fixture = r'''
#include <cstdint>
#include <cstddef>
#include <unordered_map>
#include <set>
''' + key_type + r'''
struct Pool { void *mem=nullptr; } pool;
struct DescriptorPool { Pool *pool=nullptr; uint64_t off=0; };
struct { DescriptorPool texture_pool{&pool,0},sampler_pool; } state;
struct {uint32_t type=1,flags=0,binding=7;} b;
struct {std::set<uint32_t> filter_independent_bindings;} shader;
ImgKey key() {uint64_t handle=11,sh=17;ImgKey ikey{};
''' + assignment + r'''
return ikey;}
int main() {
    ImgKey filtered=key();shader.filter_independent_bindings.insert(7);
    ImgKey gather=key();if(filtered==gather)return 10;
    std::unordered_map<ImgKey,int,ImgKeyHash> cache;
    cache.emplace(filtered,1);cache.emplace(gather,2);
    return cache.size()==2 && cache.at(filtered)==1 && cache.at(gather)==2?0:11;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            for label, content, expected in [('fixed', fixture, 0), ('old', fixture.replace(
                    '|(uint64_t(shader.filter_independent_bindings.count(b.binding)!=0)<<16)', ''), 10)]:
                cpp = path / (label + '.cpp')
                cpp.write_text(content)
                binary = path / label
                subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                                '-fsanitize=address,undefined', str(cpp), '-o', str(binary)], check=True)
                self.assertEqual(subprocess.run([str(binary)]).returncode, expected)

    def test_resource_uses(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'usage'
            subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-I', str(ENGINE),
                            str(Path(__file__).with_name('sample_usage_contract.cpp')),
                            '-o', str(binary)], check=True)
            command = [str(binary)]
            module = os.environ.get('FFRS_REAL_SAMPLE_USAGE_MODULE')
            if module:
                subprocess.run(['spirv-val', '--target-env', 'vulkan1.1', module], check=True)
                command.append(module)
            subprocess.run(command, check=True)

    def test_production_sampler_cache(self):
        source = (ENGINE / 'xbg_game.cpp').read_text()
        start = source.index('struct SamplerFastKey{')
        cache = source[start:source.index('void clear_descriptor_caches()', start)]
        start = source.index('VkSampler sampler(const samp_par &requested,')
        body = source[start:source.index('VkSampler sampler_slow(const samp_par &p,', start)]
        prefix = r'''
#include <array>
#include <unordered_map>
#include <cstdint>
#include <cstring>
#include <cassert>
#include <stdexcept>
using VkSampler=unsigned;
using VkFormat=unsigned;
constexpr uint32_t VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT=0x1000;
struct samp_par {
    uint32_t minf=3,magf=1,ws=7,wt=5,wr=6,cmp_mode=0,cmp_func=2;
    float lod_min=0,lod_max=4,lod_bias=.5,aniso=4,border[4]{0,0,0,1};
};
struct Image {};
struct Format { uint32_t optimalTilingFeatures; };
Format format_properties(VkFormat f) { return {f==100?0x8001ed87u:0x8001fd83u}; }
samp_par received;
unsigned creates=0;
VkSampler sampler_slow(const samp_par &p,Image &,VkFormat viewed,bool) {
    if(viewed==100 && ((p.minf&1) || p.magf || p.minf>=4 || p.aniso>1))
        throw std::runtime_error("unsupported filtering");
    received=p;return ++creates;
}
'''
        suffix = r'''
int main() {
    try {
        Image image;samp_par requested;
        VkSampler safe=sampler(requested,image,100,false,true);
        assert(received.minf==2 && received.magf==0 && received.aniso==1);
        assert(received.ws==7 && received.wt==5 && received.wr==6);
        assert(received.cmp_func==2 && received.border[3]==1);
        assert(received.lod_min==0 && received.lod_max==4 && received.lod_bias==.5);
        assert(sampler(requested,image,100,false,true)==safe && creates==1);
        bool refused=false;
        try {sampler(requested,image,100,false,false);}catch(const std::runtime_error &){refused=true;}
        assert(refused); // a fetch-only cache hit cannot leak into a filtered use
        sampler(requested,image,103,false,true);assert(received.minf==3 && received.magf==1 && received.aniso==4);
        for(unsigned min=0;min<6;++min) {
            sampler_fast.clear();
            requested.minf=min;sampler(requested,image,100,false,true);
            assert(received.minf==(min<2?0:2));
        }
    } catch(const std::runtime_error &) {return 9;}
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            for label, text, expected in [('fixed', body, 0), ('old', body.replace(
                    'filter_independent && p.minf<=5 && p.magf<=1 && !(format_properties(viewed)',
                    'filter_independent && false && p.minf<=5 && p.magf<=1 && !(format_properties(viewed)'), 9)]:
                cpp = path / (label + '.cpp')
                cpp.write_text(prefix + cache + text + suffix)
                binary = path / label
                subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                                '-fsanitize=address,undefined', str(cpp), '-o', str(binary)], check=True)
                self.assertEqual(subprocess.run([str(binary)]).returncode, expected)


if __name__ == '__main__':
    unittest.main()
