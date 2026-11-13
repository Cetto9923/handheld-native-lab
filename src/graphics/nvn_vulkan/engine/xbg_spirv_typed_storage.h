#pragma once
#include <algorithm>
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

// The number of components an SPIR-V image format carries. A typed image
// has to be written with a texel of at least that many components (VUID
// vkCmdDispatch-OpImageWrite-08795 for image views, 04469 for buffer views).
inline uint32_t xbg_spirv_image_format_components(uint32_t spirv_format){
    switch(spirv_format){
    case XBG_IMAGE_FORMAT_R32F:case XBG_IMAGE_FORMAT_R32I:case XBG_IMAGE_FORMAT_R32UI:return 1;
    case XBG_IMAGE_FORMAT_RG32F:case XBG_IMAGE_FORMAT_RG32I:case XBG_IMAGE_FORMAT_RG32UI:return 2;
    case XBG_IMAGE_FORMAT_RGBA32F:case XBG_IMAGE_FORMAT_RGBA32I:case XBG_IMAGE_FORMAT_RGBA32UI:return 4;
    default:return 0;
    }
}

// A typed module is only legal when the declared SPIR-V format is exactly the
// format of the bound view (VUID vkCmdDispatch-format-08795/08796).
inline bool xbg_typed_storage_matches(uint32_t spirv_format,uint32_t vk_format){
    return spirv_format!=XBG_IMAGE_FORMAT_UNKNOWN && spirv_format==xbg_spirv_image_format(vk_format);
}

// The strict "declared format must equal the bound view format" rule applies
// only to the bindings this engine rewrote itself. A module that already
// declares a typed format carries the game's own declaration: its NVN format
// and the Vulkan view format are the game's own pair, and NVN does not require
// the two to be spelled the same way (the real module c11e2f23ff063e85 binds a
// 3D Rg32ui storage image whose view format comes from the guest's NVN
// format). That pair is not the engine's to reject, and 1.0.7 ran the same
// modules without this check.
//
// rewritten  the binding is in the engine's rewritten set
// declared   the SPIR-V format the engine declared for that binding
// bound      the VkFormat of the view that is bound right now
inline bool xbg_typed_storage_requires_match(bool rewritten,uint32_t declared,uint32_t bound){
    return rewritten && xbg_typed_storage_matches(declared,bound);
}

// The image type a descriptor binding uses, with the operand positions the
// rewrite needs. Nothing is modified by this analysis.
struct XbgStorageImageBinding {
    uint32_t variable=0,image_type=0,pointer_type=0,sampled_type=0;
    size_t image_at=0;
    uint32_t declared_format=XBG_IMAGE_FORMAT_UNKNOWN;
    uint32_t dim=0;                 // the Dim operand of OpTypeImage
    bool found=false;
};

// The Dim operand of OpTypeImage.
enum {
    XBG_IMAGE_DIM_1D=0, XBG_IMAGE_DIM_2D=1, XBG_IMAGE_DIM_3D=2, XBG_IMAGE_DIM_CUBE=3,
    XBG_IMAGE_DIM_RECT=4, XBG_IMAGE_DIM_BUFFER=5, XBG_IMAGE_DIM_SUBPASS=6
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
    out.image_at=ii;out.declared_format=w[ii+8];out.dim=w[ii+3];out.found=true;return true;
}

// The numeric class of a storage binding's sampled type: 0 float, 1 signed,
// 2 unsigned, 3 when the module does not say. It is the class the declared
// image format has to match (VUID StandaloneSpirv-Image-04965).
inline uint32_t xbg_spirv_image_scalar_class(const std::vector<uint32_t> &w,uint32_t binding){
    XbgStorageImageBinding info;if(!xbg_spirv_binding_image(w,binding,info))return 3;
    for(size_t i=5;i<w.size();){uint32_t n=w[i]>>16,op=w[i]&65535u;
        if(n>=2&&w[i+1]==info.sampled_type){
            if(op==22)return 0;                          // OpTypeFloat
            if(op==21&&n>=4){if(w[i+3]==0)return 2;      // OpTypeInt 32 0
                              if(w[i+3]==1)return 1;}     // OpTypeInt 32 1
        }
        i+=n;}
    return 3;
}

