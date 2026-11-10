#include "src/graphics/nvn_vulkan/engine/xbg_writeback_policy.h"
#include <cassert>
#include <cstdint>
#include <vector>
struct Range {uint64_t addr,bytes;};
int main(){
 std::vector<Range> p{{1000,100}};
 assert(xbg_wb_needs_partial_merge(p,1050,100));
 assert(!xbg_wb_needs_partial_merge(p,1010,20));
 assert(!xbg_wb_needs_partial_merge(p,1100,20));
 p.push_back({900,300});assert(!xbg_wb_needs_partial_merge(p,1050,100));
 p.push_back({1090,20});assert(xbg_wb_needs_partial_merge(p,1050,100));
 assert(!xbg_wb_needs_partial_merge(p,UINT64_MAX-4,10));
 // Later overlapping writes overwrite earlier bytes; untouched bytes remain
 // from the original host snapshot.
 std::vector<Range> writes{{96,12},{104,12},{112,8},{400,4}};
 std::vector<XbgWritebackOverlay> plan;
 assert(xbg_wb_overlay_plan(writes,100,16,plan));assert(plan.size()==3);
 std::vector<int> result(16,0);const int values[]={1,2,3,4};
 for(const auto &c:plan)for(uint64_t i=0;i<c.bytes;i++)result[c.dest_offset+i]=values[c.index];
 for(int i=0;i<16;i++)assert(result[i]==(i<4?1:i<12?2:3));
 auto saved=plan;assert(!xbg_wb_overlay_plan(writes,101,15,plan));assert(plan.size()==saved.size());
 assert(!xbg_wb_overlay_plan(writes,UINT64_MAX-4,8,plan));
 assert(!xbg_wb_overlay_plan(writes,500,16,plan));
 writes.push_back({102,4});assert(!xbg_wb_overlay_plan(writes,100,16,plan));
 // Compare the planned copies with a byte-level chronological reference over
 // varied overlaps, including ranges extending beyond either side.
 uint32_t random=7;
 for(int trial=0;trial<2000;++trial){
  std::vector<Range> ranges;std::vector<int> expected(32,0),actual(32,0);
  for(int n=0;n<8;++n){
   random=random*1664525u+1013904223u;uint64_t start=(random%20)*4;
   random=random*1664525u+1013904223u;uint64_t size=(1+random%8)*4;
   ranges.push_back({start,size});
   for(size_t i=0;i<expected.size();++i)if(32+i>=start && 32+i<start+size)expected[i]=n+1;
  }
  std::vector<XbgWritebackOverlay> copies;bool got=xbg_wb_overlay_plan(ranges,32,32,copies);
  bool any=false;for(int x:expected)any|=x!=0;assert(got==any);
  for(const auto &c:copies)for(uint64_t i=0;i<c.bytes;++i)actual[c.dest_offset+i]=int(c.index)+1;
  assert(actual==expected);
 }
}
