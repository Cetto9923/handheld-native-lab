#include "../../src/graphics/nvn_vulkan/engine/xbg_framebuffer.h"
#include <stdio.h>
int main(void){
    int errors=0;uint32_t count=99;
    if(xbg_nvn_view_count(8,2,0,&count) || count!=6)errors++;
    if(xbg_nvn_view_count(8,2,3,&count) || count!=3)errors++;
    count=99;if(xbg_nvn_view_count(8,8,0,&count)!=-1 || count!=99)errors++;
    if(xbg_nvn_view_count(8,2,7,&count)!=-1 || count!=99)errors++;
    xbg_framebuffer_attachment attachment={1,2,2,1,1,0,0,0,0,1};
    if(xbg_nvn_view_count(1,0,0,&attachment.level_count) || xbg_nvn_view_count(1,0,0,&attachment.layer_count))errors++;
    xbg_framebuffer_extent extent={0};
    if(xbg_framebuffer_plan(&attachment,1,16384,16384,&extent,NULL) || extent.width!=2 || extent.height!=2)errors++;
    printf("view_range_contract errors=%d\n",errors);return errors?1:0;
}
