"""Full production bbox-ready differential tests; host state shims, no GPU."""
from pathlib import Path
import argparse,subprocess,tempfile

def fn(s,name):
 a=s.index(name);return s[a:s.index('\n}',a)+2]
HARNESS='\n#include <cstdint>\n#include <cstring>\n#include <vector>\n#include <map>\n#include <algorithm>\n#include <cassert>\n#include <random>\nusing VkDeviceSize=uint64_t;\nstruct NVNcounterData {uint64_t words[4];};\nstruct Report {uint64_t addr;};struct Buffer {uint64_t offset;};struct Writeback {uint64_t addr,bytes;Buffer buffer;};\nstd::vector<Report> pending_reports;std::vector<Writeback> pending_writebacks;\n\nbool timing_active=false,read_preflight=false,draw_detail=false,prof=false,blocked=false,indexed_bbox_ready_enabled=false;\nvoid *provenance_file=nullptr;\nstruct {bool active=false,probe=false;}capture;\nbool pass_prof_active(){return prof;}bool ab_off(const char*){return blocked;}\nstruct p_draw_idx {uint64_t ibuf=1024;};\ntypedef struct { uint32_t fmt : 8, offset : 19, stream : 5; } va_st;\nstruct Range {uint64_t address=2048,size=64;};\nenum {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER=1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER};\nenum {XBG_SOURCE_UNIFORM=1,XBG_SOURCE_STORAGE,XBG_SOURCE_SUPPORT,XBG_SOURCE_C1,XBG_SOURCE_DRIVER0,XBG_SOURCE_DRIVER2,XBG_SOURCE_TEXTURE,XBG_SOURCE_IMAGE};\nstruct Binding {unsigned type=1,count=1,source=1,slot=0,bytes=16,binding=0;};\nstruct Shader {std::vector<unsigned>inputs,storage_writes;std::vector<Binding>bindings;};\nstruct Pool {void *pool=(void*)1;int n=256;};\nunsigned pool_key(Pool*){return 1;}\nstruct Texture {uint64_t mem=4096;struct {uint64_t size=64;}L;};\nstruct Record {Texture texture;};\nstd::map<int,Record>legacy_textures;std::map<unsigned,std::map<int,Record>>textures;\nstruct State {bool rast_discard=false,tf_on=false;Shader *shader[5]{};struct {bool depth=false;}rt;uint32_t attrib[16]{};Range vb[16],ub[5][16],sb[5][16];Pool texture_pool;uint64_t img[5][256]{},tex[5][256]{};}state;\nunsigned nvn_stage[5]={0,1,2,3,4};\nstruct {struct {uint64_t minStorageBufferOffsetAlignment=16,minUniformBufferOffsetAlignment=16,minTexelBufferOffsetAlignment=16;}limits;}properties;\nsize_t uniform_range(size_t copy){return std::max<size_t>(16,(copy+15)&~size_t(15));}\nXbgIndexedReadyStats indexed_ready_stats;\nXbgIndexedBBoxStats indexed_bbox_stats;\n'
TEST='\nvoid attr(unsigned loc,unsigned fmt,unsigned stream){va_st v{};v.fmt=fmt;v.stream=stream;memcpy(&state.attrib[loc],&v,4);}\nvoid compare(){\n p_draw_idx a;IndexedInputFailure x,y;bool old=original_graphics(a,64,&x);\n bool now=indexed_graphics_ready(a,64,&y);assert(old==now);assert(!strcmp(x.why,y.why) && x.stage==y.stage && x.binding==y.binding && x.address==y.address && x.bytes==y.bytes);\n\n assert(original_graphics(a,64,nullptr)==indexed_graphics_ready(a,64,nullptr));\n}\nint main(){\n std::mt19937 rng(4111);Shader vs,fs;\n for(unsigned k=0;k<20000;++k){\n  state=State{};vs=Shader{};fs=Shader{};state.shader[0]=&vs;state.shader[4]=&fs;\n  pending_reports.clear();pending_writebacks.clear();textures.clear();legacy_textures.clear();\n  read_preflight=prof=draw_detail=false;capture={};provenance_file=nullptr;\n  indexed_bbox_ready_enabled=k%3!=0;blocked=k%7==0;timing_active=k%2;\n  for(unsigned i=0,n=rng()%20;i<n;++i)vs.inputs.push_back(rng()%18);\n  for(unsigned i=0;i<16;++i){attr(i,rng()%3,rng()%20);state.vb[i]={uint64_t(rng()%4096),uint64_t(rng()%128)};}\n  if(k%3==0)pending_reports.push_back({rng()%4096});\n  if(k%2==0)pending_writebacks.push_back({rng()%4096,32,{rng()%64}});\n  if(k%11==0)state.shader[0]=nullptr;\n  if(k%13==0)state.rast_discard=true;\n  if(k%17==0)read_preflight=true;\n  if(k%19==0)vs.storage_writes.push_back(1);\n  if(k%5==0){Binding b;b.type=1+rng()%6;b.source=1+rng()%9;b.slot=rng()%18;b.count=1;b.binding=rng()%30;vs.bindings.push_back(b);fs.bindings.push_back(b);}\n  legacy_textures[1]={};for(unsigned i=0;i<256;++i)state.tex[0][i]=state.tex[4][i]=1;\n  compare();\n }\n // Rebuild after pin-like write and preserve newest partial-overlap precedence.\n state=State{};vs=Shader{};fs=Shader{};state.shader[0]=&vs;state.shader[4]=&fs;vs.inputs={0};attr(0,1,0);pending_reports.clear();pending_writebacks.clear();read_preflight=false;indexed_bbox_ready_enabled=true;blocked=false;compare();\n pending_writebacks.push_back({2048,64,{0}});compare();pending_writebacks.push_back({2048,32,{0}});compare();\n // Direct predicate: actual production overlap, 200k random queries.\n for(unsigned k=0;k<10000;++k){\n  pending_reports.clear();pending_writebacks.clear();\n  for(unsigned i=0,n=rng()%8;i<n;++i)pending_reports.push_back({k%19==0?UINT64_MAX-2:rng()%4096});\n  for(unsigned i=0,n=rng()%12;i<n;++i)pending_writebacks.push_back({k%23==0?UINT64_MAX-4:rng()%4096,rng()%256,{rng()%128}});\n  XbgIndexedBounds bounds;bounds.build(pending_reports,pending_writebacks,sizeof(NVNcounterData));\n  for(unsigned j=0;j<20;++j){uint64_t a=j%13==0?UINT64_MAX-2:rng()%8192,b=rng()%256,alignment=1ull<<(rng()%8);assert(original_range(a,b,alignment)==indexed_range_ready(a,b,alignment,nullptr,nullptr,&bounds));}\n }\n // Disjoint envelopes eliminate actual scans; changing diagnostic counters/envelopes\n // cannot influence production result, which uses the independent bounds object.\n pending_reports={{64}};pending_writebacks={{128,32,{0}}};XbgIndexedBounds bounds;bounds.build(pending_reports,pending_writebacks,32);XbgIndexedBBoxStats diag;XbgIndexedBBoxProbe probe(&diag);probe.build(pending_reports,pending_writebacks,32);\n probe.reports.low=0;probe.reports.high=UINT64_MAX;diag.queries=123;\n assert(indexed_range_ready(1024,64,4,nullptr,&probe,&bounds));assert(diag.report_scan_steps==0 && diag.writeback_scan_steps==0);\n // Overlap with newest partial write must fail even if older write fully covers.\n pending_reports.clear();pending_writebacks={{1024,64,{0}},{1024,32,{0}}};bounds.build(pending_reports,pending_writebacks,32);assert(!indexed_range_ready(1024,64,4,nullptr,nullptr,&bounds));\n // Invalid pending interval conservatively disables one bound, preserving old scan.\n pending_writebacks={{UINT64_MAX-2,4,{0}},{1024,64,{1}}};bounds.build(pending_reports,pending_writebacks,32);assert(!bounds.writebacks.usable);assert(!indexed_range_ready(1024,64,4,nullptr,nullptr,&bounds));\n pending_writebacks.clear();bounds.build(pending_reports,pending_writebacks,32);assert(!indexed_range_ready(0,1,4,nullptr,nullptr,&bounds));assert(!indexed_range_ready(UINT64_MAX,1,4,nullptr,nullptr,&bounds));\n}\n'
ORIGINAL_GRAPHICS='bool indexed_graphics_ready(const p_draw_idx &a,size_t index_bytes,IndexedInputFailure *failure=nullptr){\n    XbgIndexedReadyProbe probe(timing_active?&indexed_ready_stats:nullptr);\n    XbgIndexedBBoxProbe bbox(timing_active?&indexed_bbox_stats:nullptr);\n    auto range_ready=[&](uint64_t address,uint64_t bytes,VkDeviceSize alignment){return indexed_range_ready(address,bytes,alignment,probe.out?&probe:nullptr,bbox.out?&bbox:nullptr);};\n    auto reject=[&](const char *why,uint32_t st=UINT32_MAX,uint32_t binding=UINT32_MAX,uint64_t address=0,uint64_t bytes=0){if(failure)*failure={why,st,binding,address,bytes};return false;};\n    if(read_preflight || capture.active || capture.probe || provenance_file || pass_prof_active() || draw_detail ||\n       state.rast_discard || state.tf_on || !state.shader[0] || (!state.shader[4] && !state.rt.depth) || state.shader[1] || state.shader[2] || state.shader[3])return reject("state");\n    bbox.build(pending_reports,pending_writebacks,sizeof(NVNcounterData));\n    if(!range_ready(a.ibuf,index_bytes,4))return reject("index",0,0,a.ibuf,index_bytes);\n    for(auto loc:state.shader[0]->inputs){if(loc>=16)return reject("vertex-location");va_st v;memcpy(&v,&state.attrib[loc],4);if(!v.fmt)continue;if(v.stream>=16)return reject("vertex-stream");probe.vertex_stream(v.stream);auto r=state.vb[v.stream];if(!range_ready(r.address,r.size,4))return reject("vertex",0,v.stream,r.address,r.size);}\n    for(unsigned st=0;st<5;st++)if(state.shader[st]){auto &shader=*state.shader[st];unsigned ns=nvn_stage[st];if(!shader.storage_writes.empty())return reject("storage-write",st);\n        for(auto &b:shader.bindings)for(unsigned e=0;e<b.count;e++){\n            if(b.type==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || b.type==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER){\n                if(b.source==XBG_SOURCE_UNIFORM || b.source==XBG_SOURCE_STORAGE){unsigned slot=b.slot+e;if(slot>=16)return false;auto r=b.source==XBG_SOURCE_UNIFORM?state.ub[ns][slot]:state.sb[ns][slot];size_t bytes=b.type==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER?std::min<size_t>(r.size,b.bytes?b.bytes:65536):r.size;\n                    // Storage intervals can merge; range-ready intervals prove their range either host-clean or fully resident in GPU buffers.\n                    VkDeviceSize alignment = b.type==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER ? properties.limits.minStorageBufferOffsetAlignment : properties.limits.minUniformBufferOffsetAlignment;\n                    if(b.type==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)bytes=uniform_range(bytes);\n                    if(!range_ready(r.address,bytes,alignment))return reject("cpu-buffer",st,b.binding,r.address,bytes);\n                }else if(b.source!=XBG_SOURCE_SUPPORT && b.source!=XBG_SOURCE_C1 && b.source!=XBG_SOURCE_DRIVER0 && b.source!=XBG_SOURCE_DRIVER2)return false;\n            }else{\n                if(b.source!=XBG_SOURCE_TEXTURE && b.source!=XBG_SOURCE_IMAGE)return false;unsigned slot=b.slot+e;\n                if(slot>=(b.source==XBG_SOURCE_IMAGE?16u:256u) || !state.texture_pool.pool)return false;\n                uint64_t handle=b.source==XBG_SOURCE_IMAGE?state.img[ns][slot]:state.tex[ns][slot];int id=int(handle&0xfffff);\n                if(!id || (state.texture_pool.n>0 && id>=state.texture_pool.n))return reject("null-image",st,b.binding);\n                auto pool=textures.find(pool_key(&state.texture_pool));auto &table=pool==textures.end()?legacy_textures:pool->second;auto found=table.find(id);if(found==table.end())return false;auto &t=found->second.texture;\n                if(b.type==VK_DESCRIPTOR_TYPE_STORAGE_IMAGE || b.type==VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER)return reject("storage-image",st,b.binding);\n                if(b.type==VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER){if(!range_ready(uint64_t(t.mem),t.L.size,properties.limits.minTexelBufferOffsetAlignment))return reject("texel",st,b.binding,uint64_t(t.mem),t.L.size);}\n                else if(t.mem && t.L.size && !indirect_cpu_clean(uint64_t(t.mem),t.L.size))return reject("image-memory",st,b.binding,uint64_t(t.mem),t.L.size);\n            }\n        }\n    }\n    return true;\n}'
ORIGINAL_RANGE='bool indexed_range_ready(uint64_t address,uint64_t bytes,VkDeviceSize alignment,XbgIndexedReadyProbe *probe=nullptr,XbgIndexedBBoxProbe *bbox=nullptr){\n    if(probe)probe->request(address,bytes,alignment);\n    if(bbox)bbox->request(address,bytes);\n    if(!address || !bytes || address+bytes<address)return false;\n    for(auto &r:pending_reports){if(probe)probe->report_step();if(bbox)bbox->report_step();if(xbg_wb_overlaps(address,bytes,r.addr,sizeof(NVNcounterData)))return false;}\n    for(auto w=pending_writebacks.rbegin();w!=pending_writebacks.rend();++w){\n        if(probe)probe->writeback_step();if(bbox)bbox->writeback_step();\n        if(!xbg_wb_overlaps(address,bytes,w->addr,w->bytes))continue;\n        return address>=w->addr && address+bytes<=w->addr+w->bytes &&\n            (w->buffer.offset+address-w->addr)%alignment==0;\n    }\n    return true;\n}'