// The number of components the module writes to (or reads from) a storage
// binding. A storage texel is a vector in the module, and the bound view
// format has to have that many components.
inline uint32_t xbg_spirv_storage_write_components(const std::vector<uint32_t> &w,uint32_t binding){
    XbgStorageImageBinding info;if(!xbg_spirv_binding_image(w,binding,info))return 0;
    std::unordered_map<uint32_t,size_t> def;             // type id -> OpTypeXxx
    std::unordered_map<uint32_t,uint32_t> value_type;    // value id -> its type id
    for(size_t i=5;i<w.size();){uint32_t n=w[i]>>16,op=w[i]&65535u;
        if(op>=19&&op<=33&&n>=2)def[w[i+1]]=i;
        else if(n>=3)value_type[w[i+2]]=w[i+1];
        i+=n;}
    // the values loaded from the binding's descriptor variable
    std::unordered_set<uint32_t> loaded;
    for(size_t i=5;i<w.size();){uint32_t n=w[i]>>16,op=w[i]&65535u;
        if(op==61&&n>=4&&w[i+3]==info.variable)loaded.insert(w[i+2]);
        i+=n;}
    uint32_t components=0;
    for(size_t i=5;i<w.size();){uint32_t n=w[i]>>16,op=w[i]&65535u;
        uint32_t texel=0;
        if(op==99&&n>=5&&loaded.count(w[i+1]))texel=w[i+3];   // OpImageWrite Image Texel
        else if(op==98&&n>=5&&loaded.count(w[i+3]))texel=w[i+2]; // OpImageRead Image Result
        if(texel){auto v=value_type.find(texel);
            if(v!=value_type.end()){auto t=def.find(v->second);
                if(t!=def.end()){
                    uint32_t c=(w[t->second]&65535u)==23?w[t->second+3]:1u;
                    if(c>components)components=c;}}}
        i+=n;}
    return components;
}

// The VkFormat to bind for a storage binding whose bound format SPIR-V
// cannot declare.
//
// SPIR-V's Image Format operand has no 16-bit or 8-bit format at all, and a
// Buffer image only ever carries Rgba32f/i/ui. A bound view whose format has
// no SPIR-V image format therefore cannot be declared by the module, so the
// typed-storage rewrite cannot rescue the binding and the module stays
// formatless. On a device without VK_FORMAT_FEATURE_2_STORAGE_WRITE_WITHOUT_FORMAT
// that is a fatal error.
//
// The engine then binds the format the module itself describes: the same
// numeric class, 32 bits per component, and the component count the module
// writes. The view and the module then describe the same texels, and the
// rewrite can declare them.
//
// Returns the bound format unchanged when there is nothing to substitute.
inline uint32_t xbg_typed_storage_view_format(const std::vector<uint32_t> &w,uint32_t binding,uint32_t native){
    if(xbg_spirv_image_format(native)!=XBG_IMAGE_FORMAT_UNKNOWN)return native;  // already declarable
    XbgStorageImageBinding info;if(!xbg_spirv_binding_image(w,binding,info))return native;
    const uint32_t klass=xbg_spirv_image_scalar_class(w,binding);if(klass>2)return native;
    uint32_t components=xbg_spirv_storage_write_components(w,binding);
    if(info.dim==XBG_IMAGE_DIM_BUFFER)components=4;   // a Buffer image texel is always RGBA
    if(components==3)return native;                    // SPIR-V has no Rgb32 image format
    if(components<1||components>4)return native;
    uint32_t candidate=0;
    if(klass==2){
        candidate=components==1?XBG_VK_FORMAT_R32_UINT:components==2?XBG_VK_FORMAT_R32G32_UINT:XBG_VK_FORMAT_R32G32B32A32_UINT;
    } else if(klass==1){
        candidate=components==1?XBG_VK_FORMAT_R32_SINT:components==2?XBG_VK_FORMAT_R32G32_SINT:XBG_VK_FORMAT_R32G32B32A32_SINT;
    } else {
        candidate=components==1?XBG_VK_FORMAT_R32_SFLOAT:components==2?XBG_VK_FORMAT_R32G32_SFLOAT:XBG_VK_FORMAT_R32G32B32A32_SFLOAT;
    }
    if(xbg_spirv_image_format(candidate)==XBG_IMAGE_FORMAT_UNKNOWN)return native;
    // The engine's own rewrite already declares this format: keep it, so the
    // bound view and the module describe the same texels. Without this the
    // descriptor path would fall back to the guest's 16-bit view and reject
    // the module the engine itself wrote.
    if(info.declared_format==xbg_spirv_image_format(candidate))return candidate;
    // A format the game declares itself is the game's own business.
    if(info.declared_format!=XBG_IMAGE_FORMAT_UNKNOWN)return native;
    return candidate;
}

