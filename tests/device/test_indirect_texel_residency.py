"""Execute the production indirect-dispatch input admission checks."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class IndirectTexelResidency(unittest.TestCase):
    def test_current_slot_inputs_and_alias_fallbacks(self):
        source = (ROOT / 'src/graphics/nvn_vulkan/engine/xbg_game.cpp').read_text()
        start = source.index('bool current_pending_range(uint64_t address,')
        # The earlier forward declaration has no body.
        start = source.index('bool current_pending_range(uint64_t address,', start + 1)
        body = source[start:source.index('// Optional, bounded real-data diagnostic.', start)]
        probe = (ROOT / 'src/graphics/nvn_vulkan/engine/xbg_probe.inc').read_text()
        start = probe.index('void probe_cpu_sync(')
        probe_body = probe[start:probe.index('std::shared_ptr<Image> probe_keep(', start)]
        fixture = r'''
#include "xbg_writeback_policy.h"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <map>
#include <set>
#include <vector>
using VkDeviceSize=uint64_t;
enum { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER=4,
       VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER=5,
       VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER=6,
       VK_DESCRIPTOR_TYPE_STORAGE_BUFFER=7 };
enum { XBG_SOURCE_SUPPORT=1,XBG_SOURCE_UNIFORM=2,XBG_SOURCE_STORAGE=3,
       XBG_SOURCE_C1=4,XBG_SOURCE_DRIVER0=5,XBG_SOURCE_DRIVER2=6,
       XBG_SOURCE_TEXTURE=7,XBG_SOURCE_IMAGE=8 };
struct Binding { unsigned binding,type,count,bytes,source,slot; };
struct Shader { std::string key="fixture"; std::vector<Binding> bindings; std::set<unsigned> storage_writes; } shader;
struct Range { uint64_t address=0,size=0; };
struct Pool { void *pool=nullptr; int n=32; };
struct State {
    Shader *shader[6]{};
    Range ub[6][16]{},sb[6][16]{};
    uint64_t tex[6][256]{},img[6][16]{};
    Pool texture_pool;
} state;
struct Slot { bool recording=false,pending=false; int fence=0; uint64_t covers=0,ops=0; } slots[16];
unsigned slot_index=0;
unsigned nvn_stage[6]={0,1,2,3,4,5};
struct Buffer { uint64_t offset=0,size=512; };
struct Writeback { uint64_t addr,bytes; Buffer buffer; const void *slot; };
std::vector<Writeback> pending_writebacks;
struct Report { uint64_t addr; };
struct NVNcounterData { uint64_t value,timestamp; };
std::vector<Report> pending_reports;
struct Texture {
    struct { unsigned target=10; } d;
    void *mem=nullptr;
    struct { uint64_t size=0; } L;
};
struct RegisteredTexture { Texture texture; };
std::map<int,RegisteredTexture> legacy_textures;
std::map<int,std::map<int,RegisteredTexture>> textures;
int pool_key(const Pool *) { return 1; }
struct { bool active=false,probe=false,trace=false; } capture;
void *provenance_file=nullptr;
bool profile=false,control=false,honor_inline_enabled=true,draw_detail=false;
bool flame_diag_enabled=false,fire=false,water=false;
struct { std::atomic<bool> on{false}; } ubtrace;
int g_ui_moff=0;
bool pass_prof_active() { return profile; }
bool fire_trace_active() { return fire; }
bool water_ready() { return water; }
bool ab_off(const char *name) { assert(std::string(name)=="indtexel");return control; }
struct { struct { uint64_t minStorageBufferOffsetAlignment=16;
                 uint64_t minUniformBufferOffsetAlignment=16; } limits; } properties;
size_t uniform_range(size_t n) { return std::max<size_t>(16,(n+15)&~size_t(15)); }
struct { bool waiting=true; } probe;
struct { std::atomic<uint64_t> presents{1}; } stats;
unsigned cur_op=27;bool xview_guard=true;
struct { void *device=nullptr; } cfg;
enum { VK_SUCCESS=0 };
int vkGetFenceStatus(void *,int) { return VK_SUCCESS; }
int marker='3';
int probe_marker_value() { return marker; }
std::vector<std::string> logs;
void xbg_logf(const char *format,...) {
    char text[2048];va_list args;va_start(args,format);
    vsnprintf(text,sizeof(text),format,args);va_end(args);logs.emplace_back(text);
}
''' + body + probe_body + r'''
void texture(int id,uint64_t address,uint64_t bytes) {
    auto &t=legacy_textures[id].texture;
    t.mem=reinterpret_cast<void *>(address);t.L.size=bytes;t.d.target=10;
}
void baseline() {
    state=State{};shader=Shader{};
    state.shader[5]=&shader;
    state.texture_pool={reinterpret_cast<void *>(1),32};
    state.ub[5][0]={0x1000,96};
    state.img[5][0]=1;state.tex[5][128]=2;
    slots[0].recording=true;slots[1].recording=false;slot_index=0;
    pending_writebacks.clear();pending_reports.clear();
    legacy_textures.clear();textures.clear();
    texture(1,0x2000,256);texture(2,0x3000,256);
    shader.bindings={{5,6,1,1392,1,0},{11,5,1,0,8,0},
                     {29,4,1,0,7,128},{53,6,1,65536,2,0}};
    control=false;honor_inline_enabled=true;
}
int main(int argc,char **) {
    baseline();assert(indirect_dispatch_inputs_resident());
    pending_writebacks.push_back({0x2000,256,{32},&slots[0]});
    pending_writebacks.push_back({0x3000,256,{64},&slots[0]});
    assert(indirect_dispatch_inputs_resident());
    pending_writebacks.back().slot=&slots[1];assert(!indirect_dispatch_inputs_resident());
    pending_writebacks.back().slot=&slots[0];
    pending_writebacks.push_back({0x3004,4,{0},&slots[0]});
    assert(!indirect_dispatch_inputs_resident()); // New partial write wins.
    pending_writebacks.pop_back();
    pending_writebacks.back().buffer.offset=2;assert(!indirect_dispatch_inputs_resident());
    pending_writebacks.back().buffer.offset=64;
    pending_writebacks.push_back({0x1000,96,{16},&slots[0]});
    assert(indirect_dispatch_inputs_resident());
    pending_writebacks.back().slot=&slots[1];assert(!indirect_dispatch_inputs_resident());
    baseline();pending_reports.push_back({0x2000});assert(!indirect_dispatch_inputs_resident());
    baseline();pending_reports.push_back({0x3000});assert(!indirect_dispatch_inputs_resident());
    baseline();control=true;assert(!indirect_dispatch_inputs_resident());
    control=false;honor_inline_enabled=false;assert(!indirect_dispatch_inputs_resident());
    // Existing UBO-only eligibility remains available on other devices.
    shader.bindings={{53,6,1,65536,2,0}};assert(indirect_dispatch_inputs_resident());
    baseline();state.texture_pool.pool=nullptr;assert(!indirect_dispatch_inputs_resident());
    baseline();state.img[5][0]=0;assert(!indirect_dispatch_inputs_resident());
    baseline();state.img[5][0]=32;assert(!indirect_dispatch_inputs_resident());
    baseline();legacy_textures.erase(1);assert(!indirect_dispatch_inputs_resident());
    baseline();legacy_textures[1].texture.d.target=2;assert(!indirect_dispatch_inputs_resident());
    baseline();legacy_textures[1].texture.L.size=0;assert(!indirect_dispatch_inputs_resident());
    baseline();texture(1,UINT64_MAX-3,8);assert(!indirect_dispatch_inputs_resident());
    baseline();texture(1,0x2002,256);assert(!indirect_dispatch_inputs_resident());
    baseline();shader.bindings[1].slot=16;assert(!indirect_dispatch_inputs_resident());
    baseline();shader.bindings[2].slot=256;assert(!indirect_dispatch_inputs_resident());
    baseline();shader.bindings[1].count=129;assert(!indirect_dispatch_inputs_resident());
    baseline();shader.bindings[1].type=3;assert(!indirect_dispatch_inputs_resident());
    baseline();shader.bindings.push_back({17,5,1,0,8,0});
    assert(indirect_dispatch_inputs_resident()); // Exact writable aliases share storage.
    state.img[5][1]=3;texture(3,0x2080,256);shader.bindings.back().slot=1;
    assert(!indirect_dispatch_inputs_resident());
    baseline();texture(2,0x2000,256);assert(!indirect_dispatch_inputs_resident());
    baseline();texture(1,0x1000,96);assert(!indirect_dispatch_inputs_resident());
    baseline();shader.bindings.push_back({71,7,1,0,3,0});
    state.sb[5][0]={0x1f00,512};assert(indirect_dispatch_inputs_resident());
    state.sb[5][0]={0x1f00,384};assert(!indirect_dispatch_inputs_resident());
    baseline();textures[1]=legacy_textures;legacy_textures.clear();
    assert(indirect_dispatch_inputs_resident()); // Select the bound pool table.
    for(bool *flag:{&draw_detail,&fire,&water,&flame_diag_enabled,&profile}) {
        *flag=true;assert(!indirect_dispatch_inputs_resident());*flag=false;
    }
    ubtrace.on=true;assert(!indirect_dispatch_inputs_resident());ubtrace.on=false;
    capture.active=true;assert(!indirect_dispatch_inputs_resident());capture.active=false;
    capture.probe=true;assert(!indirect_dispatch_inputs_resident());capture.probe=false;
    provenance_file=reinterpret_cast<void *>(1);assert(!indirect_dispatch_inputs_resident());
    provenance_file=nullptr;g_ui_moff=1;assert(!indirect_dispatch_inputs_resident());
    g_ui_moff=0;slots[0].recording=false;
    pending_writebacks.push_back({0x3000,256,{64},&slots[0]});
    assert(!indirect_dispatch_inputs_resident());

    baseline();pending_writebacks.push_back({0x4000,60,{0},&slots[0]});
    if(argc>1){
        shader.bindings[1].count=16;shader.bindings[2].count=32;
        for(auto &handle:state.img[5])handle=1;
        for(auto &handle:state.tex[5])handle=2;
    }
    probe_cpu_sync(0x4000,12,5,0x4000,60);
    auto texel_count=[] { return std::count_if(logs.begin(),logs.end(),[](const auto &line) {
        return line.find("CODEX_INDIRECT_TEXEL ")==0;
    }); };
    const int expected_texels=argc>1?32:2;
    assert(texel_count()==expected_texels);
    assert(std::any_of(logs.begin(),logs.end(),[](const auto &line) {
        return line.find("CODEX_INDIRECT_BUFFER binding=29 address=0x3000 bytes=256") == 0;
    }));
    probe_cpu_sync(0x4000,12,5,0x4000,60);assert(texel_count()==expected_texels);
    size_t previous_logs=logs.size();marker='0';
    probe_cpu_sync(0x4000,12,5,0x4000,60);assert(logs.size()==previous_logs);
    marker='3';capture.trace=true;
    probe_cpu_sync(0x4000,12,5,0x4000,60);assert(logs.size()==previous_logs);
    capture.trace=false;probe.waiting=false;
    probe_cpu_sync(0x4000,12,5,0x4000,60);assert(logs.size()==previous_logs);
}
'''
        fixture = '#include <string>\n' + fixture
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / 'test.cpp').write_text(fixture)
            subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-I',
                            str(ROOT / 'src/graphics/nvn_vulkan/engine'),
                            str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
            subprocess.run([str(path / 'test')], check=True)
            subprocess.run([str(path / 'test'), 'many'], check=True)


if __name__ == '__main__':
    unittest.main()
