#pragma once
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// A storage image / storage texel buffer that is declared with Image Format
// Unknown (formatless) may only be written when the bound format supports
// VK_FORMAT_FEATURE_2_STORAGE_WRITE_WITHOUT_FORMAT_BIT. Mali-G615 (RG477M)
// has no such format feature, so the guest's formatless write cannot run.
//
// The module may instead declare the image with the SPIR-V image format of the
// format that is actually bound. The written bytes, the texel address stride
// and the number of written components are decided by the bound view's
// VkFormat in both forms (VUID vkCmdDispatch-OpImageWrite-04469, and the
// typed rules 08795/08796), so the rewrite is byte-for-byte equivalent.
//
// The rewrite only changes the Image Format operand of one OpTypeImage: no
// data-flow instruction is added, removed or edited. Two exceptions are
// needed to keep the result valid SPIR-V, and both are proven by the host
// contract in tests/device/test_typed_storage.cpp:
//
//  * A format that SPIR-V only allows with the StorageImageExtendedFormats
//    capability (Rg32f, Rg32i, Rg32ui) needs that capability in the module.
//    Without it the module is invalid, which spirv-val rejects.
//  * When the typed form of the image type is already present in the module,
//    editing the format operand would create a second, identical type
//    declaration, which SPIR-V forbids. The variable then takes the existing
//    type instead.
//
// A third rule widens the coverage without changing any access: an image type
// that several descriptors share may also be typed, but only when every
// sharing descriptor declares the same format. The caller proves that with
// the format map below, so no other descriptor's access changes.

// SPIR-V Image Format operand values. Verified on this host by disassembling
// the real FFRS module with every candidate value (spirv-dis), not from memory.
enum {
    XBG_IMAGE_FORMAT_UNKNOWN=0,
    XBG_IMAGE_FORMAT_RGBA32F=1,
    XBG_IMAGE_FORMAT_R32F=3,
    XBG_IMAGE_FORMAT_RG32F=6,
    XBG_IMAGE_FORMAT_RGBA32I=21,
    XBG_IMAGE_FORMAT_R32I=24,
    XBG_IMAGE_FORMAT_RG32I=25,
    XBG_IMAGE_FORMAT_RGBA32UI=30,
    XBG_IMAGE_FORMAT_R32UI=33,
    XBG_IMAGE_FORMAT_RG32UI=35
};

// Vulkan format numbers (vulkan_core.h). The engine asserts these at compile
// time; they are repeated here so this header stays host-testable.
enum {
    XBG_VK_FORMAT_R32_UINT=98,
    XBG_VK_FORMAT_R32_SINT=99,
    XBG_VK_FORMAT_R32_SFLOAT=100,
    XBG_VK_FORMAT_R32G32_UINT=101,
    XBG_VK_FORMAT_R32G32_SINT=102,
    XBG_VK_FORMAT_R32G32_SFLOAT=103,
    XBG_VK_FORMAT_R32G32B32A32_UINT=107,
    XBG_VK_FORMAT_R32G32B32A32_SINT=108,
    XBG_VK_FORMAT_R32G32B32A32_SFLOAT=109
};

// The SPIR-V capability that the extended storage image formats need.
// Number from the SPIR-V 1.3 capability list; the engine's own capability
// check accepts the same number for StorageImageExtendedFormats.
enum { XBG_SPIRV_CAPABILITY_STORAGE_IMAGE_EXTENDED_FORMATS=49 };

// The SPIR-V image format that matches a bound VkFormat exactly, or Unknown
// when SPIR-V cannot express that format. Unknown keeps the module formatless.
inline uint32_t xbg_spirv_image_format(uint32_t vk_format){
    switch(vk_format){
    case XBG_VK_FORMAT_R32_UINT:return XBG_IMAGE_FORMAT_R32UI;
    case XBG_VK_FORMAT_R32_SINT:return XBG_IMAGE_FORMAT_R32I;
    case XBG_VK_FORMAT_R32_SFLOAT:return XBG_IMAGE_FORMAT_R32F;
    case XBG_VK_FORMAT_R32G32_UINT:return XBG_IMAGE_FORMAT_RG32UI;
    case XBG_VK_FORMAT_R32G32_SINT:return XBG_IMAGE_FORMAT_RG32I;
    case XBG_VK_FORMAT_R32G32_SFLOAT:return XBG_IMAGE_FORMAT_RG32F;
    case XBG_VK_FORMAT_R32G32B32A32_UINT:return XBG_IMAGE_FORMAT_RGBA32UI;
    case XBG_VK_FORMAT_R32G32B32A32_SINT:return XBG_IMAGE_FORMAT_RGBA32I;
    case XBG_VK_FORMAT_R32G32B32A32_SFLOAT:return XBG_IMAGE_FORMAT_RGBA32F;
    default:return XBG_IMAGE_FORMAT_UNKNOWN;
    }
}