// The SPIR-V image format a binding's module declares, or Unknown when the
// binding is formatless (or absent). The engine uses this as the module's
// actual form, not the package's formatless flag.
inline uint32_t xbg_spirv_declared_image_format(const std::vector<uint32_t> &w,uint32_t binding){
    XbgStorageImageBinding info;
    if(!xbg_spirv_binding_image(w,binding,info))return XBG_IMAGE_FORMAT_UNKNOWN;
    return info.declared_format;
}

// The bindings whose descriptor variable uses the same image type as the
// given binding. A shared image type declares one format for all of them, so
// the module may only declare a format when every one of them needs the same
// one (the declared format has to match the bound view of each).
inline void xbg_spirv_shared_image_bindings(const std::vector<uint32_t> &w,uint32_t binding,std::vector<uint32_t> &out){
    out.clear();
    if(w.size()<5||w[0]!=0x07230203u)return;
    std::unordered_map<uint32_t,size_t> def;uint32_t variable=0;
    for(size_t i=5;i<w.size();){uint32_t n=w[i]>>16,op=w[i]&65535u;
        if(op>=19&&op<=33&&n>=2)def[w[i+1]]=i;
        if(op==59&&n>=4)def[w[i+2]]=i;
        if(op==71&&n==4&&w[i+2]==33&&w[i+3]==binding)variable=w[i+1];
        i+=n;}
    if(!variable)return;
    auto var=def.find(variable);if(var==def.end())return;
    size_t vi=var->second;if((w[vi]&65535u)!=59)return;
    auto ptr=def.find(w[vi+1]);if(ptr==def.end())return;
    size_t pi=ptr->second;if((w[pi]&65535u)!=32)return;
    const uint32_t image=w[pi+3];
    std::unordered_set<uint32_t> pointers;            // the pointer types of that image type
    for(size_t i=5;i<w.size();){uint32_t n=w[i]>>16,op=w[i]&65535u;
        if(op==32&&n==4&&w[i+3]==image)pointers.insert(w[i+1]);
        i+=n;}
    std::unordered_map<uint32_t,uint32_t> binding_of;  // variable id -> its binding number
    for(size_t i=5;i<w.size();){uint32_t n=w[i]>>16,op=w[i]&65535u;
        if(op==71&&n==4&&w[i+2]==33)binding_of[w[i+1]]=w[i+3];
        i+=n;}
    for(size_t i=5;i<w.size();){uint32_t n=w[i]>>16,op=w[i]&65535u;
        if(op==59&&n>=4&&pointers.count(w[i+1])){
            auto b=binding_of.find(w[i+2]);
            if(b!=binding_of.end())out.push_back(b->second);}
        i+=n;}
    std::sort(out.begin(),out.end());
    out.erase(std::unique(out.begin(),out.end()),out.end());
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
// shared_ok  the caller has checked that every other binding that uses the
//             same image type needs the same declared format, so declaring it
//             on the shared type is correct for all of them. Without it a
//             shared image type is refused, because changing its format would
//             change another descriptor's access as well.
inline int xbg_typed_storage_rewrite(std::vector<uint32_t> &words,uint32_t binding,uint32_t spirv_format,bool shared_ok=false){
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
    if(users!=1&&!shared_ok)return -1;
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

// Every global variable whose type is a pointer to the image type of the given
// binding. A shared image type is used by more than one of them, and declares
// one format for all of them.
inline void xbg_spirv_image_type_users(const std::vector<uint32_t> &w,uint32_t binding,std::vector<uint32_t> &out){
    out.clear();
    if(w.size()<5||w[0]!=0x07230203u)return;
    XbgStorageImageBinding info;
    if(!xbg_spirv_binding_image(w,binding,info))return;
    std::unordered_map<uint32_t,size_t> def;
    for(size_t i=5;i<w.size();){uint32_t n=w[i]>>16,op=w[i]&65535u;
        if(op>=19&&op<=33&&n>=2)def[w[i+1]]=i;
        if(op==59&&n>=4)def[w[i+2]]=i;
        i+=n;
    }
    for(size_t i=5;i<w.size();){uint32_t n=w[i]>>16,op=w[i]&65535u;
        if(op==59&&n>=4){
            auto ptr=def.find(w[i+1]);
            if(ptr!=def.end()&&(w[ptr->second]&65535u)==32&&w[ptr->second+3]==info.image_type)out.push_back(w[i+2]);
        }
        i+=n;
    }
}

// The smallest number of components the module writes to a storage binding.
// A typed image has to be written with a texel of at least as many components
// as the format it declares, so a binding whose texels are narrower than the
// format cannot be declared. 0 when the binding has no write at all.
inline uint32_t xbg_spirv_storage_min_write_components(const std::vector<uint32_t> &w,uint32_t binding){
    XbgStorageImageBinding info;if(!xbg_spirv_binding_image(w,binding,info))return 0;
    std::unordered_map<uint32_t,size_t> def;             // type id -> OpTypeXxx
    std::unordered_map<uint32_t,uint32_t> value_type;    // value id -> its type id
    for(size_t i=5;i<w.size();){uint32_t n=w[i]>>16,op=w[i]&65535u;
        if(op>=19&&op<=33&&n>=2)def[w[i+1]]=i;
        else if(n>=3)value_type[w[i+2]]=w[i+1];
        i+=n;}
    std::unordered_set<uint32_t> loaded;
    for(size_t i=5;i<w.size();){uint32_t n=w[i]>>16,op=w[i]&65535u;
        if(op==61&&n>=4&&w[i+3]==info.variable)loaded.insert(w[i+2]);
        i+=n;}
    uint32_t minimum=0;
    for(size_t i=5;i<w.size();){uint32_t n=w[i]>>16,op=w[i]&65535u;
        if(op==99&&n>=5&&loaded.count(w[i+1])){
            auto v=value_type.find(w[i+3]);
            if(v==value_type.end())return 0;
            auto t=def.find(v->second);
            if(t==def.end())return 0;
            uint32_t c=(w[t->second]&65535u)==23?w[t->second+3]:1u;
            if(!minimum||c<minimum)minimum=c;
        }
        i+=n;}
    return minimum;
}


// The bindings that share one formatless storage image type. A module with
// such a group needs the image type split when the group does not agree on
// one format.
inline bool xbg_spirv_shared_image_group(const std::vector<uint32_t> &w,std::vector<uint32_t> &out){
    out.clear();
    if(w.size()<5||w[0]!=0x07230203u)return false;
    std::vector<uint32_t> bindings;
    for(size_t i=5;i<w.size();){uint32_t n=w[i]>>16,op=w[i]&65535u;
        if(n==0||i+n>w.size())return false;
        if(op==71&&n==4&&w[i+2]==33)bindings.push_back(w[i+3]);
        i+=n;
    }
    std::sort(bindings.begin(),bindings.end());
    bindings.erase(std::unique(bindings.begin(),bindings.end()),bindings.end());
    for(uint32_t binding:bindings){
        XbgStorageImageBinding info;
        if(!xbg_spirv_binding_image(w,binding,info))continue;
        if(info.declared_format!=XBG_IMAGE_FORMAT_UNKNOWN)continue;
        std::vector<uint32_t> sharers;
        xbg_spirv_shared_image_bindings(w,binding,sharers);
        if(sharers.size()>1){out=sharers;return true;}
    }
    return false;
}

// Gives one binding a private copy of the image type it shares, so that the
// binding can declare its own format while the other users of the shared type
// keep theirs.
//
// The binding's variable and the loads from it are retargeted to the new type.
// No value, coordinate, texel or write changes, so the shader writes exactly
// the bytes it wrote before: the bound view's format decides the texel
// layout in the typed and in the formatless form alike (VUID
// vkCmdDispatch-OpImageWrite-04469).
//
//  1 = the binding declares the format on its own image type
//  0 = the binding's image type is not shared: there is nothing to split
// -1 = refused; the module is unchanged
inline int xbg_typed_storage_split_rewrite(std::vector<uint32_t> &words,uint32_t binding,uint32_t spirv_format){
    const uint32_t scalar=xbg_spirv_image_format_scalar(spirv_format);
    if(scalar>2)return -1;
    if(words.size()<5||words[0]!=0x07230203u)return -1;
    XbgStorageImageBinding info;
    if(!xbg_spirv_binding_image(words,binding,info))return -1;
    if(info.declared_format!=XBG_IMAGE_FORMAT_UNKNOWN)return info.declared_format==spirv_format?0:-1;
    const uint32_t components=xbg_spirv_image_format_components(spirv_format);
    if(!components)return -1;
    // The image type has to be shared, or there is nothing to split.
    std::vector<uint32_t> users;
    xbg_spirv_image_type_users(words,binding,users);
    if(users.size()<2)return 0;
    // The sampled type must match the format's scalar class (VUID
    // StandaloneSpirv-Image-04965).
    if(xbg_spirv_image_scalar_class(words,binding)!=scalar)return -1;
    // The texel has to be at least as wide as the declared format.
    if(xbg_spirv_storage_min_write_components(words,binding)<components)return -1;
    std::unordered_map<uint32_t,size_t> def;
    for(size_t i=5;i<words.size();){uint32_t n=words[i]>>16,op=words[i]&65535u;
        if(op>=19&&op<=33&&n>=2)def[words[i+1]]=i;
        if(op==59&&n>=4)def[words[i+2]]=i;
        i+=n;
    }
    const auto variable=def.find(info.variable);if(variable==def.end())return -1;
    const auto pointer=def.find(info.pointer_type);if(pointer==def.end())return -1;
    // The values the binding loads, and the accesses that use them.
    std::unordered_set<uint32_t> loaded;
    std::unordered_set<size_t> load_at;
    for(size_t i=5;i<words.size();){uint32_t n=words[i]>>16,op=words[i]&65535u;
        if(op==61&&n>=4&&words[i+3]==info.variable){
            if(words[i+1]!=info.image_type)return -1;   // not an image load
            loaded.insert(words[i+2]);load_at.insert(i);
        }
        i+=n;
    }
    if(loaded.empty())return -1;
    // Only a write keeps its meaning when the image type changes: a read or a
    // texel pointer sees the format, so they keep the module formatless.
    for(size_t i=5;i<words.size();){uint32_t n=words[i]>>16,op=words[i]&65535u;
        if(!load_at.count(i)){
            for(uint32_t k=1;k<n;++k){
                if(!loaded.count(words[i+k]))continue;
                if(op==99)continue;                      // OpImageWrite
                return -1;
            }
        }
        i+=n;
    }
    // The new declarations: a private copy of the image type with the new
    // format, and a pointer to it.
    uint32_t bound=words[3];
    const uint32_t image_type=bound++;
    const uint32_t pointer_type=bound++;
    const uint32_t image_decl[9]={9u<<16|25u,image_type,
        words[info.image_at+2],words[info.image_at+3],words[info.image_at+4],
        words[info.image_at+5],words[info.image_at+6],words[info.image_at+7],spirv_format};
    const uint32_t pointer_decl[4]={4u<<16|32u,pointer_type,words[pointer->second+2],image_type};
    std::vector<uint32_t> out;out.reserve(words.size()+16);
    out.insert(out.end(),words.begin(),words.begin()+5);
    for(size_t i=5;i<words.size();){
        const uint32_t n=words[i]>>16,op=words[i]&65535u;
        if(i==variable->second){
            out.insert(out.end(),image_decl,image_decl+9);
            out.insert(out.end(),pointer_decl,pointer_decl+4);
        }
        const size_t at=out.size();
        out.insert(out.end(),words.begin()+i,words.begin()+i+n);
        if(i==variable->second)out[at+1]=pointer_type;
        if(op==61&&n>=4&&loaded.count(words[i+2])&&words[i+3]==info.variable)out[at+1]=image_type;
        i+=n;
    }
    out[3]=bound;
    words.swap(out);
    return 1;
}

// One binding that needs the module to declare a format.
struct XbgTypedStorageRequest {
    uint32_t binding=0;
    uint32_t spirv_format=XBG_IMAGE_FORMAT_UNKNOWN;
};

// Declares the requested formats in the module.
//
// The bindings that share one image type have to agree on the format, because
// the shared type declares one format for all of them. When they disagree,
// every one of them gets a private copy of the type and declares its own
// format. A binding that shares the type and is not in the list would keep
// the shared type formatless, so the whole group is refused.
//
// failed   the request that could not be declared
// shared   1 when the shared type could carry the format of that request,
//          0 when the group disagrees or a sharer is not in the list
// split    the bindings that received a private image type
//
//  1 = the module declares the requested formats
// -1 = refused; the module is unchanged
inline int xbg_typed_storage_declare(std::vector<uint32_t> &words,const std::vector<XbgTypedStorageRequest> &requests,size_t &failed,int &shared,std::vector<uint32_t> &split){
    failed=0;shared=1;split.clear();
    if(requests.empty())return -1;
    if(words.size()<5||words[0]!=0x07230203u)return -1;
    std::vector<uint32_t> image_of(requests.size(),0);
    for(size_t r=0;r<requests.size();++r){
        XbgStorageImageBinding info;
        if(!xbg_spirv_binding_image(words,requests[r].binding,info)){failed=r;return -1;}
        image_of[r]=info.image_type;
    }
    std::vector<char> handled(requests.size(),0);
    for(size_t r=0;r<requests.size();++r){
        if(handled[r])continue;
        // the requests that share this image type
        std::vector<size_t> group;
        for(size_t k=0;k<requests.size();++k)if(image_of[k]==image_of[r])group.push_back(k);
        // Every descriptor that uses the shared type has to be in the group: a
        // binding that is not in it would keep the shared type formatless.
        std::vector<uint32_t> sharers;
        xbg_spirv_shared_image_bindings(words,requests[r].binding,sharers);
        for(uint32_t other:sharers){
            bool in_group=false;
            for(size_t k:group)if(requests[k].binding==other)in_group=true;
            if(!in_group){failed=r;shared=0;return -1;}
        }
        // The group agrees on one format, or every binding of it gets a
        // private copy of the image type.
        bool same=true;
        for(size_t k:group)if(requests[k].spirv_format!=requests[group[0]].spirv_format)same=false;
        for(size_t k:group){
            int rewritten=0;
            if(same)rewritten=xbg_typed_storage_rewrite(words,requests[k].binding,requests[k].spirv_format,true);
            else{
                rewritten=xbg_typed_storage_split_rewrite(words,requests[k].binding,requests[k].spirv_format);
                // The first split leaves the last sharer alone on the shared
                // type, which it can declare itself.
                if(rewritten==0)rewritten=xbg_typed_storage_rewrite(words,requests[k].binding,requests[k].spirv_format,true);
                else if(rewritten==1)split.push_back(requests[k].binding);
            }
            if(rewritten!=1&&rewritten!=0){failed=k;shared=same?1:0;return -1;}
            handled[k]=1;
        }
    }
    return 1;
}
