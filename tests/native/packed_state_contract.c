/* Guest memcpy of value objects must preserve draw and query state. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "nvn_packed_state.h"
extern void nvn_render_state_color_set_defaults(void*);
extern void nvn_render_state_color_set_alpha_test(void*,int);
extern int nvn_render_state_color_get_alpha_test(const void*);
extern void nvn_render_state_vertex_attrib_set_defaults(void*);
extern void nvn_render_state_vertex_attrib_set_format(void*,int,int64_t);
extern void nvn_render_state_vertex_attrib_set_stream_index(void*,int);
extern void nvn_render_state_vertex_attrib_get_format(const void*,int*,int64_t*);
extern void nvn_render_state_vertex_stream_set_defaults(void*);
extern void nvn_render_state_vertex_stream_set_stride(void*,int64_t);
extern int64_t nvn_render_state_vertex_stream_get_stride(const void*);
extern void nvn_blend_state_set_defaults(void*);
extern void nvn_blend_state_set_blend_func(void*,int,int,int,int);
extern void nvn_blend_state_get_blend_func(const void*,int*,int*,int*,int*);
#define CHECK(x) do { if(!(x)){fprintf(stderr,"failed line %d: %s\n",__LINE__,#x);errors++;}}while(0)
int main(void){
 int errors=0,a,b,c,d,fmt;int64_t off;
 struct {uint32_t pre;ffrs_color_st state;uint32_t post;} col={0xfeedbabe,{0},0xcafed00d};
 nvn_render_state_color_set_defaults(&col.state);CHECK(col.state.alpha_func==8&&col.state.logic_op==3);
 ffrs_color_st copy=col.state;CHECK(nvn_render_state_color_get_alpha_test(&copy)==8);
 nvn_render_state_color_set_alpha_test(&copy,2);CHECK(copy.alpha_func==2&&col.state.alpha_func==8);
 memcpy(&copy,&col.state,sizeof copy);CHECK(nvn_render_state_color_get_alpha_test(&copy)==8);
 CHECK(col.pre==0xfeedbabe&&col.post==0xcafed00d);
 struct {uint32_t pre;ffrs_va_st state;uint32_t post;} va={0xfeedbabe,{0},0xcafed00d};
 nvn_render_state_vertex_attrib_set_defaults(&va.state);nvn_render_state_vertex_attrib_set_format(&va.state,0x35,1234);nvn_render_state_vertex_attrib_set_stream_index(&va.state,7);
 ffrs_va_st vac=va.state;nvn_render_state_vertex_attrib_get_format(&vac,&fmt,&off);CHECK(fmt==0x35&&off==1234&&vac.stream==7);CHECK(va.pre==0xfeedbabe&&va.post==0xcafed00d);
 ffrs_vs_st vs={0},vsc;nvn_render_state_vertex_stream_set_defaults(&vs);nvn_render_state_vertex_stream_set_stride(&vs,48);vsc=vs;CHECK(vsc.stride==48&&nvn_render_state_vertex_stream_get_stride(&vsc)==48);
 struct {uint64_t pre;ffrs_blend_st state;uint64_t post;} bl={0xfeedbabe,{0},0xcafed00d};
 nvn_blend_state_set_defaults(&bl.state);CHECK(bl.state.src_rgb==2&&bl.state.dst_rgb==1&&bl.state.eq_rgb==1);
 nvn_blend_state_set_blend_func(&bl.state,5,6,2,1);ffrs_blend_st blc=bl.state;nvn_blend_state_get_blend_func(&blc,&a,&b,&c,&d);CHECK(a==5&&b==6&&c==2&&d==1);CHECK(bl.pre==0xfeedbabe&&bl.post==0xcafed00d);
 printf("errors=%d copied_color=%u stride=%u blend=%d,%d,%d,%d\n",errors,copy.alpha_func,vsc.stride,a,b,c,d);return errors!=0;
}
