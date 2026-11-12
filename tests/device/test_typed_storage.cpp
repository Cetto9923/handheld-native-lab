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
// Format operand changes, an already typed module is left alone, an image
// type that two descriptors share becomes typed when both declare the same
// format, an extended format becomes typed with the capability it needs, and
// a module that already declares the typed form reuses that type instead of
// declaring it twice.
// Red: a shared image type whose other descriptor declares another format, a
// formatless read on the same image, an extended format without the device
// feature and a module whose declared format disagrees with the bound
// VkFormat are all refused with the module unchanged.

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
// How many image types of the module declare the given format.
static uint32_t count_typed_images(const std::vector<uint32_t> &words,uint32_t spirv_format){
    uint32_t count=0;
    for(size_t i=5;i<words.size();){uint32_t n=words[i]>>16,op=words[i]&0xffffu;if(!n||i+n>words.size())break;if(op==25&&n>=9&&words[i+8]==spirv_format)++count;i+=n;}
    return count;
}
int main(int argc,char **argv){
    assert(argc>=5);
    // argv: uint fixture, shared fixture, read fixture, typed output,
    //       duplicate fixture, shared output, extended output, duplicate
    //       output, real module, real output.
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

    // Green: an image type that two descriptors share may be typed when both
    // descriptors declare the same format. Only the Image Format operand of
    // the shared type changes.
    {
        XbgStorageFormatMap formats;
        formats.binding_format[11]=XBG_IMAGE_FORMAT_R32UI;
        formats.binding_format[12]=XBG_IMAGE_FORMAT_R32UI;
        auto typed_shared=shared;
        assert(xbg_typed_storage_rewrite(typed_shared,11,XBG_IMAGE_FORMAT_R32UI,formats,false)==1);
        assert(xbg_spirv_declared_image_format(typed_shared,11)==XBG_IMAGE_FORMAT_R32UI);
        assert(xbg_spirv_declared_image_format(typed_shared,12)==XBG_IMAGE_FORMAT_R32UI);
        assert_only_format_changed(shared,typed_shared,11);
        if(argc>=7)save(argv[6],typed_shared);
    }
    // Negative control: the other descriptor of the shared type declares a
    // different format, so typing the type would change that descriptor.
    {
        XbgStorageFormatMap formats;
        formats.binding_format[11]=XBG_IMAGE_FORMAT_R32UI;
        formats.binding_format[12]=XBG_IMAGE_FORMAT_R32I;
        auto refused=shared;
        assert(xbg_typed_storage_rewrite(refused,11,XBG_IMAGE_FORMAT_R32UI,formats,false)==-1);
        assert(refused==shared);
        // Without the map nothing proves the other descriptor's format.
        auto unproven=shared;
        assert(xbg_typed_storage_rewrite(unproven,11,XBG_IMAGE_FORMAT_R32UI)==-1);
        assert(unproven==shared);
    }
    // Negative control: an extended format needs the capability, so the
    // rewrite refuses it when the device does not support it.
    {
        auto refused=fixture;
        assert(xbg_typed_storage_rewrite(refused,11,XBG_IMAGE_FORMAT_RG32UI,XbgStorageFormatMap{},false)==-1);
        assert(refused==fixture);
    }
    // Green: with the capability the module declares it, and the module stays
    // valid SPIR-V for Vulkan 1.1.
    {
        auto extended=fixture;
        assert(xbg_typed_storage_rewrite(extended,11,XBG_IMAGE_FORMAT_RG32UI,XbgStorageFormatMap{},true)==1);
        assert(xbg_spirv_declared_image_format(extended,11)==XBG_IMAGE_FORMAT_RG32UI);
        assert(xbg_spirv_has_capability(extended,XBG_SPIRV_CAPABILITY_STORAGE_IMAGE_EXTENDED_FORMATS));
        if(argc>=8)save(argv[7],extended);
    }
    // The module that already declares the typed form must not end up with
    // two identical types: the variable takes the existing type instead.
    if(argc>=6){
        const std::vector<uint32_t> duplicate=load(argv[5]);
        auto reused=duplicate;
        assert(xbg_typed_storage_rewrite(reused,11,XBG_IMAGE_FORMAT_R32UI)==1);
        assert(xbg_spirv_declared_image_format(reused,11)==XBG_IMAGE_FORMAT_R32UI);
        assert(count_typed_images(reused,XBG_IMAGE_FORMAT_R32UI)==1);
        if(argc>=9)save(argv[8],reused);
    }
    save(argv[4],typed);

    // Golden: the real FFRS module (when the local device dump is present) goes
    // through the engine's integer storage rewrite and then the typed rewrite.
    if(argc>=11){
        const std::vector<uint32_t> real=load(argv[9]);
        auto words=real;
        assert(xbg_spirv_ffrs_stencil(words,5,"664748e31a2dfad9")>=1);
        assert(xbg_spirv_declared_image_format(words,11)==XBG_IMAGE_FORMAT_UNKNOWN);
        const std::vector<uint32_t> before=words;
        assert(xbg_typed_storage_rewrite(words,11,XBG_IMAGE_FORMAT_R32UI)==1);
        assert_only_format_changed(before,words,11);
        save(argv[10],words);
    }
    std::cout<<"Typed storage texel buffer: formatless to R32ui, idempotent, shared image and formatless read refused, bound format mismatch red PASS\n";
    std::cout<<"Shared image type typed with the same format green, different format red, extended format refused without the capability, duplicate type reuses the existing type PASS\n";
    return 0;
}
