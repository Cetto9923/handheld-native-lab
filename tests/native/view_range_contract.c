#include "../../src/graphics/nvn_vulkan/engine/xbg_framebuffer.h"
#include <stdio.h>
static int clear_coverage_contract(void){
    int errors=0;
    xbg_framebuffer_attachment bound[9]={{1,32,32,1,32,0,1,0,32,1}};
    xbg_framebuffer_extent plan={0};
    if(xbg_framebuffer_plan_layered(bound,9,16384,16384,&plan,NULL) ||
       !xbg_framebuffer_covers_clear(&plan,&bound[0]))errors++;
    bound[8]=(xbg_framebuffer_attachment){1,32,32,1,1,0,1,0,1,1};
    if(xbg_framebuffer_plan_layered(bound,9,16384,16384,&plan,NULL) || plan.layers!=1 ||
       xbg_framebuffer_covers_clear(&plan,&bound[0]) || !xbg_framebuffer_covers_clear(&plan,&bound[8]))errors++;
    bound[8]=(xbg_framebuffer_attachment){1,16,32,1,32,0,1,0,32,1};
    if(xbg_framebuffer_plan_layered(bound,9,16384,16384,&plan,NULL) || plan.width!=16 ||
       xbg_framebuffer_covers_clear(&plan,&bound[0]))errors++;
    bound[8].width=32;bound[8].height=16;
    if(xbg_framebuffer_plan_layered(bound,9,16384,16384,&plan,NULL) || plan.height!=16 ||
       xbg_framebuffer_covers_clear(&plan,&bound[0]))errors++;
    bound[8].active=0;bound[0].base_layer=5;
    if(xbg_nvn_view_count(32,5,0,&bound[0].layer_count) || bound[0].layer_count!=27 ||
       xbg_framebuffer_plan_layered(bound,9,16384,16384,&plan,NULL) || plan.layers!=27 ||
       !xbg_framebuffer_covers_clear(&plan,&bound[0]))errors++;
    bound[0]=(xbg_framebuffer_attachment){1,64,32,3,8,1,1,3,2,1};
    if(xbg_framebuffer_plan_layered(bound,9,16384,16384,&plan,NULL) || plan.width!=32 || plan.height!=16 ||
       !xbg_framebuffer_covers_clear(&plan,&bound[0]))errors++;
    plan.layers=1;if(xbg_framebuffer_covers_clear(&plan,&bound[0]))errors++;
    if(xbg_framebuffer_covers_clear(NULL,&bound[0]) || xbg_framebuffer_covers_clear(&plan,NULL))errors++;
    return errors;
}
int main(void){
    int errors=clear_coverage_contract();uint32_t count=99;
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
