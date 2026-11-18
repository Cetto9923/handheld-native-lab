"""Production range predicate and actual writeback policy; shadow-only host QA."""
from pathlib import Path
import argparse,subprocess,tempfile

def function(s, signature):
 a=s.index(signature);return s[a:s.index('\n}',a)+2]
HARNESS=r'''
#include <cassert>
#include <random>
#include <vector>
#include <cstdint>
#include "xbg_writeback_policy.h"
#include "xbg_indexed_ready_probe.h"
#include "xbg_indexed_bbox_probe.h"
using VkDeviceSize=uint64_t;
struct NVNcounterData {uint64_t words[4];};
struct Report {uint64_t addr;};struct Buffer {uint64_t offset;};struct Writeback {uint64_t addr,bytes;Buffer buffer;};
std::vector<Report>pending_reports;std::vector<Writeback>pending_writebacks;
'''
TEST=r'''
int main(){
 XbgIndexedBBox b;assert(b.outside(1,1));assert(!b.outside(0,1));assert(!b.outside(1,0));assert(!b.outside(UINT64_MAX,1));
 b.add(100,20);assert(b.outside(80,20));assert(b.outside(120,1));assert(!b.outside(99,2));assert(!b.outside(119,2));
 XbgIndexedBBox zero;zero.add(0,100);assert(!zero.outside(1,1));assert(zero.outside(100,1));
 XbgIndexedBBox top;top.add(UINT64_MAX-16,16);assert(!top.outside(UINT64_MAX-1,1));assert(top.outside(1,1));
 top.add(UINT64_MAX,1);assert(!top.usable && !top.outside(1,1));
 XbgIndexedBBox invalid;invalid.add(32,0);assert(!invalid.usable && !invalid.outside(64,4));
 std::mt19937 rng(8171);
 for(unsigned run=0;run<10000;++run){
  pending_reports.clear();pending_writebacks.clear();
  for(unsigned i=0,n=rng()%10;i<n;++i)pending_reports.push_back({run%11==0?UINT64_MAX-3:rng()%4096});
  for(unsigned i=0,n=rng()%20;i<n;++i)pending_writebacks.push_back({run%13==0?UINT64_MAX-2:rng()%4096,rng()%256,{rng()%64}});
  XbgIndexedBBoxStats stats;XbgIndexedBBoxProbe probe(&stats);probe.build(pending_reports,pending_writebacks,sizeof(NVNcounterData));
  assert(stats.report_build_steps==pending_reports.size() && stats.writeback_build_steps==pending_writebacks.size());
  for(unsigned j=0;j<20;++j){
   uint64_t a=j%11==0?UINT64_MAX-3:rng()%8192,n=rng()%128,align=1ull<<(rng()%7);
   uint64_t rsteps=0,wsteps=0;bool rejected=false;
   if(a && xbg_wb_range_valid(a,n)){
    for(auto r:pending_reports){++rsteps;if(xbg_wb_overlaps(a,n,r.addr,sizeof(NVNcounterData))){rejected=true;break;}}
    if(!rejected)for(auto w=pending_writebacks.rbegin();w!=pending_writebacks.rend();++w){++wsteps;if(xbg_wb_overlaps(a,n,w->addr,w->bytes))break;}
   }
   auto before=stats;bool result=indexed_range_ready(a,n,align,nullptr,&probe);assert(result==original(a,n,align));
   assert(stats.report_scan_steps-before.report_scan_steps==rsteps && stats.writeback_scan_steps-before.writeback_scan_steps==wsteps);
   assert(stats.report_avoidable_steps-before.report_avoidable_steps==(probe.report_outside?rsteps:0));
   assert(stats.writeback_avoidable_steps-before.writeback_avoidable_steps==(probe.writeback_outside?wsteps:0));
   if(probe.report_outside)for(auto r:pending_reports)assert(!xbg_wb_overlaps(a,n,r.addr,sizeof(NVNcounterData)));
   if(probe.writeback_outside)for(auto w:pending_writebacks)assert(!xbg_wb_overlaps(a,n,w.addr,w.bytes));
  }
  XbgIndexedBBoxProbe disabled(nullptr);disabled.build(pending_reports,pending_writebacks,sizeof(NVNcounterData));
  assert(disabled.reports.empty && disabled.writebacks.empty);assert(indexed_range_ready(4096,64,4,nullptr,&disabled)==original(4096,64,4));
 }
 // A new pin changes both bounds and original readiness. Rebuild per call.
 pending_reports.clear();pending_writebacks.clear();XbgIndexedBBoxStats stats;
 {XbgIndexedBBoxProbe p(&stats);p.build(pending_reports,pending_writebacks,32);assert(indexed_range_ready(64,64,4,nullptr,&p));assert(p.writeback_outside);}
 pending_writebacks.push_back({64,32,{0}});
 {XbgIndexedBBoxProbe p(&stats);p.build(pending_reports,pending_writebacks,32);assert(!indexed_range_ready(64,64,4,nullptr,&p));assert(!p.writeback_outside);}
 // Report short-circuit must not invent writeback savings even when wb bbox outside.
 pending_reports={{64}};pending_writebacks={{1024,32,{0}}};stats={};
 {XbgIndexedBBoxProbe p(&stats);p.build(pending_reports,pending_writebacks,32);assert(!indexed_range_ready(64,64,4,nullptr,&p));}
 assert(stats.writeback_outside==1 && stats.writeback_scan_steps==0 && stats.writeback_avoidable_steps==0);
}
'''
def main():
 p=argparse.ArgumentParser();p.add_argument('--source',type=Path,default=Path(__file__).resolve().parents[2]/'src/graphics/nvn_vulkan/engine/xbg_game.cpp');p.add_argument('--header',type=Path);p.add_argument('--engine-dir',type=Path);a=p.parse_args();s=a.source.read_text();engine=a.engine_dir or a.source.parent
 current=function(s,'bool indexed_range_ready(')
 graphics=function(s,'bool indexed_graphics_ready(')
 assert 'XbgIndexedBBoxProbe bbox(timing_active?&indexed_bbox_stats:nullptr);' in graphics
 assert 'bbox.build(pending_reports,pending_writebacks,sizeof(NVNcounterData));' in graphics
 assert 'bbox.out?&bbox:nullptr' in graphics and 'pin_indexed_input(' not in graphics
 stripped=current.replace(',XbgIndexedBBoxProbe *bbox=nullptr','').replace('    if(bbox)bbox->request(address,bytes);\n','').replace('if(bbox)bbox->report_step();','').replace('if(bbox)bbox->writeback_step();','')
 assert stripped==ORIGINAL,'Changed actual predicate/scan sequence'
 with tempfile.TemporaryDirectory(prefix='indexed-bbox-') as tmp:
  d=Path(tmp)
  for name in ['xbg_writeback_policy.h','xbg_indexed_ready_probe.h']:(d/name).write_text((engine/name).read_text())
  (d/'xbg_indexed_bbox_probe.h').write_text((a.header or engine/'xbg_indexed_bbox_probe.h').read_text())
  cpp=d/'test.cpp';exe=d/'test';cpp.write_text(HARNESS+ORIGINAL.replace('bool indexed_range_ready(', 'bool original(')+current+TEST)
  subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(cpp),'-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True)
 print('PASS actual overlap policy, 200000 differential queries/scans, overflow/empty/halfopen/invalid/pin/shortcircuit; ASan/UBSan; GPU NOT_RUN')

ORIGINAL = 'bool indexed_range_ready(uint64_t address,uint64_t bytes,VkDeviceSize alignment,XbgIndexedReadyProbe *probe=nullptr){\n    if(probe)probe->request(address,bytes,alignment);\n    if(!address || !bytes || address+bytes<address)return false;\n    for(auto &r:pending_reports){if(probe)probe->report_step();if(xbg_wb_overlaps(address,bytes,r.addr,sizeof(NVNcounterData)))return false;}\n    for(auto w=pending_writebacks.rbegin();w!=pending_writebacks.rend();++w){\n        if(probe)probe->writeback_step();\n        if(!xbg_wb_overlaps(address,bytes,w->addr,w->bytes))continue;\n        return address>=w->addr && address+bytes<=w->addr+w->bytes &&\n            (w->buffer.offset+address-w->addr)%alignment==0;\n    }\n    return true;\n}'
if __name__=="__main__":main()
