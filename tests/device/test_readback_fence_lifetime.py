"""Run the production readback path with fences reset during slot retirement."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class ReadbackFenceLifetime(unittest.TestCase):
    def test_completion_before_wait(self):
        source = (ROOT / 'src/graphics/nvn_vulkan/engine/xbg_game.cpp').read_text()
        start = source.index('void readback_sync(uint64_t &reason){')
        body = source[start:source.index('void retire_handle(', start)]
        prefix = r'''
#include <cassert>
#include <cstdint>
#include <algorithm>
#include <stdexcept>
#include "xbg_readback_retire.h"
constexpr unsigned kMaxSlots=4, SITE_GPU_SYNC=0;
constexpr int VK_NOT_READY=1;
struct Slot { bool recording=false, pending=false; int fence=0; };
Slot slots[kMaxSlots];
unsigned slot_count=4, slot_index=2, sync_site=0, waits=0, cleanups=0;
bool readback_retire_targeted=true, complete_owner=false;
bool signaled[kMaxSlots];
struct { uint64_t readback_fallback=0, readback_no_wait=0,
    readback_targeted=0, readback_inflight=0; } perf;
struct { int device=0; uint64_t fence_timeout_ns=2000000000; } cfg;
uint64_t block_ns[1]{}, block_calls[1]{};
struct SiteScope { explicit SiteScope(unsigned) {} };
uint64_t timing_clock() { return 1; }
unsigned prev_slot_index() { return (slot_index+slot_count-1)%slot_count; }
void submit_slots(bool drain) {
    if(slots[slot_index].recording) {
        slots[slot_index].recording=false; slots[slot_index].pending=true;
        slot_index=(slot_index+1)%slot_count;
    }
    if(drain) for(auto &s:slots) { s.pending=false; signaled[s.fence]=false; }
}
int vkGetFenceStatus(int, int fence) { return signaled[fence]?0:VK_NOT_READY; }
void retire_completed_slots() {
    ++cleanups;
    for(auto &s:slots) if(s.pending && signaled[s.fence]) {
        s.pending=false; signaled[s.fence]=false;
    }
}
int hybrid_wait_fence(int fence, uint64_t timeout) {
    ++waits; assert(timeout>=10000000000ull);
    // An unsignaled fence with no pending submission can never complete.
    if(!slots[fence].pending) return 2;
    signaled[fence]=true;
    return 0;
}
#define VK(call) do { if((call)!=0) throw std::runtime_error("reset fence timeout"); } while(0)
'''
        suffix = r'''
int main() {
    try {
        for(unsigned scenario=0;scenario<6;++scenario) {
            for(unsigned i=0;i<kMaxSlots;++i) { slots[i]={false,false,int(i)}; signaled[i]=false; }
            perf={}; waits=cleanups=0; slot_index=2;
            readback_retire_targeted=scenario!=3;
            slots[2].recording=true;
            if(scenario==2) { slots[2].recording=false; slots[1].pending=false; }
            if(scenario==5) { slots[2].recording=false; slots[1].pending=true; }
            unsigned owner=scenario==2 || scenario==5?1:2;
            signaled[owner]=scenario==0 || scenario==5;
            slots[0].pending=true; signaled[0]=true;
            if(scenario==4) slots[0].pending=false;
            uint64_t reason=0; readback_sync(reason); assert(reason==1);
            if(scenario==0 || scenario==5) {
                assert(!waits && perf.readback_no_wait==1 && !slots[owner].pending);
            } else if(scenario==1 || scenario==4) {
                assert(waits==1 && perf.readback_targeted==1 && !slots[owner].pending);
            } else assert(!waits);
        }
    } catch(const std::runtime_error &) { return 9; }
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            variants = [('fixed', body)]
            guard = 'if(!owner->pending){++perf.readback_no_wait;return;}'
            if guard in body:
                variants.append(('old', body.replace(guard, '')))
            for label, implementation in variants:
                cpp = path / (label + '.cpp')
                cpp.write_text(prefix + implementation + suffix)
                binary = path / label
                subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                                '-fsanitize=address,undefined', '-I',
                                str(ROOT / 'src/graphics/nvn_vulkan/engine'),
                                str(cpp), '-o', str(binary)], check=True)
                result = subprocess.run([str(binary)])
                self.assertEqual(result.returncode, 9 if label == 'old' else 0)


if __name__ == '__main__':
    unittest.main()