// Scalar class a declared image format requires: 0 float, 1 signed, 2 unsigned.
inline uint32_t xbg_spirv_image_format_scalar(uint32_t spirv_format){
    switch(spirv_format){
    case XBG_IMAGE_FORMAT_R32F:case XBG_IMAGE_FORMAT_RG32F:case XBG_IMAGE_FORMAT_RGBA32F:return 0;
    case XBG_IMAGE_FORMAT_R32I:case XBG_IMAGE_FORMAT_RG32I:case XBG_IMAGE_FORMAT_RGBA32I:return 1;
    case XBG_IMAGE_FORMAT_R32UI:case XBG_IMAGE_FORMAT_RG32UI:case XBG_IMAGE_FORMAT_RGBA32UI:return 2;
    default:return 3;
    }
}

// The formats that SPIR-V only allows with StorageImageExtendedFormats.
// Verified on this host: spirv-val --target-env vulkan1.1 rejects every
// rewritten FFRS module that declares Rg32f, Rg32i or Rg32ui without the
// capability, and accepts the R32 and Rgba32 forms without it.
inline bool xbg_spirv_image_format_needs_extended(uint32_t spirv_format){
    return spirv_format==XBG_IMAGE_FORMAT_RG32F
        || spirv_format==XBG_IMAGE_FORMAT_RG32I
        || spirv_format==XBG_IMAGE_FORMAT_RG32UI;
}

// A typed module is only legal when the declared SPIR-V format is exactly the
// format of the bound view (VUID vkCmdDispatch-format-08795/08796).
inline bool xbg_typed_storage_matches(uint32_t spirv_format,uint32_t vk_format){
    return spirv_format!=XBG_IMAGE_FORMAT_UNKNOWN && spirv_format==xbg_spirv_image_format(vk_format);
}

// The image type a descriptor binding uses, with the operand positions the
// rewrite needs. Nothing is modified by this analysis.
struct XbgStorageImageBinding {
    uint32_t variable=0,image_type=0,pointer_type=0,sampled_type=0;
    size_t image_at=0;
    uint32_t declared_format=XBG_IMAGE_FORMAT_UNKNOWN;
    bool found=false;
};

// Finds the OpTypeImage that a descriptor binding's variable points at.
// Only UniformConstant variables (the descriptor storage class) are accepted.
inline bool xbg_spirv_binding_image(const std::vector<uint32_t> &w,uint32_t binding,XbgStorageImageBinding &out){
    if(w.size()<5||w[0]!=0x07230203u)return false;
    std::unordered_map<uint32_t,size_t> def;uint32_t decorated=0;
    for(size_t i=5;i<w.size();){uint32_t n=w[i]>>16,op=w[i]&65535u;
        if(!n||i+n>w.size())return false;
        if(op>=19&&op<=33&&n>=2)def[w[i+1]]=i;
        if(op==59&&n>=4)def[w[i+2]]=i;
        if(op==71&&n==4&&w[i+2]==33&&w[i+3]==binding)decorated=w[i+1];
        i+=n;
    }
    if(!decorated)return false;
    auto var=def.find(decorated);if(var==def.end())return false;
    size_t vi=var->second;if((w[vi]&65535u)!=59||w[vi+3]!=0)return false;
    auto ptr=def.find(w[vi+1]);if(ptr==def.end())return false;
    size_t pi=ptr->second;if((w[pi]&65535u)!=32||w[pi+2]!=0)return false;
    auto img=def.find(w[pi+3]);if(img==def.end())return false;
    size_t ii=img->second;if((w[ii]&65535u)!=25||w[ii+7]!=2)return false; // Sampled=2: storage image
    out.variable=decorated;out.pointer_type=w[vi+1];out.image_type=w[pi+3];out.sampled_type=w[ii+2];
    out.image_at=ii;out.declared_format=w[ii+8];out.found=true;return true;
}

