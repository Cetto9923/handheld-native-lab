#include "../../src/graphics/nvn_vulkan/engine/xbg_spirv_typed_storage.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <vector>
static std::vector<uint32_t> load(const char *p){
    std::ifstream in(p,std::ios::binary|std::ios::ate);assert(in.good());
    auto b=in.tellg();assert(b>=20&&b%4==0);in.seekg(0);
    std::vector<uint32_t> w(size_t(b)/4);in.read(reinterpret_cast<char*>(w.data()),b);assert(in.good());
    return w;
}
int main(int argc,char**argv){
    assert(argc>=2);
    auto m=load(argv[1]);
    // the real module: game-declared typed storage images plus shared formatless ones
    const uint32_t rg32ui=xbg_spirv_declared_image_format(m,47);
    const uint32_t rgba32ui=xbg_spirv_declared_image_format(m,59);
    const uint32_t plain53=xbg_spirv_declared_image_format(m,53);
    const uint32_t plain65=xbg_spirv_declared_image_format(m,65);
    printf("binding 47 declared=%u (Rg32ui=%u)\n", rg32ui, XBG_IMAGE_FORMAT_RG32UI);
    printf("binding 59 declared=%u (Rgba32ui=%u)\n", rgba32ui, XBG_IMAGE_FORMAT_RGBA32UI);
    printf("binding 53 declared=%u (Unknown=%u)\n", plain53, XBG_IMAGE_FORMAT_UNKNOWN);
    printf("binding 65 declared=%u (Unknown=%u)\n", plain65, XBG_IMAGE_FORMAT_UNKNOWN);
    // the game declares 47 and 59 typed; those are NOT engine rewrites
    assert(rg32ui==XBG_IMAGE_FORMAT_RG32UI);
    assert(rgba32ui==XBG_IMAGE_FORMAT_RGBA32UI);
    assert(plain53==XBG_IMAGE_FORMAT_UNKNOWN);
    assert(plain65==XBG_IMAGE_FORMAT_UNKNOWN);
    // so the strict match does not apply: whatever the guest binds is accepted
    assert(!xbg_typed_storage_requires_match(false,rg32ui,XBG_VK_FORMAT_R32_UINT));
    assert(!xbg_typed_storage_requires_match(false,rgba32ui,XBG_VK_FORMAT_R32G32B32A32_UINT));
    // but an engine rewrite still has to match exactly
    assert(xbg_typed_storage_requires_match(true,XBG_IMAGE_FORMAT_R32UI,XBG_VK_FORMAT_R32_UINT));
    assert(!xbg_typed_storage_requires_match(true,XBG_IMAGE_FORMAT_R32UI,XBG_VK_FORMAT_R32_SINT));
    printf("real module c11e2f23ff063e85: game-declared typed images no longer rejected, engine rewrites still strict PASS\n");
    return 0;
}
