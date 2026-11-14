#include "../../src/graphics/nvn_vulkan/engine/xbg_spirv_typed_storage.h"
#include "../../src/graphics/nvn_vulkan/engine/xbg_spirv_stencil.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
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
static unsigned capability_count(const std::vector<uint32_t> &words,uint32_t capability){
    unsigned count=0;
    for(size_t i=5;i<words.size();){
        const uint32_t n=words[i]>>16,op=words[i]&65535u;
        assert(n && i+n<=words.size());
        if(op==17 && n==2 && words[i+1]==capability)++count;
        i+=n;
    }
    return count;
}
static std::vector<uint32_t> scalar_fixture(const std::vector<uint32_t> &fixture,uint32_t scalar){
    auto words=fixture;
    XbgStorageImageBinding info;assert(xbg_spirv_binding_image(words,11,info));
    for(size_t i=5;i<words.size();){
        const uint32_t n=words[i]>>16,op=words[i]&65535u;
        if(op==21 && words[i+1]==info.sampled_type){
            if(scalar==0){words[i]=(3u<<16)|22u;words.erase(words.begin()+i+3);}
            else if(scalar==1){
                // Keep the coordinate's separate integer type unique too.
                for(size_t j=5;j<words.size();j+=words[j]>>16)
                    if((words[j]&65535u)==21 && words[j+1]!=info.sampled_type && words[j+2]==32 && words[j+3]==1)words[j+3]=0;
                words[i+3]=1;
            }
            return words;
        }
        i+=n;
    }
    assert(false);return {};
}
static void assert_function_unchanged(const std::vector<uint32_t> &before,const std::vector<uint32_t> &after){
    auto first_function=[](const std::vector<uint32_t> &words){
        for(size_t i=5;i<words.size();i+=words[i]>>16)if((words[i]&65535u)==54)return i;
        assert(false);return words.size();
    };
    const size_t a=first_function(before),b=first_function(after);
    // Type references on OpLoad may change, while coordinates and texels keep
    // their original IDs and every other instruction remains identical.
    assert(before.size()-a==after.size()-b);
    for(size_t i=a,j=b;i<before.size();){
        const uint32_t n=before[i]>>16,op=before[i]&65535u;
        for(uint32_t k=0;k<n;++k)if(op!=61 || k!=1)assert(before[i+k]==after[j+k]);
        i+=n;j+=n;
    }
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
    if(argc>=9 && std::string(argv[7])!="--duplicate-types"){
        const std::vector<uint32_t> real=load(argv[7]);
        auto words=real;
        assert(xbg_spirv_ffrs_stencil(words,5,"664748e31a2dfad9")>=1);
        assert(xbg_spirv_declared_image_format(words,11)==XBG_IMAGE_FORMAT_UNKNOWN);
        const std::vector<uint32_t> before=words;
        assert(xbg_typed_storage_rewrite(words,11,XBG_IMAGE_FORMAT_R32UI)==1);
        assert_only_format_changed(before,words,11);
        save(argv[8],words);
    }
    // The RG477M's native R16_UINT view is declared as R16ui, preserving the
    // original two-byte texel stride and the original coordinate/value IDs.
    XbgStorageImageBinding buffer_info;assert(xbg_spirv_binding_image(fixture,11,buffer_info));
    assert(buffer_info.dim==XBG_IMAGE_DIM_BUFFER);
    assert(xbg_spirv_image_scalar_class(fixture,11)==2);       // unsigned
    assert(xbg_spirv_storage_write_components(fixture,11)==4);  // one uint4 texel
    // A 16-bit bound view declares R16ui natively via StorageImageExtendedFormats.
    assert(xbg_spirv_image_format(74)==XBG_IMAGE_FORMAT_R16UI);
    assert(xbg_typed_storage_view_format(fixture,11,74)==74);
    // A bound view SPIR-V can declare is left alone.
    assert(xbg_typed_storage_view_format(fixture,11,98)==98);
    assert(xbg_typed_storage_view_format(fixture,11,107)==107);
    // A module that declares its own format keeps it: no substitution.
    assert(xbg_typed_storage_view_format(game_declared,0,74)==74);
    auto buffer_typed=fixture;
    assert(xbg_typed_storage_rewrite(buffer_typed,11,XBG_IMAGE_FORMAT_R16UI)==1);
    assert(xbg_spirv_declared_image_format(buffer_typed,11)==XBG_IMAGE_FORMAT_R16UI);
    assert(xbg_typed_storage_view_format(buffer_typed,11,74)==74);
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

    // Every supported Rg32 scalar class needs StorageImageExtendedFormats.
    // A device that did not enable it must receive the original module.
    const std::vector<std::vector<uint32_t>> extended_formats={
        {XBG_IMAGE_FORMAT_RG32F,XBG_IMAGE_FORMAT_RG16F,XBG_IMAGE_FORMAT_R16F},
        {XBG_IMAGE_FORMAT_RG32I,XBG_IMAGE_FORMAT_RG16I,XBG_IMAGE_FORMAT_RG8I,XBG_IMAGE_FORMAT_R16I,XBG_IMAGE_FORMAT_R8I},
        {XBG_IMAGE_FORMAT_RG32UI,XBG_IMAGE_FORMAT_RG16UI,XBG_IMAGE_FORMAT_RG8UI,XBG_IMAGE_FORMAT_R16UI,XBG_IMAGE_FORMAT_R8UI}
    };
    for(uint32_t scalar=0;scalar<extended_formats.size();++scalar){
        const auto source=scalar_fixture(fixture,scalar);
        for(uint32_t format:extended_formats[scalar]){
            auto refused=source;
            assert(xbg_typed_storage_rewrite(refused,11,format,false,false)==-1);
            assert(refused==source);
            auto extended=source;
            assert(xbg_typed_storage_rewrite(extended,11,format,false,true)==1);
            assert(capability_count(extended,49)==1);
            assert_function_unchanged(source,extended);
            auto repeated=extended;
            assert(xbg_typed_storage_rewrite(repeated,11,format,false,true)==0);
            assert(repeated==extended);
            save((std::string(argv[4])+".extended-"+std::to_string(format)+".spv").c_str(),extended);
        }
    }
    auto native_refused=fixture;
    assert(xbg_typed_storage_rewrite(native_refused,11,XBG_IMAGE_FORMAT_R16UI,false,false)==-1);
    assert(native_refused==fixture);
    assert(capability_count(buffer_typed,49)==1);

    if(argc==10 && std::string(argv[7])=="--duplicate-types"){
        const auto duplicate=load(argv[8]);
        const std::string prefix=argv[9];
        XbgStorageImageBinding known;assert(xbg_spirv_binding_image(duplicate,13,known));
        auto reused=duplicate;
        assert(xbg_typed_storage_rewrite(reused,11,XBG_IMAGE_FORMAT_R32UI,true)==1);
        XbgStorageImageBinding first,second;
        assert(xbg_spirv_binding_image(reused,11,first));
        assert(xbg_spirv_binding_image(reused,12,second));
        assert(first.image_type==known.image_type && second.image_type==known.image_type);
        assert(first.pointer_type==known.pointer_type && second.pointer_type==known.pointer_type);
        assert(reused.size()==duplicate.size() && reused[3]==duplicate[3]);
        assert_function_unchanged(duplicate,reused);
        save((prefix+".reused.spv").c_str(),reused);

        // One sharer receives the pre-existing R32ui type, the other keeps
        // the original formatless type and its original load type.
        auto private_type=duplicate;
        assert(xbg_typed_storage_split_rewrite(private_type,11,XBG_IMAGE_FORMAT_R32UI)==1);
        assert(xbg_spirv_binding_image(private_type,11,first));
        assert(xbg_spirv_binding_image(private_type,12,second));
        assert(first.image_type==known.image_type && second.declared_format==XBG_IMAGE_FORMAT_UNKNOWN);
        assert(private_type.size()==duplicate.size() && private_type[3]==duplicate[3]);
        assert_function_unchanged(duplicate,private_type);
        save((prefix+".private.spv").c_str(),private_type);

        // The full shared-split path reuses R32ui and privately declares Rg32ui.
        const std::vector<XbgTypedStorageRequest> requests={{11,XBG_IMAGE_FORMAT_RG32UI},{12,XBG_IMAGE_FORMAT_R32UI}};
        size_t failed=0;int shared_result=1;std::vector<uint32_t> split;
        auto split_refused=duplicate;
        assert(xbg_typed_storage_declare(split_refused,requests,failed,shared_result,split,false)==-1);
        assert(split_refused==duplicate && failed==0);
        auto split_typed=duplicate;
        assert(xbg_typed_storage_declare(split_typed,requests,failed,shared_result,split,true)==1);
        assert(xbg_spirv_declared_image_format(split_typed,11)==XBG_IMAGE_FORMAT_RG32UI);
        assert(xbg_spirv_declared_image_format(split_typed,12)==XBG_IMAGE_FORMAT_R32UI);
        assert(capability_count(split_typed,49)==1);
        assert_function_unchanged(duplicate,split_typed);
        save((prefix+".split.spv").c_str(),split_typed);

        auto split_capability_refused=duplicate;
        assert(xbg_typed_storage_split_rewrite(split_capability_refused,11,XBG_IMAGE_FORMAT_RG32UI,false)==-1);
        assert(split_capability_refused==duplicate);

        // A matching declaration after the source variables cannot be used
        // there. Refuse instead of creating a duplicate or a forward type use.
        std::vector<uint32_t> late(duplicate.begin(),duplicate.begin()+5),declarations;
        for(size_t i=5;i<duplicate.size();i+=duplicate[i]>>16){
            const uint32_t n=duplicate[i]>>16,op=duplicate[i]&65535u;
            if((op==25 && duplicate[i+1]==known.image_type) ||
               (op==32 && duplicate[i+1]==known.pointer_type))
                declarations.insert(declarations.end(),duplicate.begin()+i,duplicate.begin()+i+n);
        }
        for(size_t i=5;i<duplicate.size();i+=duplicate[i]>>16){
            const uint32_t n=duplicate[i]>>16,op=duplicate[i]&65535u;
            if((op==25 && duplicate[i+1]==known.image_type) ||
               (op==32 && duplicate[i+1]==known.pointer_type))continue;
            if(op==59 && duplicate[i+2]==known.variable)late.insert(late.end(),declarations.begin(),declarations.end());
            late.insert(late.end(),duplicate.begin()+i,duplicate.begin()+i+n);
        }
        save((prefix+".late-source.spv").c_str(),late);
        const auto late_source=late;
        assert(xbg_typed_storage_rewrite(late,11,XBG_IMAGE_FORMAT_R32UI,true)==-1);
        assert(late==late_source);
        assert(xbg_typed_storage_split_rewrite(late,11,XBG_IMAGE_FORMAT_R32UI)==-1);
        assert(late==late_source);

        // A later failure cannot publish a half-rewritten module.
        auto transaction=duplicate;
        const std::vector<XbgTypedStorageRequest> bad_requests={{11,XBG_IMAGE_FORMAT_R32UI},{12,XBG_IMAGE_FORMAT_R32I}};
        assert(xbg_typed_storage_declare(transaction,bad_requests,failed,shared_result,split)==-1);
        assert(transaction==duplicate);
    }

    save(argv[4],typed);
    std::cout<<"Typed storage texel buffer: formatless to R32ui, idempotent, shared image and formatless read refused, bound format mismatch red, native R16ui and extended-format capability gates preserve texel layout PASS\n";
    return 0;
}
