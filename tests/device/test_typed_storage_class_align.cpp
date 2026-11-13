#include "../../src/graphics/nvn_vulkan/engine/xbg_spirv_typed_storage.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <vector>

// The real FFRS battle compute module d201be268f66c813, the second module the
// RG477M (Mali-G615) aborted on with
//   FATAL: Texel format lacks formatless-write support
// Its bindings 35 and 41 share one formatless storage image type whose
// sampled type is %float, while the guest binds an R32_SINT view for binding
// 41. The engine used to keep the guest's view format, so the rewrite tried
// to declare R32i on a float-sampled image. That violates VUID
// StandaloneSpirv-Image-04965, so the rewrite refused, the module stayed
// formatless and the device aborted.
//
// The guest's own metadata and the guest's own shader disagree about the
// numeric class of that buffer. The reference platform resolves that in the
// shader's favour, because a formatless access follows the shader's type, so
// the view takes the module's class with the guest's component count. The
// texel size and the texel layout stay the same, so the written bytes do not
// change.
//
// usage: test-typed-storage-class-align <module.spv> [rewritten.spv]

static std::vector<uint32_t> load(const char *p){
    std::ifstream in(p,std::ios::binary|std::ios::ate);assert(in.good());
    auto b=in.tellg();assert(b>=20&&b%4==0);in.seekg(0);
    std::vector<uint32_t> w(size_t(b)/4);in.read(reinterpret_cast<char*>(w.data()),b);assert(in.good());
    return w;
}
int main(int argc,char**argv){
    assert(argc>=2);
    auto words=load(argv[1]);
    // The two bindings of the group, on one formatless storage image type.
    XbgStorageImageBinding first,second;
    assert(xbg_spirv_binding_image(words,35,first));
    assert(xbg_spirv_binding_image(words,41,second));
    assert(first.image_type==second.image_type);
    assert(first.declared_format==XBG_IMAGE_FORMAT_UNKNOWN);
    assert(second.declared_format==XBG_IMAGE_FORMAT_UNKNOWN);
    assert(xbg_spirv_image_scalar_class(words,35)==0);   // float
    assert(xbg_spirv_image_scalar_class(words,41)==0);
    assert(xbg_spirv_storage_write_components(words,41)==4);
    assert(xbg_spirv_storage_min_write_components(words,41)==4);
    // The guest's view for binding 41 is R32_SINT: an integer class against
    // the module's float sampled type. The view follows the module's class
    // and keeps the guest's component count, so the texel size is unchanged.
    const uint32_t aligned=xbg_typed_storage_view_format(words,41,99);
    assert(aligned==XBG_VK_FORMAT_R32_SFLOAT);
    assert(aligned!=XBG_VK_FORMAT_R32_SINT);       // mutant: the old rule kept the guest's integer class
    assert(xbg_vk_format_components(99)==xbg_vk_format_components(aligned));
    // Both bindings of the group resolve to the same view, so the shared image
    // type can declare one format for them and no split is needed.
    assert(xbg_typed_storage_view_format(words,35,99)==XBG_VK_FORMAT_R32_SFLOAT);
    // A class that already agrees is left alone.
    assert(xbg_typed_storage_view_format(words,41,100)==XBG_VK_FORMAT_R32_SFLOAT);
    assert(xbg_typed_storage_view_format(words,41,109)==XBG_VK_FORMAT_R32G32B32A32_SFLOAT);
    // The guest's integer declaration is the illegal one: the rewrite refuses
    // it, which is what made the RG477M abort.
    auto probe=words;
    assert(xbg_typed_storage_rewrite(probe,41,XBG_IMAGE_FORMAT_R32I,true)==-1);
    // The aligned declaration is accepted on the shared type.
    std::vector<XbgTypedStorageRequest> requests={{35,XBG_IMAGE_FORMAT_R32F},{41,XBG_IMAGE_FORMAT_R32F}};
    size_t failed=0;int shared=0;std::vector<uint32_t> split;
    assert(xbg_typed_storage_declare(words,requests,failed,shared,split)==1);
    assert(failed==0);   // no request failed
    assert(shared==1);   // the shared type could carry the format
    assert(split.empty());
    assert(xbg_spirv_declared_image_format(words,35)==XBG_IMAGE_FORMAT_R32F);
    assert(xbg_spirv_declared_image_format(words,41)==XBG_IMAGE_FORMAT_R32F);
    // After the rewrite the module declares the format itself, and the
    // descriptor path has to resolve the same view as the rewrite did.
    assert(xbg_typed_storage_view_format(words,41,99)==XBG_VK_FORMAT_R32_SFLOAT);
    assert(xbg_typed_storage_view_format(words,35,99)==XBG_VK_FORMAT_R32_SFLOAT);
    if(argc>=3){
        std::ofstream out(argv[2],std::ios::binary);
        out.write(reinterpret_cast<const char*>(words.data()),std::streamsize(words.size()*4));
        assert(bool(out));
    }
    printf("real module d201be268f66c813 bindings 35/41: the guest R32_SINT view follows the module's float class PASS\n");
    return 0;
}
