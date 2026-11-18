"""Bounded raw recorder contracts; no Vulkan/device execution.
Run python3 test_gpu_raw.py --source engine/xbg_game.cpp --header engine/xbg_gpu_raw.h.
"""
from pathlib import Path
import argparse,subprocess,tempfile,re
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,default=Path(__file__).resolve().parents[2]/'src/graphics/nvn_vulkan/engine/xbg_game.cpp');p.add_argument('--header',type=Path,default=Path(__file__).resolve().parents[2]/'src/graphics/nvn_vulkan/engine/xbg_gpu_raw.h');a=p.parse_args();s=a.source.read_text();h=a.header.read_text()
main=r'''
#include <cassert>
#include <type_traits>
#include <limits>
static_assert(std::is_trivially_copyable<XbgGpuRaw::Row>::value,"POD rows");
static XbgGpuRaw g;
int main(){
 assert(!g.enabled);assert(!g.get(g.admit(100,1,1,0,1,true)));
 for(auto count:{0ull,121ull}){g.configure(0,count,1);assert(!g.enabled);}
 g.configure(UINT64_MAX,1,1);assert(!g.enabled);g.configure(0,1,4097);assert(!g.enabled);
 g.configure(10,3,2);assert(!g.get(g.admit(9,1,42,0,1,true)));
 auto one=g.admit(10,2,42,0,2,true);assert(g.get(one));g.get(one)->submit_begin=100;
 g.submit_done(one,0,110,8);assert(g.pending==1);g.submit_done(one,0,111,9);assert(g.pending==1);
 auto two=g.admit(11,3,42,1,1,true);g.submit_done(two,0,120,8);assert(g.pending==2);
 assert(!g.get(g.admit(11,4,42,2,1,true)));assert(g.dropped==1 && g.used==2);
 assert(!g.ready(12));assert(!g.ready(13));assert(g.closed);
 assert(!g.get(g.admit(13,5,42,0,1,true)));
 auto wrong=one;wrong.serial++;assert(!g.get(wrong));wrong=one;wrong.session++;assert(!g.get(wrong));wrong=one;wrong.row=UINT32_MAX;assert(!g.get(wrong));
 g.query(one,1,0,0,150);assert(!g.get(one)->legacy_accepted && g.get(one)->query_read); // NOT_READY
 g.query(two,-4,0,0,151);assert(!g.get(two)->legacy_accepted); // error
 g.retire(one);g.retire(one);assert(g.pending==1);assert(!g.ready(13));g.retire(two);assert(g.ready(13));
 g.exported=true;assert(!g.ready(14));
 auto stale=one;g.configure(0,120,4096);assert(!g.get(stale));
 for(unsigned i=0;i<4096;++i){auto q=g.admit(i%120,i+1,9,i%16,1,i%2);assert(q.row==i);g.submit_done(q,0,i,0);g.query(q,0,5,5,i);assert(g.get(q)->legacy_accepted==bool(i%2));g.retire(q);}
 assert(!g.get(g.admit(119,4097,9,0,1,true)));assert(g.used==4096 && g.dropped==1);assert(g.ready(120));
 // Failed submit remains explicit, cannot underflow pending on retirement.
 g.configure(0,1,1);auto fail=g.admit(0,1,0,0,1,true);g.submit_done(fail,-4,20,0);assert(g.pending==0 && !g.get(fail)->submitted && g.get(fail)->retired);g.retire(fail);assert(g.pending==0);assert(g.ready(1));
 // Preserve original successful-query predicate for any timestamp bit width.
 for(unsigned bits:{0u,32u,64u})for(int result:{0,1,-4})for(uint64_t t0:{0ull,1ull,UINT64_MAX})for(uint64_t t1:{0ull,1ull,UINT64_MAX}){
  g.configure(0,1,1);auto q=g.admit(0,5,9,0,1,bits!=0);g.submit_done(q,0,1,0);g.query(q,result,t0,t1,2);assert(g.get(q)->legacy_accepted==bool(bits && result==0 && t1>=t0));g.retire(q);assert(g.pending==0);
 }
 // Pre/post-window slot handles remain invalid; reconfigure invalidates old session.
 g.configure(10,1,1);auto absent=g.admit(9,1,0,0,1,true);g.submit_done(absent,0,1,0);g.retire(absent);assert(!g.used && !g.pending);
 auto late=g.admit(10,2,0,0,1,true);g.submit_done(late,0,1,0);g.tick(11);assert(!g.get(g.admit(11,3,0,0,1,true)));g.query(late,0,10,20,30);g.retire(late);assert(g.ready(11));g.configure(0,0,0);assert(!g.get(late));
}
'''
with tempfile.TemporaryDirectory() as d:
 t=Path(d);(t/'xbg_gpu_raw.h').write_text(h);(t/'test.cpp').write_text('#include <initializer_list>\n#include "xbg_gpu_raw.h"\n'+main)
 subprocess.run(['clang++','-std=c++17','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(t/'test.cpp'),'-o',str(t/'test')],check=True);subprocess.run([str(t/'test')],check=True)
def check(s):
 assert 'if(auto *r=gpu_raw.get(s.raw_handle))r->submit_begin=timing_clock();' in s
 assert 'if(gpu_raw.get(s.raw_handle))gpu_raw.submit_done(s.raw_handle,int32_t(raw_submit_result),timing_clock(),' in s
 assert 'if(gpu_raw.get(s.raw_handle))gpu_raw.query(s.raw_handle,int32_t(raw_result),t[0],t[1],timing_clock());if(raw_result==VK_SUCCESS && t[1]>=t[0])' in s
 assert s.count('VkResult raw_result=vkGetQueryPoolResults(cfg.device,s.timestamps,0,2,sizeof(t),t,8,VK_QUERY_RESULT_64_BIT);')==1
 assert 'VkResult raw_submit_result=vkQueueSubmit(cfg.queue,1,&si,s.fence);' in s
 assert 'check(raw_submit_result,"vkQueueSubmit(cfg.queue,1,&si,s.fence)");' in s
 assert 'retire_slot(s);}\n        s.raw_handle={};' in s
 assert 'gpu_raw.admit(stats.presents.load(),slot_serial,submitted,slot_index,ncb,s.timestamps!=VK_NULL_HANDLE)' in s
 assert 's.hud_frame=hud::frame.load(std::memory_order_relaxed);if(auto *r=gpu_raw.get(s.raw_handle))r->hud_frame=s.hud_frame;' in s
 export=s[s.index('void gpu_raw_export(bool partial){'):s.index('void save_pipeline_cache(){')]
 assert '(!partial && (!gpu_raw.closed || gpu_raw.pending))' in export
 assert not re.search(r'vk\w+\(|retire_slot\(|submit_slots\(|gfx_drain\(',export)
 assert 'if(gpu_raw.ready(presents))gpu_raw_export(false);' in s
check(s)
for old,new in [('raw_result==VK_SUCCESS && t[1]>=t[0]','true'),('s.raw_handle={};','/*stale*/'),('slot_serial,submitted,slot_index,ncb','slot_serial,s.covers,slot_index,ncb'),('(!partial && (!gpu_raw.closed || gpu_raw.pending))','false')]:
 assert old in s
 try:check(s.replace(old,new,1))
 except AssertionError:pass
 else:raise AssertionError('missed negative '+old)
print('PASS ASan/UBSan state machine capacity4096/reuse/query/late retirement; source gating and four negative controls; Vulkan/device NOT_RUN')