RECORD_CACHE_CASES = r"""
void record_cache_cases() {
    clear_texture_record_cache();
    pending_reports.clear();
    pending_writebacks.clear();
    textures.clear();
    legacy_textures.clear();
    state = State{};
    Shader vs, fs;
    state.shader[0] = &vs;
    state.shader[4] = &fs;
    read_preflight = prof = draw_detail = blocked = false;
    capture = {};
    provenance_file = nullptr;
    Binding binding;
    binding.type = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    binding.source = XBG_SOURCE_TEXTURE;
    vs.bindings.push_back(binding);
    state.tex[0][0] = 1;
    // A missing descriptor must remain a miss until registration actually exists.
    compare();
    legacy_textures[1] = {};
    compare();
    auto hits = texture_record_hits;
    compare();
    assert(texture_record_hits > hits);
    // Registration updates the live node, so a cached pointer sees new bytes.
    legacy_textures[1].texture.mem = 0;
    compare();
    legacy_textures[1].texture.mem = 4096;
    compare();
    // Late creation of the actual pool changes table identity immediately.
    textures[1][1] = {};
    textures[1][1].texture.mem = 0;
    compare();
    textures[1][1].texture.mem = 4096;
    compare();
    // Disabling the existing cache retains direct-map behavior and counters.
    texture_record_cache_enabled = false;
    hits = texture_record_hits;
    compare();
    assert(texture_record_hits == hits);
    texture_record_cache_enabled = true;
    // Shutdown-style clear invalidates every stored node before reusing a table.
    clear_texture_record_cache();
    textures.clear();
    legacy_textures.clear();
    compare();
}
"""

