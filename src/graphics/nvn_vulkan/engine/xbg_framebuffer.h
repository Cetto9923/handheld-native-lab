#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xbg_framebuffer_attachment {
    uint32_t active,width,height,levels,layers;
    uint32_t base_level,level_count,base_layer,layer_count,samples;
} xbg_framebuffer_attachment;
typedef struct xbg_framebuffer_extent { uint32_t width,height,layers,samples; } xbg_framebuffer_extent;
/* Nonempty attachment intersection after selecting each NVN mip/slice.
 * Layered rendering and mixed sample counts require separately enabled support
 * and are rejected. On failure, output is unchanged and bad_index identifies
 * the invalid attachment (UINT32_MAX for an empty set/global extent limit). */
int xbg_framebuffer_plan(const xbg_framebuffer_attachment *attachments,uint32_t count,
    uint32_t max_width,uint32_t max_height,xbg_framebuffer_extent *out,uint32_t *bad_index);
/* Explicit layered path: framebuffer uses the minimum view layer count. */
int xbg_framebuffer_plan_layered(const xbg_framebuffer_attachment *,uint32_t,uint32_t,uint32_t,xbg_framebuffer_extent *,uint32_t *);
/* After a successful plan, a folded clear must cover its selected target mip
 * and every layer of that view. The view's base layer is relative to its image. */
int xbg_framebuffer_covers_clear(const xbg_framebuffer_extent *plan,const xbg_framebuffer_attachment *target);
const char *xbg_framebuffer_error(int result);
/* NVN count zero selects all remaining subresources, as nxnative's image
 * view implementation does. Invalid ranges leave the output untouched. */
int xbg_nvn_view_count(uint32_t total,uint32_t base,uint32_t count,uint32_t *out);
#ifdef __cplusplus
}
#endif
