/* Actual command recorder; explicit CPU family/bridge seams, no GPU claim. */
#include "../../src/graphics/nvn_device/nvn_command_buffer.h"
#include "../../src/graphics/nvn_device/nvn_obj_ffrs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned char memory[2048];
static int pool, texture, view;
static ffrs_tex texture_value;
static ffrs_view view_value;
static unsigned bridge_view_calls;
static uint64_t generation;
int nvn_trace_enabled(void){return 0;}
void nvn_engine_pool_del(void *p){assert(p);}
int nvn_memory_pool_info_of(const void *p,nvn_memory_pool_info *out) {
    if(p!=&pool)return 0;
    *out=(nvn_memory_pool_info){memory,sizeof(memory),0,1};return 1;
}
const ffrs_tex *nvn_bridge_texture(const void *p){return p==&texture?&texture_value:NULL;}
const ffrs_view *nvn_bridge_view(const void *p){++bridge_view_calls;return p==&view?&view_value:NULL;}
int nvn_texture_view_info_of(const void *p,ffrs_view *out,uint64_t *serial) {
    if(p!=&view)return 0;*out=view_value;*serial=generation;return 1;
}
int main(void) {
    int buffer;assert(nvn_command_buffer_initialize(&buffer));
    const void *colors[]={&texture},*views[]={&view};
    nvn_cb_p_set_rt first;
    for(unsigned i=0;i<10000;i++) {
        generation=i+1;memset(&view_value,0,sizeof(view_value));
        view_value.set_levels=1;view_value.base_level=i%16;view_value.nlevels=1;
        view_value.set_swizzle=1;view_value.swz[0]=i%6;
        nvn_command_buffer_add_command_memory_storage(&buffer,&pool,0,sizeof(memory));
        nvn_command_buffer_begin_recording(&buffer);
        nvn_command_buffer_rec_set_render_targets(&buffer,1,colors,sizeof(texture),views,sizeof(view),NULL,NULL);
        uint64_t handle=nvn_command_buffer_end_recording(&buffer);assert(handle);
        nvn_command_package package;assert(nvn_command_buffer_get_package(handle,&package));
        nvn_cb_op_hdr hdr;memcpy(&hdr,package.records,sizeof(hdr));assert(hdr.op==NVN_CB_OP_SET_RT);
        nvn_cb_p_set_rt record;memcpy(&record,package.records+sizeof(hdr),sizeof(record));
        assert(record.n==1 && record.color[0]==&texture_value && record.has_cview[0]);
        assert(!memcmp(&record.cview[0],&view_value,sizeof(view_value)));
        if(i==0)first=record;
    }
    /* The earlier command value remains its original snapshot after every
     * subsequent guest view change; no permanent bridge-view call is needed. */
    assert(first.cview[0].base_level==0 && first.cview[0].swz[0]==0);
    for(unsigned i=0;i<2;i++) {
        views[0]=i?&pool:NULL; /* unknown family object and absent view */
        nvn_command_buffer_add_command_memory_storage(&buffer,&pool,0,sizeof(memory));
        nvn_command_buffer_begin_recording(&buffer);
        nvn_command_buffer_rec_set_render_targets(&buffer,1,colors,sizeof(texture),views,sizeof(view),NULL,NULL);
        uint64_t handle=nvn_command_buffer_end_recording(&buffer);assert(handle);
        nvn_command_package package;assert(nvn_command_buffer_get_package(handle,&package));
        nvn_cb_p_set_rt record;memcpy(&record,package.records+sizeof(nvn_cb_op_hdr),sizeof(record));
        assert(!record.has_cview[0]);
    }
    assert(bridge_view_calls==0);
    puts("command view snapshot PASS:10000 generations, exact16-byte records, null/unknown views, no bridge-view allocation path CPU_ONLY");
    return 0;
}
