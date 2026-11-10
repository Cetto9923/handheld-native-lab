#include "../../src/graphics/nvn_vulkan/engine/xbg_capture_layout.h"
#include <cassert>
#include <cstdio>
int main(){
    constexpr uint64_t cap=256ull*1024*1024;XbgCaptureLayout p;
    assert(xbg_capture_layout(139,1,512,256,1,8,cap,p));assert(p.bytes==65536 && p.row_bytes==1024 && p.block_rows==64);
    uint64_t sum=0;for(unsigned m=0;m<10;m++){
        unsigned w=512u>>m,h=256u>>m;if(!w)w=1;if(!h)h=1;
        assert(xbg_capture_layout(139,1,w,h,1,8,cap,p));sum+=p.bytes;
    }assert(sum==87400);
    assert(xbg_capture_layout(139,1,5,5,1,8,cap,p) && p.bytes==32);
    assert(xbg_capture_layout(146,1,1,1,1,16,cap,p) && p.bytes==16);
    assert(xbg_capture_layout(77,1,1384,780,1,4,cap,p) && p.bytes==4318080);
    assert(xbg_capture_layout(97,1,480,272,1,8,cap,p) && p.bytes==1044480);
    assert(xbg_capture_layout(129,2,1384,780,1,4,cap,p) && p.bytes==4318080);
    assert(xbg_capture_layout(129,4,1384,780,1,4,cap,p) && p.bytes==1079520);
    assert(!xbg_capture_layout(139,2,4,4,1,8,cap,p));
    assert(!xbg_capture_layout(157,1,4,4,1,16,cap,p));
    assert(!xbg_capture_layout(97,1,UINT32_MAX,UINT32_MAX,UINT32_MAX,8,cap,p));
    assert(!xbg_capture_layout(97,1,0,1,1,8,cap,p));
    XbgCaptureBufferCopy b;
    assert(xbg_capture_buffer_copy(2,2,32,cap,b));assert(b.source_offset==0 && b.copy_bytes==4 && b.valid_offset==2 && b.valid_bytes==2);
    assert(!xbg_capture_buffer_copy(0,2,2,cap,b)); // Never infer allocation padding beyond VkBuffer size.
    assert(!xbg_capture_buffer_copy(UINT64_MAX-1,4,UINT64_MAX,cap,b));
    assert(xbg_capture_buffer_copy(256,20,276,cap,b) && b.copy_bytes==20);
    puts("raw BC blocks/mips, depth/stencil planes, overflow/budget and exact valid buffer spans: PASS");
}
