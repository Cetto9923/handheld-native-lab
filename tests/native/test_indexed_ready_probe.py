"""Extract current range predicate; verify diagnostic equivalence. No device/GPU."""
from pathlib import Path
import argparse,subprocess,tempfile

def function(s, signature):
    a=s.index(signature);return s[a:s.index('\n}',a)+2]

HARNESS=r'''
#include <vector>
#include <cassert>
#include <cstdint>
#include <random>
using VkDeviceSize=uint64_t;
struct NVNcounterData {uint64_t words[4];};
struct Report {uint64_t addr;};
struct Buffer {uint64_t offset;};
struct Writeback {uint64_t addr,bytes;Buffer buffer;};
std::vector<Report> pending_reports;
std::vector<Writeback> pending_writebacks;
bool xbg_wb_overlaps(uint64_t a,uint64_t b,uint64_t c,uint64_t d){return a<c+d && c<a+b;}
'''
TEST=r'''
int main(){
 std::mt19937 rng(1414);
 for(unsigned iter=0;iter<10000;++iter){
  pending_reports.clear();pending_writebacks.clear();
  for(unsigned i=0,n=rng()%8;i<n;++i)pending_reports.push_back({rng()%1024});
  for(unsigned i=0,n=rng()%12;i<n;++i)pending_writebacks.push_back({rng()%1024,1+rng()%256,{rng()%128}});
  uint64_t a=rng()%1200,b=rng()%256,align=1ull<<(rng()%8);
  if(iter%20==0)a=UINT64_MAX-8;
  bool expected=original(a,b,align);
  XbgIndexedReadyStats stats;
  {XbgIndexedReadyProbe probe(&stats);
   assert(indexed_range_ready(a,b,align,&probe)==expected);
   assert(indexed_range_ready(a,b,align,&probe)==expected);
  }
  assert(stats.calls==1 && stats.requests==2 && stats.unique==1 && stats.duplicates==1);
  assert(stats.reports_steps==2*stats.duplicate_reports_steps);
  assert(stats.writebacks_steps==2*stats.duplicate_writebacks_steps);
  assert(indexed_range_ready(a,b,align)==expected);
  {XbgIndexedReadyProbe probe(nullptr);assert(indexed_range_ready(a,b,align,&probe)==expected);assert(probe.requests==0 && probe.used==0);}
 }
 // A pin appends a partial write between calls: never inherit readiness/key state.
 pending_reports.clear();pending_writebacks.clear();XbgIndexedReadyStats total;
 {XbgIndexedReadyProbe probe(&total);assert(indexed_range_ready(64,64,4,&probe));}
 pending_writebacks.push_back({64,32,{0}});
 {XbgIndexedReadyProbe probe(&total);assert(!indexed_range_ready(64,64,4,&probe));}
 assert(total.unique==2 && total.duplicates==0);
 XbgIndexedReadyStats vs;
 {XbgIndexedReadyProbe p(&vs);p.vertex_stream(0);p.vertex_stream(0);p.vertex_stream(15);p.vertex_stream(15);}
 {XbgIndexedReadyProbe p(&vs);p.vertex_stream(0);}
 assert(vs.vertex_requests==5 && vs.vertex_repeats==2);
 // Alignment is part of the exact key; saturation excludes the WHOLE call
 // from exact uniqueness totals rather than inventing accuracy after 128 keys.
 XbgIndexedReadyStats s;
 {XbgIndexedReadyProbe p(&s);p.request(1,1,4);p.request(1,1,8);p.request(1,1,4);}
 assert(s.unique==2 && s.duplicates==1);
 {XbgIndexedReadyProbe p(&s);for(unsigned i=0;i<129;++i)p.request(i,4,4);}
 assert(s.overflow_calls==1 && s.exact_calls==1 && s.exact_requests==3 && s.requests==132);
}
'''
def main():
 p=argparse.ArgumentParser();p.add_argument('--source',type=Path,default=Path(__file__).resolve().parents[2]/'src/graphics/nvn_vulkan/engine/xbg_game.cpp');p.add_argument('--header',type=Path)
 a=p.parse_args();s=a.source.read_text();h=(a.header or a.source.parent/'xbg_indexed_ready_probe.h').read_text()
 current=function(s,'bool indexed_range_ready(')
 # BBox shadow diagnostic has its own actual-helper tests. Remove ONLY its
 # audited signature/counter calls here, retaining exact original predicate proof.
 if 'XbgIndexedBBoxProbe' in current:
  removals=[(',XbgIndexedBBoxProbe *bbox=nullptr',''),('    if(bbox)bbox->request(address,bytes);\n',''),('if(bbox)bbox->report_step();',''),('if(bbox)bbox->writeback_step();','')]
  for old,new in removals:
   assert current.count(old)==1, 'Unexpected bbox diagnostic shape'
   current=current.replace(old,new)
  assert 'bbox' not in current

 # Independent frozen original below, not a current-vs-current comparison.
 graphics=function(s,'bool indexed_graphics_ready(')
 assert 'XbgIndexedReadyProbe probe(timing_active?&indexed_ready_stats:nullptr);' in graphics
 assert graphics.count('range_ready(')==5 # lambda original call plus 4 audited sites
 assert 'probe.vertex_stream(v.stream);auto r=state.vb[v.stream];' in graphics
 assert 'pin_indexed_input(' not in graphics
 assert 'if(probe)probe->request(address,bytes,alignment);' in current
 stripped=current.replace(',XbgIndexedReadyProbe *probe=nullptr','').replace('    if(probe)probe->request(address,bytes,alignment);\n','').replace('if(probe)probe->report_step();','').replace('        if(probe)probe->writeback_step();\n','')
 stripped=stripped.replace('for(auto &r:pending_reports){if(xbg_wb_overlaps(address,bytes,r.addr,sizeof(NVNcounterData)))return false;}','for(auto &r:pending_reports)if(xbg_wb_overlaps(address,bytes,r.addr,sizeof(NVNcounterData)))return false;')
 assert stripped==ORIGINAL, 'Diagnostic changed original predicate'
 assert 'indexed_ready_stats={};' in s and 'NVN INDEXED_READY v=1' in s
 with tempfile.TemporaryDirectory(prefix='indexed-ready-') as tmp:
  cpp=Path(tmp)/'test.cpp';exe=Path(tmp)/'test';cpp.write_text(h.replace('#pragma once','')+HARNESS+ORIGINAL.replace('bool indexed_range_ready(', 'bool original(')+current+TEST)
  subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(cpp),'-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True)
 print('PASS 10000 differential cases, exact keys/scans, disabled, pin fresh-call, saturation; ASan/UBSan; GPU NOT_RUN')
ORIGINAL = 'bool indexed_range_ready(uint64_t address,uint64_t bytes,VkDeviceSize alignment){\n    if(!address || !bytes || address+bytes<address)return false;\n    for(auto &r:pending_reports)if(xbg_wb_overlaps(address,bytes,r.addr,sizeof(NVNcounterData)))return false;\n    for(auto w=pending_writebacks.rbegin();w!=pending_writebacks.rend();++w){\n        if(!xbg_wb_overlaps(address,bytes,w->addr,w->bytes))continue;\n        return address>=w->addr && address+bytes<=w->addr+w->bytes &&\n            (w->buffer.offset+address-w->addr)%alignment==0;\n    }\n    return true;\n}'
if __name__=='__main__':main()
