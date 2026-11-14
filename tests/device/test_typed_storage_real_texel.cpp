#include "../../src/graphics/nvn_vulkan/engine/xbg_spirv_typed_storage.h"
#include "../../src/graphics/nvn_vulkan/engine/xbg_spirv_stencil.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <vector>

// The real FFRS battle module 3b6302a58b86a34a, which aborted the RG477M
// (Mali-G615) 22 frames after the title with
//   FATAL: Texel format lacks formatless-write support
// Binding 47 is a formatless storage Buffer image. The guest binds a 16-bit
// view (R16_UINT) and SPIR-V's Image Format operand has no 16-bit format, so
// the module cannot declare the bound format and the typed-storage rewrite
// cannot rescue the binding. The engine binds the format the module itself
// describes: 32-bit unsigned RGBA, which SPIR-V declares as Rgba32ui.
static std::vector<uint32_t> load(const char *p){
    std::ifstream in(p,std::ios::binary|std::ios::ate);assert(in.good());
    auto b=in.tellg();assert(b>=20&&b%4==0);in.seekg(0);
    std::vector<uint32_t> w(size_t(b)/4);in.read(reinterpret_cast<char*>(w.data()),b);assert(in.good());
    return w;
}
int main(int argc,char**argv){
    assert(argc>=2);
    auto words=load(argv[1]);
    // The module's own form: a formatless Buffer image at binding 47.
    assert(xbg_spirv_declared_image_format(words,47)==XBG_IMAGE_FORMAT_UNKNOWN);
    XbgStorageImageBinding info;assert(xbg_spirv_binding_image(words,47,info));
    assert(info.dim==XBG_IMAGE_DIM_BUFFER);
    // The FFRS integer facts turn the write into a raw uint4.
    assert(xbg_spirv_ffrs_stencil(words,5,"3b6302a58b86a34a")>=1);
    assert(xbg_spirv_image_scalar_class(words,47)==2);       // unsigned
    assert(xbg_spirv_storage_write_components(words,47)==4);  // one uint4 texel
    // The guest's 16-bit view has no SPIR-V image format: that is why the
    // module stayed formatless and the device aborted.
    assert(xbg_spirv_image_format(74)==XBG_IMAGE_FORMAT_UNKNOWN);
    // The engine substitutes the module's own 32-bit RGBA form.
    const uint32_t substitute=xbg_typed_storage_view_format(words,47,74);
    assert(substitute==XBG_VK_FORMAT_R32G32B32A32_UINT);
    // Red mutant: the old rule kept the guest's 16-bit view.
    assert(substitute!=74);
    // The rewrite declares it, and the strict match then holds.
    assert(xbg_typed_storage_rewrite(words,47,xbg_spirv_image_format(substitute))==1);
    assert(xbg_spirv_declared_image_format(words,47)==XBG_IMAGE_FORMAT_RGBA32UI);
    assert(xbg_typed_storage_requires_match(true,XBG_IMAGE_FORMAT_RGBA32UI,XBG_VK_FORMAT_R32G32B32A32_UINT));
    // A bound view SPIR-V can declare is left alone.
    assert(xbg_typed_storage_view_format(words,47,98)==98);
    printf("real module 3b6302a58b86a34a binding 47: the 16-bit view is replaced by the module's own 32-bit RGBA form PASS\n");
    // The second module the RG477M reached after that one: 9bee7f09d6c655c3.
    // Its bindings 29 and 35 share one formatless storage Buffer image type,
    // so the typed-storage rewrite refused it and the module stayed
    // formatless. A shared type may only be declared when every binding that
    // uses it needs the same format. The module that needs the type split
    // (ba0d8842fa53e521) is checked by the split contract; the group is
    // found in the module itself, so both of them run here.
    if(argc>=3){
        auto shared_module=load(argv[2]);
        const int stencil=xbg_spirv_ffrs_stencil(shared_module,5,"9bee7f09d6c655c3");
        std::vector<uint32_t> group;
        assert(xbg_spirv_shared_image_group(shared_module,group));
        assert(group.size()==2);
        if(stencil>=1)assert((group==std::vector<uint32_t>{29,35}));
        // The format of the group's own numeric class.
        const uint32_t klass=xbg_spirv_image_scalar_class(shared_module,group[0]);
        assert(klass<3);
        const uint32_t format=klass==0?XBG_IMAGE_FORMAT_R32F:klass==1?XBG_IMAGE_FORMAT_R32I:XBG_IMAGE_FORMAT_R32UI;
        assert(xbg_spirv_declared_image_format(shared_module,group[0])==XBG_IMAGE_FORMAT_UNKNOWN);
        assert(xbg_spirv_declared_image_format(shared_module,group[1])==XBG_IMAGE_FORMAT_UNKNOWN);
        // Without the shared check the rewrite is refused: the guardrail.
        auto refused=shared_module;
        assert(xbg_typed_storage_rewrite(refused,group[0],format,false)==-1);
        assert(refused==shared_module);
        // With it, the shared type carries the format for both bindings.
        auto typed=shared_module;
        assert(xbg_typed_storage_rewrite(typed,group[0],format,true)==1);
        assert(xbg_spirv_declared_image_format(typed,group[0])==format);
        assert(xbg_spirv_declared_image_format(typed,group[1])==format);
        printf("real module with the shared image type of bindings %u and %u: the shared type may be declared when both need it PASS\n",group[0],group[1]);
    }
    return 0;
}