// The SPIR-V image format a binding's module declares, or Unknown when the
// binding is formatless (or absent). The engine uses this as the module's
// actual form, not the package's formatless flag.
inline uint32_t xbg_spirv_declared_image_format(const std::vector<uint32_t> &w,uint32_t binding){
    XbgStorageImageBinding info;
    if(!xbg_spirv_binding_image(w,binding,info))return XBG_IMAGE_FORMAT_UNKNOWN;
    return info.declared_format;
}

// The binding number of a descriptor variable, or UINT32_MAX when the
// variable carries no Binding decoration.
inline uint32_t xbg_spirv_variable_binding(const std::vector<uint32_t> &w,uint32_t variable){
    for(size_t i=5;i<w.size();){
        const uint32_t n=w[i]>>16,op=w[i]&65535u;
        if(!n||i+n>w.size())break;
        if(op==71&&n==4&&w[i+1]==variable&&w[i+2]==33)return w[i+3];
        i+=n;
    }
    return UINT32_MAX;
}

// The image format every descriptor of the module declares after the rewrite,
// keyed by binding number. The engine fills this in with the formats it
// resolved at runtime. It is the proof that a descriptor which shares an
// image type declares the same format, so that no other access changes.
struct XbgStorageFormatMap {
    std::unordered_map<uint32_t,uint32_t> binding_format;
    bool declares(uint32_t binding,uint32_t spirv_format)const{
        auto it=binding_format.find(binding);
        return it!=binding_format.end()&&it->second==spirv_format;
    }
};

// The result id to instruction offset map the shipping code builds.
inline void xbg_spirv_def_map(const std::vector<uint32_t> &w,std::unordered_map<uint32_t,size_t> &def){
    for(size_t i=5;i<w.size();){
        const uint32_t n=w[i]>>16,op=w[i]&65535u;
        if(!n||i+n>w.size())return;
        if(op>=19&&op<=33&&n>=2)def[w[i+1]]=i;
        if(op==59&&n>=4)def[w[i+2]]=i;
        i+=n;
    }
}

// The ids of the values a module loads out of a descriptor variable.
inline std::unordered_set<uint32_t> xbg_spirv_loaded_ids(const std::vector<uint32_t> &w,uint32_t variable){
    std::unordered_set<uint32_t> loaded;
    for(size_t i=5;i<w.size();){
        const uint32_t n=w[i]>>16,op=w[i]&65535u;
        if(!n||i+n>w.size())break;
        if(op==61&&n>=4&&w[i+3]==variable)loaded.insert(w[i+2]);
        i+=n;
    }
    return loaded;
}

// The offsets of the OpLoad instructions that read a descriptor variable.
inline std::vector<size_t> xbg_spirv_variable_loads(const std::vector<uint32_t> &w,uint32_t variable){
    std::vector<size_t> out;
    for(size_t i=5;i<w.size();){
        const uint32_t n=w[i]>>16,op=w[i]&65535u;
        if(!n||i+n>w.size())break;
        if(op==61&&n>=4&&w[i+3]==variable)out.push_back(i);
        i+=n;
    }
    return out;
}

// Does the module read the image behind a descriptor variable?
inline bool xbg_spirv_image_read(const std::vector<uint32_t> &w,uint32_t variable){
    const std::unordered_set<uint32_t> loaded=xbg_spirv_loaded_ids(w,variable);
    if(loaded.empty())return false;
    for(size_t i=5;i<w.size();){
        const uint32_t n=w[i]>>16,op=w[i]&65535u;
        if(!n||i+n>w.size())break;
        if((op==98||op==305)&&n>=4&&loaded.count(w[i+3]))return true;
        i+=n;
    }
    return false;
}

// The descriptor variables that share one OpTypeImage, with their bindings.
struct XbgImageSharer {
    uint32_t variable=0,binding=UINT32_MAX,image_type=0;
    size_t variable_at=0;
};
inline std::vector<XbgImageSharer> xbg_spirv_image_sharers(const std::vector<uint32_t> &w,uint32_t image_type){
    std::unordered_map<uint32_t,size_t> def;xbg_spirv_def_map(w,def);
    std::vector<XbgImageSharer> sharers;
    for(size_t i=5;i<w.size();){
        const uint32_t n=w[i]>>16,op=w[i]&65535u;
        if(!n||i+n>w.size())break;
        if(op==59&&n>=4){
            auto ptr=def.find(w[i+1]);
            if(ptr!=def.end()&&(w[ptr->second]&65535u)==32&&w[ptr->second+3]==image_type){
                XbgImageSharer sharer;
                sharer.variable=w[i+2];sharer.binding=xbg_spirv_variable_binding(w,sharer.variable);
                sharer.image_type=image_type;sharer.variable_at=i;
                sharers.push_back(sharer);
            }
        }
        i+=n;
    }
    return sharers;
}

