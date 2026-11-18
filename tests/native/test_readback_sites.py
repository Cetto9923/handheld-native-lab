from pathlib import Path
import subprocess,tempfile,re,sys
source=Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).resolve().parents[2]/'src/graphics/nvn_vulkan/engine/xbg_game.cpp'
s=source.read_text()
def function(signature):
 start=s.index(signature); p=s.index('{',start); depth=1;i=p+1
 while depth:
  if s[i]=='{':depth+=1
  elif s[i]=='}':depth-=1
  i+=1
 return s[start:i]
HARNESS='\n#include <vector>\n#include <cstdint>\n#include <cstdlib>\n#include <cstring>\n#include <cassert>\n#include <stdexcept>\nusing VkDeviceSize=uint64_t;\n#define DESC_CALL(k,e) (e)\n#define DESC_SCOPE(k) ((void)0)\nvoid need(bool b,const char*){if(!b)throw std::runtime_error("need");}\nstruct Root {void *recorder;};Root root{&root};Root *descriptor_root=&root;bool descriptor_detail=true;\nstruct Timing {uint64_t readback_alloc_bytes=0,readback_copy_bytes=0,readback_zero_bytes=0,readback_guard_bytes=0,readback_noncoherent_flushes=0;} descriptor_timing;\nunsigned flushes; std::vector<uint8_t> dest(256),stage(256);bool coherent;\nstruct Buffer {unsigned buffer;uint64_t offset;void *mapped;bool coherent;unsigned memory=3;size_t size=64;uint64_t allocation_size=256;void flush(){if(!coherent)++flushes;}};\nstruct Arena {Buffer alloc(size_t n){need(n && n<=256,"size");return {1,0,dest.data(),coherent};}};\nstruct Guard {unsigned buffer;uint64_t begin,end;};\nstruct Op {unsigned src,dst;uint64_t a,b,n;};\nstruct Slot {Arena readback;std::vector<Guard> guard;std::vector<Op> pro_ops;} slot;\nSlot &recording_slot(){return slot;}\nnamespace evguard {struct EngineRead {EngineRead(const void*,size_t){}};}\nuint64_t xview_guarded=0,xview_guarded_bytes=0;\nBuffer stream_buffer(size_t n,const void *p,size_t c,bool z){need(c<=n,"size");if(p && c)memcpy(stage.data(),p,c);if(z && c<n)memset(stage.data()+c,0,n-c);return {2,0,stage.data(),true};}\n\nbool readback_fullcopy_enabled=false;uint64_t readback_fullcopy_hits=0,readback_fullcopy_bytes=0;bool aboff=false;bool ab_off(const char*){return aboff;}\n'
ORIGINAL='Buffer original(size_t n,const void *data=nullptr,size_t copy=0){\n    Slot &s=recording_slot();Buffer b=s.readback.alloc(n);\n    for(const auto &g:s.guard)if(g.buffer==b.buffer && b.offset<g.end && g.begin<b.offset+n){\n        if(data && copy)need(copy<=n,"Buffer copy overflow");\n        Buffer src=stream_buffer(n,data,data?copy:0,true);s.pro_ops.push_back({src.buffer,b.buffer,src.offset,b.offset,VkDeviceSize(n)});\n        ++xview_guarded;xview_guarded_bytes+=n;return b;}\n    if(data && copy){need(copy<=n,"Buffer copy overflow");need(b.mapped!=nullptr,"readback_buffer destination mapped pointer is null");evguard::EngineRead er(data,copy);memcpy(b.mapped,data,copy);}else copy=0;\n    if(copy<n){need(b.mapped!=nullptr,"readback_buffer zero-fill destination mapped pointer is null");memset(static_cast<uint8_t*>(b.mapped)+copy,0,n-copy);}   // only the bytes the copy leaves\n    b.flush();return b;\n}'

