"""Exercise the production CPU mirror and the existing live CPU marker reader."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
GAME = Path(os.environ.get('FFRS_UPLOAD_GAME_SOURCE', ROOT / 'src/graphics/nvn_vulkan/engine/xbg_game.cpp'))


class UploadCpuPreserve(unittest.TestCase):
    def test_mirror_copy(self):
        source = GAME.read_text()
        start = source.index('bool guest_texture_write(')
        body = source[start:source.index('\nvoid copy_texture_buffer(', start)]
        fixture = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>
#define _Static_assert static_assert
extern "C" {
#include "src/graphics/nvn_vulkan/nvn/nvn_obj.h"
#include "src/graphics/nvn_vulkan/tex/nvn_deswizzle.h"
}
''' + body + r'''

static void success_case(int format, int target, unsigned width, unsigned height,
                         unsigned depth, unsigned levels, unsigned layer,
                         const NVNcopyRegion &region, unsigned level) {
    tex_t texture{};
    texture.d.target=target; texture.d.format=format;
    texture.d.width=width; texture.d.height=height;
    texture.d.depth=depth; texture.d.levels=levels;
    nvn_tex_layout(&texture.d, &texture.L);
    std::vector<uint8_t> storage(texture.L.size), expected(texture.L.size);
    for (size_t i=0; i<storage.size(); ++i) storage[i]=uint8_t(i*17+31);
    expected=storage;
    pool_t pool{}; pool.mem=storage.data(); pool.size=storage.size();
    texture.mem=storage.data(); texture.pool=&pool;
    int bw=0, bh=0;
    unsigned bpb=unsigned(nvn_fmt_bpb(format, &bw, &bh));
    size_t columns=(unsigned(region.width)+bw-1)/bw;
    size_t rows=(unsigned(region.height)+bh-1)/bh;
    size_t row=columns*bpb+16, image=row*rows;
    std::vector<uint8_t> input(image*region.depth);
    for (size_t i=0; i<input.size(); ++i) input[i]=uint8_t(i*19+7);

    // Build the reference with the independent complete-level swizzle API.
    bool volume=target==2;
    unsigned count=volume?1:unsigned(region.depth);
    for (unsigned k=0; k<count; ++k) {
        unsigned selected=volume?layer:layer+unsigned(region.zoffset)+k;
        uint32_t row_bytes, block_rows, slices, bytes_per_block;
        uint64_t base; unsigned th, td;
        assert(!nvn_deswizzle_geom(&texture.d, level, selected, &row_bytes,
            &block_rows, &slices, &base, &th, &td, &bytes_per_block));
        std::vector<uint8_t> linear(size_t(row_bytes)*block_rows*slices);
        assert(!nvn_deswizzle(&texture.d, level, selected, expected.data(), linear.data(), row_bytes));
        unsigned slice_count=volume?unsigned(region.depth):1;
        for (unsigned z=0; z<slice_count; ++z) {
            unsigned destination_z=volume?unsigned(region.zoffset)+z:0;
            unsigned source_z=volume?z:k;
            for (size_t y=0; y<rows; ++y) {
                size_t offset=(size_t(destination_z)*block_rows+unsigned(region.yoffset)/bh+y)*row_bytes
                    +unsigned(region.xoffset)/bw*bpb;
                memcpy(linear.data()+offset, input.data()+source_z*image+y*row, columns*bpb);
            }
        }
        assert(!nvn_swizzle(&texture.d, level, selected, linear.data(), row_bytes, expected.data()));
    }
    assert(guest_texture_write(&texture, level, layer, region, input.data(), row, image));
    assert(storage==expected);
}

int main() {
    success_case(0x25,1,64,32,1,5,0,{0,0,0,64,32,1},0);
    success_case(0x25,1,64,32,1,5,0,{3,2,0,7,9,1},1);
    success_case(0x45,1,63,31,1,5,0,{4,4,0,59,27,1},0);
    success_case(0x48,1,63,31,1,5,0,{0,0,0,15,7,1},2);
    success_case(0x25,4,32,16,4,4,1,{2,3,1,9,7,2},0);
    success_case(0x45,4,31,15,4,3,0,{0,0,1,15,7,2},1);
    success_case(0x25,2,16,8,5,3,0,{1,1,1,9,5,3},0);
    success_case(0x48,2,16,16,5,3,0,{0,0,0,8,8,2},1);

    tex_t texture{};texture.d.target=1;texture.d.format=0x25;
    texture.d.width=64;texture.d.height=32;texture.d.depth=1;texture.d.levels=1;
    nvn_tex_layout(&texture.d, &texture.L);
    std::vector<uint8_t> storage(texture.L.size,0xa5), input(64*32*4,0x6b);
    pool_t pool{};pool.mem=storage.data();pool.size=storage.size();
    texture.mem=storage.data();texture.pool=&pool;
    texture.L.size=128;  // The old mirror writes a prefix before discovering this truncation.
    NVNcopyRegion region{0,0,0,64,32,1};
    assert(!guest_texture_write(&texture,0,0,region,input.data(),64*4,64*32*4));
    assert(std::all_of(storage.begin(),storage.end(),[](uint8_t value){return value==0xa5;}));
    texture.L.size=pool.size;pool.size=127;
    assert(!guest_texture_write(&texture,0,0,region,input.data(),64*4,64*32*4));
    assert(std::all_of(storage.begin(),storage.end(),[](uint8_t value){return value==0xa5;}));
}
'''
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp)
            objects = []
            for name in ('nvn/nvn_tex', 'tex/nvn_deswizzle'):
                obj = output / (Path(name).name + '.o')
                subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror',
                                '-fsanitize=address,undefined', '-c',
                                str(ROOT / 'src/graphics/nvn_vulkan' / (name + '.c')),
                                '-o', str(obj)], check=True)
                objects.append(str(obj))
            (output / 'mirror.cpp').write_text(fixture)
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-I', str(ROOT),
                            str(output / 'mirror.cpp'), *objects, '-o', str(output / 'mirror')], check=True)
            subprocess.run([str(output / 'mirror')], check=True)

    def test_preserve_order(self):
        source = GAME.read_text()
        start = source.index('void copy_texture_buffer(')
        body = source[start:source.index('\nvoid copy_texture(', start)]
        depth = body.index('if(im.aspects!=VK_IMAGE_ASPECT_COLOR_BIT)')
        self.assertNotIn('get_image(p.tex,true)', body[:depth])
        decoded = body[body.index('if(im.decoded){'):body.index('    size_t row,image,n;')]
        color = body[body.index('    size_t row,image,n;'):]
        for branch in (decoded, color):
            self.assertLess(branch.index('guest_texture_write('), branch.index('get_image(p.tex,true)'))
            self.assertLess(branch.index('get_image(p.tex,true)'), branch.index('stream_buffer('))
        self.assertIn('if(!to_buffer && !full_destination)get_image(p.tex,true);', body[depth:])
        self.assertIn('gpu_sync(perf.sync_readback)', body)
        self.assertIn('VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT', body)

    def test_live_marker(self):
        source = GAME.read_text()
        start = source.index('    {static int cpu_prof_state=')
        block = source[start:source.index('\n    if(deferred_live_profile', start)]
        fixture = r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unistd.h>
struct { std::atomic<uint64_t> presents{0}; } stats;
struct { std::string base; } probe;
struct { void *trace=nullptr; } capture;
static void *provenance_file=nullptr;
static bool live_profile_used=false,timing_active=false;
static uint64_t timing_first=0;
static uint32_t deferred_live_profile=0;
struct XbgDescriptorTiming { unsigned calls=0; };
static XbgDescriptorTiming descriptor_timing;
static bool descriptor_detail=false;
static void poll() {
''' + block + r'''
}
static void marker(const char *text) {
    FILE *file=fopen((probe.base+"/gfx-cpu-profile").c_str(),"wb");assert(file);
    fputs(text,file);fclose(file);
}
int main(int argc,char **argv) {
    assert(argc==3);probe.base=argv[1];std::string mode=argv[2];
    if(mode=="late") {
        poll();marker("0 3\n");stats.presents=29;poll();assert(!deferred_live_profile);
        stats.presents=30;poll();assert(deferred_live_profile==3 && live_profile_used);
        assert(access((probe.base+"/gfx-cpu-profile").c_str(),F_OK));
        assert(!descriptor_detail);
        deferred_live_profile=0;marker("0 8\n");stats.presents=60;poll();assert(!deferred_live_profile);
    } else if(mode=="bounds") {
        marker("0 61\n");poll();assert(!deferred_live_profile);
        marker("18446744073709551615 3\n");stats.presents=30;poll();assert(!deferred_live_profile);
        marker("0 16\n");stats.presents=60;poll();assert(deferred_live_profile==16);
    } else if(mode=="descriptor") {
        descriptor_timing.calls=9;
        marker("0 3 1\n");poll();
        assert(deferred_live_profile==3 && descriptor_detail && descriptor_timing.calls==0);
    } else if(mode=="detail-bounds") {
        marker("0 3 2\n");poll();assert(!deferred_live_profile && !descriptor_detail);
    } else if(mode=="overflow") {
        marker("0 3\n");stats.presents=UINT64_MAX-1;poll();
        assert(!deferred_live_profile && !live_profile_used);
        stats.presents=UINT64_MAX;poll();assert(!deferred_live_profile && !live_profile_used);
    } else {
        marker("100 8\n");capture.trace=reinterpret_cast<void*>(1);poll();
        stats.presents=100;poll();assert(!deferred_live_profile);
        capture.trace=nullptr;provenance_file=reinterpret_cast<void*>(1);poll();assert(!deferred_live_profile);
        provenance_file=nullptr;timing_active=true;poll();assert(!deferred_live_profile);
        timing_active=false;timing_first=7;poll();assert(!deferred_live_profile);
        timing_first=0;poll();assert(deferred_live_profile==8 && live_profile_used);
    }
}
'''
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp)
            (output / 'marker.cpp').write_text(fixture)
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', str(output / 'marker.cpp'),
                            '-o', str(output / 'marker')], check=True)
            for mode in ('late', 'bounds', 'descriptor', 'detail-bounds', 'overflow', 'gates'):
                (output / mode).mkdir()
                subprocess.run([str(output / 'marker'), str(output / mode), mode], check=True)


if __name__ == '__main__':
    unittest.main()
