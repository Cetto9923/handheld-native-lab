#include "../../src/graphics/nvn_vulkan/engine/xbg_draw_plan.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

int main(void){
    xbg_draw_call plan;
    const uint32_t empty[][5]={
        {0,1,UINT32_MAX,UINT32_MAX,UINT32_MAX},
        {1,0,UINT32_MAX,UINT32_MAX,UINT32_MAX},
        {0,UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX},
        {UINT32_MAX,0,UINT32_MAX,UINT32_MAX,UINT32_MAX}
    };
    for(int type=-1;type<=2;++type){
        for(size_t i=0;i<sizeof empty/sizeof empty[0];++i){
            assert(xbg_draw_indirect_plan(&plan,4,type,UINT64_MAX,empty[i],sizeof empty[i])==0);
            assert(plan.primitive==4 && plan.index_type==type);
            assert(plan.count==0 && plan.instances==0 && plan.first==0);
            assert(plan.base_instance==0 && plan.base_vertex==0 && plan.index_address==0);
        }
    }
    uint32_t active[5]={3,2,4,UINT32_MAX,7};
    assert(xbg_draw_indirect_plan(&plan,4,1,0x1000,active,sizeof active)==0);
    assert(plan.count==3 && plan.instances==2 && plan.base_vertex==-1);
    assert(plan.base_instance==7 && plan.index_address==0x1008);
    const xbg_draw_call before=plan;
    const unsigned checked[]={0,1,4};
    for(size_t i=0;i<sizeof checked/sizeof checked[0];++i){
        uint32_t invalid[5];memcpy(invalid,active,sizeof invalid);
        invalid[checked[i]]=0x80000000u;
        assert(xbg_draw_indirect_plan(&plan,4,1,0x1000,invalid,sizeof invalid)==-3);
        assert(memcmp(&plan,&before,sizeof plan)==0);
    }
    assert(xbg_draw_indirect_plan(&plan,4,1,0x1001,active,sizeof active)==-5);
    assert(xbg_draw_indirect_plan(&plan,4,2,UINT64_MAX-1,active,sizeof active)==-4);
    assert(xbg_draw_indirect_plan(&plan,4,1,0x1000,active,19)==-2);
    assert(xbg_draw_indirect_plan(&plan,4,3,0,empty[0],sizeof empty[0])==-1);
    assert(xbg_draw_indirect_plan(NULL,4,1,0,empty[0],sizeof empty[0])==-1);
    assert(xbg_draw_indirect_plan(&plan,4,1,0,NULL,20)==-1);
    puts("indirect draw empty-record semantics and active-argument checks PASS");
    return 0;
}
