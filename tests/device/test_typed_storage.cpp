#include "../../src/graphics/nvn_vulkan/engine/xbg_spirv_typed_storage.h"
#include "../../src/graphics/nvn_vulkan/engine/xbg_spirv_stencil.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <vector>

// Host contract for the typed storage texel buffer rewrite.
//
// Green: a formatless storage texel buffer becomes a typed one, only the Image
// Format operand changes, an already typed module is left alone, and every
// unsupported shape is refused with the module unchanged.
// Red: a module whose declared format disagrees with the bound VkFormat is
// rejected by the match check, so the engine's requirement fails.

static std::vector<uint32_t> load(const char *path){
    std::ifstream in(path,std::ios::binary|std::ios::ate);assert(in.good());
    auto bytes=in.tellg();assert(bytes>=20&&bytes%4==0);in.seekg(0);
    std::vector<uint32_t> words(size_t(bytes)/4);in.read(reinterpret_cast<char *>(words.data()),bytes);assert(in.good());
    return words;
}
static void save(const char *path,const std::vector<uint32_t> &words){
    std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char *>(words.data()),words.size()*4);assert(out.good());
}
static size_t image_at(const std::vector<uint32_t> &words,uint32_t binding){
    XbgStorageImageBinding info;assert(xbg_spirv_binding_image(words,binding,info));return info.image_at;
}
// Every word except the Image Format operand of the binding's image type must
// stay exactly as it was: the rewrite must not touch the data flow.
static void assert_only_format_changed(const std::vector<uint32_t> &before,const std::vector<uint32_t> &after,uint32_t binding,uint32_t expected=XBG_IMAGE_FORMAT_R32UI){
    assert(before.size()==after.size());
    const size_t at=image_at(before,binding);
    assert(image_at(after,binding)==at);
    for(size_t i=0;i<before.size();i++){
        if(i==at+8)continue;
        assert(before[i]==after[i]);
    }
    assert(before[at+8]==XBG_IMAGE_FORMAT_UNKNOWN);
    assert(after[at+8]==expected);
}
int main(int argc,char **argv){
    assert(argc>=6);
    const std::vector<uint32_t> fixture=load(argv[1]);
    const std::vector<uint32_t> shared=load(argv[2]);
    const std::vector<uint32_t> reading=load(argv[3]);
    const std::vector<uint32_t> game_declared=load(argv[5]);

    // The fixture is formatless: the module's own form, not the package flag.
    assert(xbg_spirv_declared_image_format(fixture,11)==XBG_IMAGE_FORMAT_UNKNOWN);
    assert(xbg_spirv_declared_image_format(fixture,12)==XBG_IMAGE_FORMAT_UNKNOWN);

    // Green: Unknown becomes the SPIR-V format of the bound R32_UINT view.
    auto typed=fixture;
    assert(xbg_typed_storage_rewrite(typed,11,XBG_IMAGE_FORMAT_R32UI)==1);
    assert(xbg_spirv_declared_image_format(typed,11)==XBG_IMAGE_FORMAT_R32UI);
    assert_only_format_changed(fixture,typed,11);

    // Idempotent: an already typed module is not rewritten again.
    auto again=typed;
    assert(xbg_typed_storage_rewrite(again,11,XBG_IMAGE_FORMAT_R32UI)==0);
    assert(again==typed);
    // A typed module may not be re-declared with a different format.
    auto conflicting=typed;
    assert(xbg_typed_storage_rewrite(conflicting,11,XBG_IMAGE_FORMAT_R32I)==-1);
    assert(conflicting==typed);

    // Refused shapes leave the module byte-for-byte unchanged.
    auto shared_copy=shared;
    assert(xbg_typed_storage_rewrite(shared_copy,11,XBG_IMAGE_FORMAT_R32UI)==-1);
    assert(shared_copy==shared);
    // A shared image type declares one format for every binding that uses it,
    // so the module may only declare it when all of them need the same one.
    // The engine finds the other users of the type and checks that.
    std::vector<uint32_t> sharers;
    xbg_spirv_shared_image_bindings(shared,11,sharers);
    assert((sharers==std::vector<uint32_t>{11,12}));
    xbg_spirv_shared_image_bindings(shared,12,sharers);
    assert((sharers==std::vector<uint32_t>{11,12}));
    // The caller has checked that both users need R32ui: then the shared type
    // may carry it.
    auto shared_typed=shared;
    assert(xbg_typed_storage_rewrite(shared_typed,11,XBG_IMAGE_FORMAT_R32UI,true)==1);
    assert(xbg_spirv_declared_image_format(shared_typed,11)==XBG_IMAGE_FORMAT_R32UI);
    assert(xbg_spirv_declared_image_format(shared_typed,12)==XBG_IMAGE_FORMAT_R32UI);
    // Without that check the shared type stays formatless: the guardrail is
    // still the default.
    auto shared_again=shared;
    assert(xbg_typed_storage_rewrite(shared_again,11,XBG_IMAGE_FORMAT_R32UI)==-1);
    assert(shared_again==shared);
    auto reading_copy=reading;
    assert(xbg_typed_storage_rewrite(reading_copy,11,XBG_IMAGE_FORMAT_R32UI)==-1);
    assert(reading_copy==reading);
    auto wrong_scalar=fixture;
    assert(xbg_typed_storage_rewrite(wrong_scalar,11,XBG_IMAGE_FORMAT_R32I)==-1);
    assert(wrong_scalar==fixture);
    auto unknown_binding=fixture;
    assert(xbg_typed_storage_rewrite(unknown_binding,12,XBG_IMAGE_FORMAT_R32UI)==-1);
    assert(unknown_binding==fixture);
    auto unknown_format=fixture;
    assert(xbg_typed_storage_rewrite(unknown_format,11,XBG_IMAGE_FORMAT_UNKNOWN)==-1);
    assert(unknown_format==fixture);
    auto malformed=fixture;malformed[0]=0;
    assert(xbg_typed_storage_rewrite(malformed,11,XBG_IMAGE_FORMAT_R32UI)==-1);
    assert(malformed[0]==0);

    // The bound format decides the module: exact match required.
    assert(xbg_typed_storage_matches(XBG_IMAGE_FORMAT_R32UI,XBG_VK_FORMAT_R32_UINT));
    assert(!xbg_typed_storage_matches(XBG_IMAGE_FORMAT_R32UI,XBG_VK_FORMAT_R32_SINT));
    assert(!xbg_typed_storage_matches(XBG_IMAGE_FORMAT_R32I,XBG_VK_FORMAT_R32_UINT));
    assert(!xbg_typed_storage_matches(XBG_IMAGE_FORMAT_UNKNOWN,XBG_VK_FORMAT_R32_UINT));
    // Red mutant: the module declares R32i while the bound view is R32_UINT.
    auto mutant=typed;
    mutant[image_at(mutant,11)+8]=XBG_IMAGE_FORMAT_R32I;
    assert(!xbg_typed_storage_matches(XBG_IMAGE_FORMAT_R32I,XBG_VK_FORMAT_R32_UINT));

    // The format table only maps formats SPIR-V can express for a storage view.
    assert(xbg_spirv_image_format(XBG_VK_FORMAT_R32_UINT)==XBG_IMAGE_FORMAT_R32UI);
    assert(xbg_spirv_image_format(XBG_VK_FORMAT_R32G32_UINT)==XBG_IMAGE_FORMAT_RG32UI);
    assert(xbg_spirv_image_format(XBG_VK_FORMAT_R32G32B32A32_UINT)==XBG_IMAGE_FORMAT_RGBA32UI);
    assert(xbg_spirv_image_format(12345)==XBG_IMAGE_FORMAT_UNKNOWN);

    // Golden: the real FFRS module (when the local device dump is present) goes
    // through the engine's integer storage rewrite and then the typed rewrite.
    if(argc>=8){
        const std::vector<uint32_t> real=load(argv[7]);
        auto words=real;
        assert(xbg_spirv_ffrs_stencil(words,5,"664748e31a2dfad9")>=1);
        assert(xbg_spirv_declared_image_format(words,11)==XBG_IMAGE_FORMAT_UNKNOWN);
        const std::vector<uint32_t> before=words;
        assert(xbg_typed_storage_rewrite(words,11,XBG_IMAGE_FORMAT_R32UI)==1);
        assert_only_format_changed(before,words,11);
        save(argv[8],words);
    }
    // The RG477M (Mali-G615) crash of 1.0.9: "FATAL: Texel format lacks
    // formatless-write support". The binding is a formatless storage Buffer
    // image and the guest binds a 16-bit view (R16_UINT). SPIR-V's Image
    // Format operand has no 16-bit format at all, so the module cannot
    // declare the bound format, the typed-storage rewrite cannot rescue the
    // binding, and the module stays formatless: on a device without
    // VK_FORMAT_FEATURE_2_STORAGE_WRITE_WITHOUT_FORMAT that write is fatal.
    // The engine binds the format the module itself describes instead:
    // 32-bit unsigned RGBA, which SPIR-V declares as Rgba32ui.
    XbgStorageImageBinding buffer_info;assert(xbg_spirv_binding_image(fixture,11,buffer_info));
    assert(buffer_info.dim==XBG_IMAGE_DIM_BUFFER);
    assert(xbg_spirv_image_scalar_class(fixture,11)==2);       // unsigned
    assert(xbg_spirv_storage_write_components(fixture,11)==4);  // one uint4 texel
    // A 16-bit bound view has no SPIR-V image format.
    assert(xbg_spirv_image_format(74)==XBG_IMAGE_FORMAT_UNKNOWN);
    assert(xbg_typed_storage_view_format(fixture,11,74)==XBG_VK_FORMAT_R32G32B32A32_UINT);
    // Red mutant: the old rule kept the guest's 16-bit view, the module stayed
    // formatless and the device aborted. That must not come back.
    assert(xbg_typed_storage_view_format(fixture,11,74)!=74);
    // A bound view SPIR-V can declare is left alone.
    assert(xbg_typed_storage_view_format(fixture,11,98)==98);
    assert(xbg_typed_storage_view_format(fixture,11,107)==107);
    // A module that declares its own format keeps it: no substitution.
    assert(xbg_typed_storage_view_format(game_declared,0,74)==74);
    // The substituted format is one the rewrite can declare, and only the
    // Image Format operand changes.
    auto buffer_typed=fixture;
    assert(xbg_typed_storage_rewrite(buffer_typed,11,XBG_IMAGE_FORMAT_RGBA32UI)==1);
    assert(xbg_spirv_declared_image_format(buffer_typed,11)==XBG_IMAGE_FORMAT_RGBA32UI);
    assert_only_format_changed(fixture,buffer_typed,11,XBG_IMAGE_FORMAT_RGBA32UI);
    // The substitution is idempotent: on the module the engine rewrote, the
    // bound format has to stay the substituted one, or the descriptor path
    // would bind the guest's 16-bit view and reject the engine's own module.
    assert(xbg_typed_storage_view_format(buffer_typed,11,74)==XBG_VK_FORMAT_R32G32B32A32_UINT);
    save(argv[6],buffer_typed);
    // The battle-settle crash of 1.0.9 on the Honor tablet: the module
    // declares a typed storage image itself (the real module
    // c11e2f23ff063e85 declares a 3D Rg32ui storage image), and the guest
    // binds a view whose format comes from the guest's own NVN format. The
    // engine must not reject that pair: only the bindings the engine rewrote
    // itself carry the strict match requirement.
    assert(xbg_spirv_declared_image_format(game_declared,0)==XBG_IMAGE_FORMAT_RG32UI);
    assert(xbg_spirv_declared_image_format(game_declared,1)==XBG_IMAGE_FORMAT_UNKNOWN);
    // The game's own declaration: not rewritten, so no strict match, whatever
    // the bound view format is. This is the case that used to abort.
    assert(!xbg_typed_storage_requires_match(false,XBG_IMAGE_FORMAT_RG32UI,XBG_VK_FORMAT_R32_UINT));
    assert(!xbg_typed_storage_requires_match(false,XBG_IMAGE_FORMAT_RG32UI,XBG_VK_FORMAT_R32G32_UINT));
    // The engine's own rewrite: the declared format must equal the bound
    // view's format, exactly as before.
    assert(xbg_typed_storage_requires_match(true,XBG_IMAGE_FORMAT_R32UI,XBG_VK_FORMAT_R32_UINT));
    assert(!xbg_typed_storage_requires_match(true,XBG_IMAGE_FORMAT_R32UI,XBG_VK_FORMAT_R32_SINT));
    assert(!xbg_typed_storage_requires_match(true,XBG_IMAGE_FORMAT_R32I,XBG_VK_FORMAT_R32_UINT));
    // A rewritten binding whose declared format is Unknown cannot match either.
    assert(!xbg_typed_storage_requires_match(true,XBG_IMAGE_FORMAT_UNKNOWN,XBG_VK_FORMAT_R32_UINT));

    save(argv[4],typed);
    std::cout<<"Typed storage texel buffer: formatless to R32ui, idempotent, shared image and formatless read refused, bound format mismatch red, a bound view SPIR-V cannot declare is replaced by the module's own 32-bit form PASS\n";
    return 0;
}