// The image type in the module that already declares the given format with
// the same sampled type, dim, depth, arrayed and ms operands, or 0 when the
// module has no such type.
inline uint32_t xbg_spirv_typed_image_type(const std::vector<uint32_t> &w,size_t image_at,uint32_t spirv_format){
    for(size_t i=5;i<w.size();){
        const uint32_t n=w[i]>>16,op=w[i]&65535u;
        if(!n||i+n>w.size())break;
        if(op==25&&n>=9&&i!=image_at&&w[i+8]==spirv_format
           &&w[i+3]==w[image_at+3]&&w[i+4]==w[image_at+4]&&w[i+5]==w[image_at+5]
           &&w[i+6]==w[image_at+6]&&w[i+7]==w[image_at+7]&&w[i+2]==w[image_at+2])
            return w[i+1];
        i+=n;
    }
    return 0;
}

// The UniformConstant pointer type that points at an image type and is
// declared before the given offset, or 0 when the module has none. SPIR-V
// requires an id to be defined before it is used, so a variable may only take
// a pointer type that is already declared.
inline uint32_t xbg_spirv_uniform_pointer_type(const std::vector<uint32_t> &w,uint32_t image_type,size_t before){
    uint32_t found=0;size_t found_at=0;
    for(size_t i=5;i<w.size();){
        const uint32_t n=w[i]>>16,op=w[i]&65535u;
        if(!n||i+n>w.size())break;
        if(op==32&&n>=4&&w[i+3]==image_type&&w[i+2]==0&&i<before&&(!found||i<found_at)){found=w[i+1];found_at=i;}
        i+=n;
    }
    return found;
}

// Does the module declare the StorageImageExtendedFormats capability?
inline bool xbg_spirv_has_capability(const std::vector<uint32_t> &w,uint32_t capability){
    for(size_t i=5;i<w.size();){
        const uint32_t n=w[i]>>16,op=w[i]&65535u;
        if(!n||i+n>w.size())break;
        if(op==17&&n>=2&&w[i+1]==capability)return true;
        i+=n;
    }
    return false;
}