helper=s[s.index('enum class ReadbackSite'):s.index('bool perf_bounded_contaminated')]
code=HARNESS+'\nbool timing_active=true;\n'+helper+function('bool readback_fullcopy_bounds(')+ORIGINAL+function('Buffer readback_buffer(')+r"""
int main(){
 unsigned char data[64];for(unsigned i=0;i<64;++i)data[i]=i;
 for(bool active:{false,true})for(bool detail:{false,true})for(bool rootok:{false,true})for(bool guarded:{false,true})for(bool hasdata:{false,true})for(unsigned site=0;site<5;++site)for(size_t ncopy:{0u,16u,64u}){
  auto reset=[&](){dest.assign(256,0xA5);stage.assign(256,0xCC);slot.guard.clear();slot.pro_ops.clear();if(guarded)slot.guard.push_back({1,0,64});flushes=0;coherent=false;};
  reset();original(64,hasdata?data:nullptr,ncopy);auto expect=dest,es=stage;auto ef=flushes,ep=unsigned(slot.pro_ops.size());
  reset();timing_active=active;descriptor_detail=detail;descriptor_root=rootok?&root:nullptr;for(auto &v:readback_sites)v={};
  readback_buffer(64,hasdata?data:nullptr,ncopy,nullptr,ReadbackSite(site));
  assert(dest==expect && stage==es && flushes==ef && slot.pro_ops.size()==ep);
  for(unsigned i=0;i<5;++i){auto &v=readback_sites[i];bool on=active&&detail&&rootok&&i==site;assert(v.calls==unsigned(on));assert(v.alloc_bytes==(on?64:0));assert(v.sizes[0]==unsigned(on));assert(v.guard_bytes==(on&&guarded?64:0));assert(v.copy_bytes==(on&&!guarded&&hasdata?ncopy:0));assert(v.zero_bytes==(on&&!guarded?64-(hasdata?ncopy:0):0));}
 }
 for(size_t n:{0ull,4096ull,4097ull,65536ull,65537ull,1048576ull,1048577ull,16777216ull,16777217ull}){ReadbackSiteStats v;v.allocation(n);unsigned b=n<=4096?0:n<=65536?1:n<=1048576?2:n<=16777216?3:4;assert(v.sizes[b]==1 && v.calls==1 && v.alloc_bytes==n);}
 timing_active=descriptor_detail=true;descriptor_root=&root;slot.guard.clear();readback_fullcopy_enabled=true;Buffer src{5,0,nullptr,true};src.memory=4;for(auto &v:readback_sites)v={};readback_buffer(64,nullptr,0,&src,ReadbackSite::TexelPending);auto &v=readback_sites[3];assert(v.fullcopy_bytes==64 && v.zero_bytes==0 && v.copy_bytes==0);
}
"""
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined','-O1',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
# Exact production gating and labeling expressions, tested in all states.
normal=re.search(r'const bool perf_normalizable=(.*?);',s).group(1)
div=re.search(r'const double desc_divisor=(.*?);',s).group(1)
assert normal=='perf_timed_frames && !perf_bounded_contaminated && !timing_active'
assert div=='perf_normalizable?double(perf_timed_frames):1.0'
assert 'perf_bounded_contaminated=true;' in function('void timing_start(')
assert 'perf_bounded_contaminated=timing_active;' in s
assert 'parts normalization=unavailable reason=%s' in s
assert 'perf_normalizable?"storage_union_mb_per_sampled_frame":"storage_union_mb_window_total"' in s
assert 'storage_pages.size()*4096.0/1048576.0/desc_divisor' in s
assert 'for(unsigned i=0;i<XbgCpuTiming::Count;++i)perf_timing_before[i]=timing.counters[i];' in s
for frames in (0,1,6,60):
 for bounded in (False,True):
  for active in (False,True):
   valid=frames and not bounded and not active
   divisor=frames if valid else 1
   assert (120/divisor)==(120/frames if valid else 120)
assert s.count('ReadbackSite::StoragePending)')==1
assert s.count('ReadbackSite::StorageCPU)')==1
assert s.count('ReadbackSite::TexelPending)')==2
assert s.count('ReadbackSite::TexelCPU)')==1
print('PASS: 480 readback differential/gating/site cases; boundary buckets/fullcopy; normalization source contract + 16 states')
