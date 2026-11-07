#pragma once
#include <stdint.h>

// Vulkan buffer/image copies use tightly packed texel *blocks* when rowLength
// and imageHeight are zero. Numeric formats/aspects are the public Vulkan enum
// values; keep this bounds contract usable in host tests without a Vulkan SDK.
struct XbgCaptureLayout {
    uint32_t block_width=1,block_height=1,block_bytes=0;
    uint64_t row_bytes=0,block_rows=0,bytes=0;
};
inline bool xbg_capture_layout(uint32_t format,uint32_t aspect,uint32_t w,uint32_t h,uint32_t d,
                               uint32_t actual_pixel_bytes,uint64_t limit,XbgCaptureLayout &out){
    out={};if(!w || !h || !d || !limit)return false;
    uint32_t bw=1,bh=1,b=0;
    if(format>=131 && format<=146){ // BC1..BC7, including BC4 UNORM=139.
        if(aspect!=1)return false;
        bw=bh=4;b=(format<=134 || format==139 || format==140)?8:16;
    }else if(aspect==2){
        if(format==124 || format==128)b=2; // D16 and D16/S8 depth plane.
        else if(format==125 || format==126 || format==129 || format==130)b=4;
        else return false;
    }else if(aspect==4){
        if(format<127 || format>130)return false;b=1;
    }else if(aspect==1 && format && format<124 && actual_pixel_bytes && actual_pixel_bytes<=32){
        b=actual_pixel_bytes;
    }else return false; // Other compressed/multiplanar formats need an explicit contract.
    uint64_t columns=(uint64_t(w)+bw-1)/bw,rows=(uint64_t(h)+bh-1)/bh;
    if(columns>limit/b)return false;uint64_t row=columns*b;
    if(rows>limit/row)return false;uint64_t plane=row*rows;
    if(uint64_t(d)>limit/plane)return false;
    out={bw,bh,b,row,rows,plane*d};return true;
}

struct XbgCaptureBufferCopy {uint64_t source_offset=0,copy_bytes=0,valid_offset=0,valid_bytes=0;};
inline bool xbg_capture_buffer_copy(uint64_t offset,uint64_t bytes,uint64_t buffer_size,
                                    uint64_t limit,XbgCaptureBufferCopy &out){
    out={};if(!bytes || offset>buffer_size || bytes>buffer_size-offset)return false;
    uint64_t begin=offset&~uint64_t(3),prefix=offset-begin;
    if(bytes>UINT64_MAX-prefix-3)return false;
    uint64_t copy=(prefix+bytes+3)&~uint64_t(3);
    if(copy>limit || copy>buffer_size-begin)return false;
    out={begin,copy,prefix,bytes};return true;
}