// Declares the bound format in the module: the Image Format operand of the
// binding's OpTypeImage becomes spirv_format.
//
//  1 = the module was rewritten
//  0 = the module already declares exactly this format (idempotent, unchanged)
// -1 = refused: malformed module, unknown binding, an image type shared with
//      another descriptor that does not declare the same format, a formatless
//      read on the same image, a sampled type that does not match the format's
//      scalar class, or an extended format the device cannot use. A refused
//      rewrite leaves the module byte-for-byte unchanged.
//
// formats proves which format every other descriptor declares. It is what
// allows an image type that several descriptors share to be typed: every
// sharing descriptor must declare the same format, otherwise its own access
// would change.
//
// allow_extended_formats tells the rewrite that the device supports the
// StorageImageExtendedFormats capability. Without it the extended formats are
// refused, because a module that declares them without the capability is
// invalid SPIR-V.
inline int xbg_typed_storage_rewrite(std::vector<uint32_t> &words,uint32_t binding,uint32_t spirv_format,
                                     const XbgStorageFormatMap &formats,bool allow_extended_formats,
                                     const char **reason=nullptr){
    // The refusal reason is reported with the vocabulary the offline coverage
    // scan uses, so a device run and the scan can be compared line by line.
    const auto refuse=[&](const char *why)->int{if(reason)*reason=why;return -1;};
    const uint32_t scalar=xbg_spirv_image_format_scalar(spirv_format);
    if(scalar>2)return refuse("invalid-target-format");
    if(words.size()<5||words[0]!=0x07230203u)return refuse("malformed-module");
    XbgStorageImageBinding info;
    if(!xbg_spirv_binding_image(words,binding,info))return refuse("no-storage-image-for-binding");
    if(info.declared_format!=XBG_IMAGE_FORMAT_UNKNOWN){
        if(info.declared_format==spirv_format){if(reason)*reason="already-typed";return 0;}
        return refuse("typed-with-other-format");
    }
    // An extended format needs a capability that the device must support.
    if(xbg_spirv_image_format_needs_extended(spirv_format)&&!allow_extended_formats)return refuse("extended-format-not-supported");
    std::unordered_map<uint32_t,size_t> def;xbg_spirv_def_map(words,def);
    // The descriptors that share this image type.
    const std::vector<XbgImageSharer> sharers=xbg_spirv_image_sharers(words,info.image_type);
    // Every sharing descriptor must declare the same format, so that no other
    // descriptor's access changes. A descriptor that the module already typed
    // with this format keeps it; the engine checks at bind time that its
    // bound view has the same format.
    for(const auto &sharer:sharers){
        if(sharer.variable==info.variable)continue;
        uint32_t declared=XBG_IMAGE_FORMAT_UNKNOWN;
        {
            // The sharer may point at another image type with the same format.
            auto ptr=def.find(words[sharer.variable_at+1]);
            if(ptr!=def.end()&&(words[ptr->second]&65535u)==32){
                auto img=def.find(words[ptr->second+3]);
                if(img!=def.end()&&(words[img->second]&65535u)==25)declared=words[img->second+8];
            }
        }
        const bool same=declared==spirv_format||formats.declares(sharer.binding,spirv_format);
        if(!same)return refuse("image-type-shared-with-other-format");
        // A typed image changes read semantics too, so a formatless read on
        // any descriptor of this image keeps the module formatless.
        if(xbg_spirv_image_read(words,sharer.variable))return refuse("formatless-read-on-same-image");
    }
    // A typed image changes read semantics too, so a formatless read on the
    // rewritten descriptor keeps the module formatless.
    if(xbg_spirv_image_read(words,info.variable))return refuse("formatless-read-on-same-image");
    // The sampled type must match the format's scalar class (VUID
    // StandaloneSpirv-Image-04965). The repository's uint storage images may
    // only be declared as an unsigned format.
    auto sample=def.find(info.sampled_type);if(sample==def.end())return refuse("unknown-sampled-type");
    size_t si=sample->second;uint32_t declared_scalar=3;
    if((words[si]&65535u)==22)declared_scalar=0;
    else if((words[si]&65535u)==21&&words[si+3]==0)declared_scalar=2;
    else if((words[si]&65535u)==21&&words[si+3]==1)declared_scalar=1;
    if(declared_scalar!=scalar)return refuse("sampled-type-class-mismatch");
    // The module must not end up with two identical type declarations: SPIR-V
    // forbids a duplicate non-aggregate type. When the typed form already
    // exists, the variable takes that type instead of declaring a second one.
    const uint32_t existing=xbg_spirv_typed_image_type(words,info.image_at,spirv_format);
    if(existing){
        // Only an exclusive image type can move: a shared one would leave
        // the other descriptors on the old type.
        if(sharers.size()!=1)return refuse("duplicate-type-not-reusable");
        const uint32_t pointer=xbg_spirv_uniform_pointer_type(words,existing,sharers.front().variable_at);
        if(!pointer)return refuse("duplicate-type-not-reusable");
        // The variable takes the existing type, and the loads of the variable
        // declare the type they produce.
        const std::vector<size_t> loads=xbg_spirv_variable_loads(words,info.variable);
        words[sharers.front().variable_at+1]=pointer;
        for(size_t load_at:loads)words[load_at+1]=existing;
    }else{
        words[info.image_at+8]=spirv_format;
    }
    // The extended formats are only valid with their capability.
    if(xbg_spirv_image_format_needs_extended(spirv_format)
       &&!xbg_spirv_has_capability(words,XBG_SPIRV_CAPABILITY_STORAGE_IMAGE_EXTENDED_FORMATS)){
        // The capability belongs in the first logical section of the module.
        // It has no result id, so the module's id bound does not change.
        words.insert(words.begin()+5,(2u<<16)|17u);
        words.insert(words.begin()+6,XBG_SPIRV_CAPABILITY_STORAGE_IMAGE_EXTENDED_FORMATS);
    }
    return 1;
}

// The rewrite without a format map: an image type that another descriptor
// shares is refused, because nothing proves that the other descriptor declares
// the same format.
inline int xbg_typed_storage_rewrite(std::vector<uint32_t> &words,uint32_t binding,uint32_t spirv_format,
                                     const char **reason=nullptr){
    return xbg_typed_storage_rewrite(words,binding,spirv_format,XbgStorageFormatMap{},false,reason);
}
