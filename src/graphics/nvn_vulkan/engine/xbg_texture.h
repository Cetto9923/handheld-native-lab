#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* CPU fallback for the formats actually present in Switch build 94012.
 * sRGB stays encoded in RGB bytes; alpha is never gamma converted or dropped.
 * BC4/5 keep native R/RG channels, never replicate red or reconstruct blue. */
enum xbg_pixels { XBG_RGBA8_UNORM=1, XBG_RGBA8_SRGB=2,
                  XBG_R16_UNORM=3, XBG_RG16_UNORM=4,
                  XBG_R16_SNORM=5, XBG_RG16_SNORM=6, XBG_RGBA16_SFLOAT=7 };
typedef struct xbg_bc_format {
    uint32_t pixels, bytes_per_pixel, bytes_per_block;
} xbg_bc_format;
/* All NVN BC1..BC7 formats (0x41..0x50). BC6H keeps its decoded binary16
 * RGB values with alpha=1; signed BC4/5 use filterable signed 16-bit channels.
 * No HDR clamping, gamma conversion, alpha loss or channel reconstruction. */
int xbg_bc_format_info(uint32_t nvn_format,xbg_bc_format *out);
/* A decoded image may reinterpret only its linear/sRGB partner. Changing
 * compression or signedness would require decoding the source blocks again. */
int xbg_bc_view_compatible(uint32_t source_format,uint32_t view_format);
typedef struct xbg_texture {
    uint32_t width, height, levels, source_code, pixels, bytes_per_pixel;
    uint32_t block_linear, block_height_log2, nvn_format, header_bytes;
    uint64_t offsets[16];
    uint32_t sizes[16];
} xbg_texture;
/* NVN packaged texture memory has no .texture/.sprite header. This descriptor
 * accepts the original guest NVN layout directly (2D, one layer, single sample).
 * block_height_log2=UINT32_MAX selects the existing NVN automatic tile rule. */
typedef struct xbg_nvn_texture_desc {
    uint32_t format,width,height,levels,block_height_log2;
} xbg_nvn_texture_desc;
int xbg_nvn_texture_layout(const xbg_nvn_texture_desc *description,size_t storage_bytes,xbg_texture *out);
int xbg_nvn_texture_decode_mip(const void *storage,size_t storage_bytes,
    const xbg_nvn_texture_desc *description,uint32_t level,void *out,size_t out_bytes);
/* No allocation, no source mutation. All extents and mip storage are checked. */
int xbg_texture_parse(const void *source, size_t bytes, xbg_texture *out);
size_t xbg_texture_mip_bytes(const xbg_texture *texture, uint32_t level);
int xbg_texture_decode_mip(const void *source, size_t source_bytes,
    const xbg_texture *texture, uint32_t level, void *out, size_t out_bytes);
/* Independent block entry point: NVN 0x41..0x50; output up to 128 bytes.
 * BC1_RGB selector3 is black/opaque; BC1_RGBA selector3 is transparent black.
 * BC3 always uses four-color RGB regardless of color endpoint order. */
int xbg_decode_block(uint32_t nvn_format, const void *block, size_t block_bytes,
                     void *out, size_t out_bytes);
/* Decode linear compressed staging rows used by NVN CopyBufferToTexture.
 * row/image stride are compressed-byte strides; zero selects tightly packed.
 * Output is tightly packed decoded pixels, including all slices/layers. */
int xbg_decode_bc_linear(uint32_t nvn_format,uint32_t width,uint32_t height,uint32_t depth,
    const void *source,size_t source_bytes,size_t row_stride,size_t image_stride,
    void *out,size_t out_bytes);
/* Loss-preserving normalized representation for decoded BC5_UNORM on GPUs
 * whose RG16_UNORM lacks linear filtering. Two float32 channels per pixel;
 * Vulkan's missing B/A channels remain 0/1. No half/RG8 quantization, source
 * mutation, allocation, or compression encoding. Buffers must not overlap. */
int xbg_rg16_unorm_to_rg32_float(const void *source,size_t source_bytes,
    void *out,size_t out_bytes,size_t pixels);
/* Inverse normalized conversion, round to nearest UNORM16. Nonfinite/outside
 * [0,1] values fail before writing output; no unquantized GPU-write support is
 * implied. Sources produced from original UNORM16 values round-trip exactly. */
int xbg_rg32_float_to_rg16_unorm(const void *source,size_t source_bytes,
    void *out,size_t out_bytes,size_t pixels);
/* Strided 2D/array/volume transfer; source/destination row and slice pitches
 * are bytes in their respective representations. Zero selects tight packing.
 * to_float=1 is RG16->RG32, 0 is RG32->RG16; no overlapping buffers. */
int xbg_rg16_unorm_float_rows(uint32_t width,uint32_t height,uint32_t depth,
    const void *source,size_t source_bytes,size_t source_row,size_t source_image,
    void *out,size_t out_bytes,size_t out_row,size_t out_image,int to_float);
/* Same compressed row/image-stride contract as xbg_decode_bc_linear, only
 * BC5_UNORM. Output is tightly packed RG32 float across all slices/layers. */
int xbg_decode_bc5_rg32_linear(uint32_t width,uint32_t height,uint32_t depth,
    const void *source,size_t source_bytes,size_t row_stride,size_t image_stride,
    void *out,size_t out_bytes);
#ifdef __cplusplus
}
#endif