def main():
 p=argparse.ArgumentParser();p.add_argument('--source',type=Path,default=Path(__file__).resolve().parents[2]/'src/graphics/nvn_vulkan/engine/xbg_game.cpp');p.add_argument('--header',type=Path);p.add_argument('--engine-dir',type=Path);a=p.parse_args();s=a.source.read_text();engine=a.engine_dir or a.source.parent
 current=fn(s,'bool indexed_graphics_ready(');ready=fn(s,'bool indexed_range_ready(');clean=fn(s,'bool indirect_cpu_clean(')
 assert 'bool indexed_bbox_ready_enabled=false;' in s
 assert 'texel_copy_enabled=true;indexed_bbox_ready_enabled=false;' in s
 assert '{"bboxready",&indexed_bbox_ready_enabled}' in s
 assert current.index('return reject("state")')<current.index('if(indexed_bbox_ready_enabled')<current.index('if(!range_ready(a.ibuf')
 assert 'XbgIndexedBounds bounds;const XbgIndexedBounds *active_bounds=nullptr;' in current
 assert ready.index('if(!address || !bytes')<ready.index('if(!bounds')
 # Exact audit: only the explicit optional bounds argument and corresponding
 # list guards differ from the frozen range predicate; reverse order retained.
 stripped=ready.replace(',const XbgIndexedBounds *bounds=nullptr','').replace('if(!bounds || !bounds->reports.outside(address,bytes))','').replace('if(!bounds || !bounds->writebacks.outside(address,bytes))','')
 assert stripped==ORIGINAL_RANGE
 # The eligibility path reuses the existing registered-node lookup. Keep the
 # frozen direct-map function as an independent behavioral reference.
 old_lookup = 'auto found=table.find(id);if(found==table.end())return false;auto &t=found->second.texture;'
 cached_lookup = ('const auto *record=find_texture_record(table,id);\n'
                  '                if(!record)return false;\n'
                  '                const auto &t=record->texture;')
 assert current.count(cached_lookup) == 1
 direct_current = current.replace(cached_lookup, old_lookup)
 assert direct_current[direct_current.index('    if(!range_ready(a.ibuf'):]==ORIGINAL_GRAPHICS[ORIGINAL_GRAPHICS.index('    if(!range_ready(a.ibuf'):]
 registry_start = s.index('struct TextureRecordFront {')
 registry_end = s.index('std::map<uint32_t,RegisteredTexture> null_textures;', registry_start)
 registry = s[registry_start:registry_end]
 harness = HARNESS.replace('struct Record {Texture texture;};', 'struct Record {Texture texture;};using RegisteredTexture=Record;') + registry
 test = TEST.replace('pending_reports.clear();pending_writebacks.clear();textures.clear();', 'clear_texture_record_cache();pending_reports.clear();pending_writebacks.clear();textures.clear();')
 test = test.replace('int main(){', 'void record_cache_cases();\nint main(){').replace(' // Rebuild after pin-like', ' record_cache_cases();\n // Rebuild after pin-like')
 test += RECORD_CACHE_CASES
 failure=next(x for x in s.splitlines() if x.startswith('struct IndexedInputFailure '))
 with tempfile.TemporaryDirectory(prefix='bbox-ready-') as tmp:
  d=Path(tmp)
  for name in ['xbg_writeback_policy.h','xbg_indexed_ready_probe.h']:(d/name).write_text((engine/name).read_text())
  (d/'xbg_indexed_bbox_probe.h').write_text((a.header or engine/'xbg_indexed_bbox_probe.h').read_text())
  prefix='#include "xbg_writeback_policy.h"\n#include "xbg_indexed_ready_probe.h"\n#include "xbg_indexed_bbox_probe.h"\n'
  cpp=d/'test.cpp';exe=d/'test';cpp.write_text(prefix+harness+ORIGINAL_RANGE.replace('bool indexed_range_ready(', 'bool original_range(')+ready+clean+failure+ORIGINAL_GRAPHICS.replace('bool indexed_graphics_ready(', 'bool original_graphics(').replace('return indexed_range_ready(', 'return original_range(')+current+test)
  subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(cpp),'-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True)
 print('PASS full graphics 20000 states/failure fields plus 200000 actual-policy ranges, independent diagnostics/default-off/pin/newest/overflow; ASan/UBSan; GPU NOT_RUN')
if __name__=='__main__':main()
