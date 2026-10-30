/* Runtime-private state objects, matching the MIT nxnative command ABI.
 * These small NVN value objects are copied by the guest; their packed bytes
 * must travel with each copy and be the recorder's source of truth. */
#ifndef FFRS_NVN_PACKED_STATE_H
#define FFRS_NVN_PACKED_STATE_H
#include <stdint.h>
typedef struct { uint32_t blend_en:8,logic_op:4,alpha_func:4; } ffrs_color_st;
typedef struct { uint32_t mask; } ffrs_chmask_st;
typedef struct { uint64_t target:3,src_rgb:7,dst_rgb:7,src_a:7,dst_a:7,eq_rgb:3,eq_a:3; } ffrs_blend_st;
typedef struct { uint8_t enable,samples,a2c,a2c_dither,raster_samples,cov_mod,cov2color_en,cov2color_out,sl_en,sl_grid; } ffrs_ms_st;
typedef struct { uint32_t cull:2,front:1,mode:2,offset_en:3; } ffrs_poly_st;
typedef struct { uint64_t dt:1,dw:1,df:4,st:1,sf_f:4,sf_b:4,fail_f:4,zfail_f:4,zpass_f:4,fail_b:4,zfail_b:4,zpass_b:4; } ffrs_ds_st;
typedef struct { uint32_t fmt:8,offset:19,stream:5; } ffrs_va_st;
typedef struct { uint32_t stride,divisor; } ffrs_vs_st;
#if defined(__cplusplus)
#define FFRS_STATE_ASSERT static_assert
#else
#define FFRS_STATE_ASSERT _Static_assert
#endif
FFRS_STATE_ASSERT(sizeof(ffrs_color_st)==4,"color ABI");
FFRS_STATE_ASSERT(sizeof(ffrs_blend_st)==8,"blend ABI");
FFRS_STATE_ASSERT(sizeof(ffrs_ms_st)==10,"multisample ABI");
FFRS_STATE_ASSERT(sizeof(ffrs_ds_st)==8,"depth ABI");
FFRS_STATE_ASSERT(sizeof(ffrs_va_st)==4,"attribute ABI");
FFRS_STATE_ASSERT(sizeof(ffrs_vs_st)==8,"stream ABI");
#undef FFRS_STATE_ASSERT
#endif
