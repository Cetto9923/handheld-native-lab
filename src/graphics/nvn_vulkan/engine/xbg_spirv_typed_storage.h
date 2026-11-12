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
// Only the Image Format operand of one OpTypeImage changes here: no data-flow
// instruction is added, removed or edited.

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
    out.variable=decorated;out.pointer_type=w[vi+1];out.image_type=w[ii+1];out.sampled_type=w[ii+2];
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

// Declares the bound format in the module: the Image Format operand of the
// binding's OpTypeImage becomes spirv_format. Nothing else changes.
//
//  1 = the module was rewritten
//  0 = the module already declares exactly this format (idempotent, unchanged)
// -1 = refused: malformed module, unknown binding, an image type shared with
//      another variable, a formatless read on the same image, or a sampled
//      type that does not match the format's scalar class. A refused rewrite
//      leaves the module byte-for-byte unchanged.
inline int xbg_typed_storage_rewrite(std::vector<uint32_t> &words,uint32_t binding,uint32_t spirv_format){
    const uint32_t scalar=xbg_spirv_image_format_scalar(spirv_format);
    if(scalar>2)return -1;
    if(words.size()<5||words[0]!=0x07230203u)return -1;
    XbgStorageImageBinding info;
    if(!xbg_spirv_binding_image(words,binding,info))return -1;
    if(info.declared_format!=XBG_IMAGE_FORMAT_UNKNOWN)return info.declared_format==spirv_format?0:-1;
    std::unordered_map<uint32_t,size_t> def;
    for(size_t i=5;i<words.size();){uint32_t n=words[i]>>16,op=words[i]&65535u;
        if(op>=19&&op<=33&&n>=2)def[words[i+1]]=i;
        if(op==59&&n>=4)def[words[i+2]]=i;
        i+=n;
    }
    // The image type must belong to this binding alone: a shared type would
    // change another descriptor's access as well.
    uint32_t users=0;
    for(size_t i=5;i<words.size();){uint32_t n=words[i]>>16,op=words[i]&65535u;
        if(op==59&&n>=4){auto ptr=def.find(words[i+1]);
            if(ptr!=def.end()&&(words[ptr->second]&65535u)==32&&words[ptr->second+3]==info.image_type)++users;}
        i+=n;
    }
    if(users!=1)return -1;
    // The sampled type must match the format's scalar class (VUID
    // StandaloneSpirv-Image-04965). The repository's uint storage images may
    // only be declared as an unsigned format.
    auto sample=def.find(info.sampled_type);if(sample==def.end())return -1;
    size_t si=sample->second;uint32_t declared_scalar=3;
    if((words[si]&65535u)==22)declared_scalar=0;
    else if((words[si]&65535u)==21&&words[si+3]==0)declared_scalar=2;
    else if((words[si]&65535u)==21&&words[si+3]==1)declared_scalar=1;
    if(declared_scalar!=scalar)return -1;
    // A typed image changes read semantics too, so a formatless read on this
    // image keeps the module formatless.
    std::unordered_set<uint32_t> loaded;
    for(size_t i=5;i<words.size();){uint32_t n=words[i]>>16,op=words[i]&65535u;
        if(op==61&&n>=4&&words[i+3]==info.variable)loaded.insert(words[i+2]);
        i+=n;
    }
    for(size_t i=5;i<words.size();){uint32_t n=words[i]>>16,op=words[i]&65535u;
        if((op==98||op==305)&&n>=4&&loaded.count(words[i+3]))return -1;
        i+=n;
    }
    words[info.image_at+8]=spirv_format;
    return 1;
}
