#include "src/graphics/nvn_vulkan/engine/xbg_spirv_stencil.h"
#include <cassert>
#include <cstdint>
#include <vector>

// Synthetic instruction stream exercises the exact-shape guard without
// including game shader code. The real module is separately spirv-val checked.
int main(){
    const std::vector<uint32_t> original={
        0x07230203,0x00010300,0,5000,0,
        (4u<<16)|43u,1,2179,2,
        (4u<<16)|43u,1,2180,264,
        (4u<<16)|43u,1,4618,3400,
        (3u<<16)|225u,2179,4618,
        (4u<<16)|61u,1,4629,4628,
        (1u<<16)|253u};
    auto patched=original;assert(xbg_ffrs_histogram_workgroup_sync(patched));
    assert(patched.size()==original.size()+5);
    unsigned controls=0,memories=0;
    for(size_t i=5;i<patched.size();){auto n=patched[i]>>16,op=patched[i]&65535u;assert(n);controls+=op==224;memories+=op==225;i+=n;}
    assert(controls==2 && memories==0);
    auto mismatched=original;mismatched[15]=1;auto saved=mismatched;
    assert(!xbg_ffrs_histogram_workgroup_sync(mismatched) && mismatched==saved);
    auto truncated=original;truncated.pop_back();truncated.push_back(4u<<16|61u);saved=truncated;
    assert(!xbg_ffrs_histogram_workgroup_sync(truncated) && truncated==saved);
}
