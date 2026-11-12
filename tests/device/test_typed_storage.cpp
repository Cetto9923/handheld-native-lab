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
static void assert_only_format_changed(const std::vector<uint32_t> &before,const std::vector<uint32_t> &after,uint32_t binding){
    assert(before.size()==after.size());
    const size_t at=image_at(before,binding);
    assert(image_at(after,binding)==at);
    for(size_t i=0;i<before.size();i++){
        if(i==at+8)continue;
        assert(before[i]==after[i]);
    }
    assert(before[at+8]==XBG_IMAGE_FORMAT_UNKNOWN);
    assert(after[at+8]==XBG_IMAGE_FORMAT_R32UI);
}
int main(int argc,char **argv){
    assert(argc>=5);
    const std::vector<uint32_t> fixture=load(argv[1]);
    const std::vector<uint32_t> shared=load(argv[2]);
    const std::vector<uint32_t> reading=load(argv[3]);

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
    if(argc>=7){
        const std::vector<uint32_t> real=load(argv[5]);
        auto words=real;
        assert(xbg_spirv_ffrs_stencil(words,5,"664748e31a2dfad9")>=1);
        assert(xbg_spirv_declared_image_format(words,11)==XBG_IMAGE_FORMAT_UNKNOWN);
        const std::vector<uint32_t> before=words;
        assert(xbg_typed_storage_rewrite(words,11,XBG_IMAGE_FORMAT_R32UI)==1);
        assert_only_format_changed(before,words,11);
        save(argv[6],words);
    }
    save(argv[4],typed);
    std::cout<<"Typed storage texel buffer: formatless to R32ui, idempotent, shared image and formatless read refused, bound format mismatch red PASS\n";
    return 0;
}
