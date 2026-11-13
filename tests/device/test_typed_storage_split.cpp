#include "../../src/graphics/nvn_vulkan/engine/xbg_spirv_typed_storage.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <map>
#include <unordered_map>
#include <vector>

// The real FFRS battle compute modules whose two bindings share one
// formatless storage image type:
//   9bee7f09d6c655c3  bindings 29 and 35
//   ba0d8842fa53e521 bindings 53 and 59
// A shared image type declares one format for all of its users, so two
// bindings that need different bound view formats cannot both declare their
// format on it. The type has to be split: every binding of the group gets a
// private copy of the type and declares its own format.
//
// usage: test-typed-storage-split <module.spv> [rewritten.spv]

static std::vector<uint32_t> load(const char *p){
    std::ifstream in(p,std::ios::binary|std::ios::ate);assert(in.good());
    auto b=in.tellg();assert(b>=20&&b%4==0);in.seekg(0);
    std::vector<uint32_t> w(size_t(b)/4);in.read(reinterpret_cast<char*>(w.data()),b);assert(in.good());
    return w;
}
// The raw words of every instruction with the given opcode, in order. The
// result type word is cleared when asked for: that is the only word the split
// may change in an instruction.
static std::vector<uint32_t> opcode_words(const std::vector<uint32_t> &w,uint32_t op,bool clear_result_type){
    std::vector<uint32_t> out;
    for(size_t i=5;i<w.size();){uint32_t n=w[i]>>16,o=w[i]&65535u;
        if(n==0||i+n>w.size())break;
        if(o==op){
            std::vector<uint32_t> inst(w.begin()+i,w.begin()+i+n);
            if(clear_result_type&&inst.size()>=2)inst[1]=0;
            out.insert(out.end(),inst.begin(),inst.end());
        }
        i+=n;}
    return out;
}
static std::map<uint32_t,int> histogram(const std::vector<uint32_t> &w){
    std::map<uint32_t,int> h;
    for(size_t i=5;i<w.size();){uint32_t n=w[i]>>16,o=w[i]&65535u;
        if(n==0||i+n>w.size())break;
        ++h[o];i+=n;}
    return h;
}
// The image type the loads of a descriptor variable produce.
static uint32_t loaded_image_type(const std::vector<uint32_t> &w,uint32_t variable){
    uint32_t type=0;
    for(size_t i=5;i<w.size();){uint32_t n=w[i]>>16,op=w[i]&65535u;
        if(n==0||i+n>w.size())break;
        if(op==61&&n>=4&&w[i+3]==variable&&!type)type=w[i+1];
        i+=n;}
    return type;
}
static bool save(const char *p,const std::vector<uint32_t> &w){
    std::ofstream out(p,std::ios::binary);
    out.write(reinterpret_cast<const char*>(w.data()),std::streamsize(w.size()*4));
    return bool(out);
}
int main(int argc,char**argv){
    assert(argc>=2);
    const auto words=load(argv[1]);
    std::vector<uint32_t> group;
    assert(xbg_spirv_shared_image_group(words,group));
    assert(group.size()>=2);
    const uint32_t first=group[0],second=group[1];
    // Both bindings write four components and read nothing: a typed
    // declaration is safe for both of them.
    const uint32_t klass=xbg_spirv_image_scalar_class(words,first);
    assert(klass<3);
    assert(xbg_spirv_image_scalar_class(words,second)==klass);
    assert(xbg_spirv_storage_min_write_components(words,first)>=4);
    assert(xbg_spirv_storage_min_write_components(words,second)>=4);
    // The formats of the group: one component and four components of the
    // module's own numeric class.
    const uint32_t narrow=klass==0?XBG_IMAGE_FORMAT_R32F:klass==1?XBG_IMAGE_FORMAT_R32I:XBG_IMAGE_FORMAT_R32UI;
    const uint32_t wide=klass==0?XBG_IMAGE_FORMAT_RGBA32F:klass==1?XBG_IMAGE_FORMAT_RGBA32I:XBG_IMAGE_FORMAT_RGBA32UI;
    // The guardrail: the shared type may not carry one format for both, so
    // the plain rewrite is refused and the module stays byte-for-byte the
    // same.
    {
        auto refused=words;
        assert(xbg_typed_storage_rewrite(refused,first,narrow,false)==-1);
        assert(refused==words);
    }
    // A group that does not cover every sharer is refused as well: the
    // missing binding would keep the shared type formatless.
    {
        auto partial=words;
        size_t failed=0;int shared=1;std::vector<uint32_t> split;
        const std::vector<XbgTypedStorageRequest> requests={{first,narrow}};
        assert(xbg_typed_storage_declare(partial,requests,failed,shared,split)==-1);
        assert(partial==words);
        assert(shared==0);
    }
    // The group agrees on one format: the shared type carries it for both.
    {
        auto typed=words;
        size_t failed=0;int shared=1;std::vector<uint32_t> split;
        const std::vector<XbgTypedStorageRequest> requests={{first,narrow},{second,narrow}};
        assert(xbg_typed_storage_declare(typed,requests,failed,shared,split)==1);
        assert(xbg_spirv_declared_image_format(typed,first)==narrow);
        assert(xbg_spirv_declared_image_format(typed,second)==narrow);
        assert(split.empty());
    }
    // The group disagrees: the type is split and every binding declares its
    // own format on a type of its own.
    auto typed=words;
    size_t failed=0;int shared=1;std::vector<uint32_t> split;
    const std::vector<XbgTypedStorageRequest> requests={{first,narrow},{second,wide}};
    assert(xbg_typed_storage_declare(typed,requests,failed,shared,split)==1);
    assert(xbg_spirv_declared_image_format(typed,first)==narrow);
    assert(xbg_spirv_declared_image_format(typed,second)==wide);
    XbgStorageImageBinding split_first,split_second;
    assert(xbg_spirv_binding_image(typed,first,split_first));
    assert(xbg_spirv_binding_image(typed,second,split_second));
    assert(split_first.image_type!=split_second.image_type);   // the type was split
    // The loads follow the new type, or the module does not type-check.
    assert(loaded_image_type(typed,split_first.variable)==split_first.image_type);
    assert(loaded_image_type(typed,split_second.variable)==split_second.image_type);
    assert(split.size()==1&&split[0]==first);
    // The strict "declared format equals the bound view format" rule holds
    // for both bindings now.
    assert(xbg_typed_storage_requires_match(true,narrow,XBG_VK_FORMAT_R32_SFLOAT)||
           xbg_typed_storage_requires_match(true,narrow,XBG_VK_FORMAT_R32_UINT)||
           xbg_typed_storage_requires_match(true,narrow,XBG_VK_FORMAT_R32_SINT));
    assert(xbg_typed_storage_requires_match(true,wide,XBG_VK_FORMAT_R32G32B32A32_SFLOAT)||
           xbg_typed_storage_requires_match(true,wide,XBG_VK_FORMAT_R32G32B32A32_UINT)||
           xbg_typed_storage_requires_match(true,wide,XBG_VK_FORMAT_R32G32B32A32_SINT));
    // The writes are untouched: the same instructions, the same texels, the
    // same coordinates. Only the type declarations were added and the
    // variable and its loads were retargeted.
    assert(opcode_words(typed,99,false)==opcode_words(words,99,false));   // OpImageWrite
    assert(opcode_words(typed,80,false)==opcode_words(words,80,false));   // OpCompositeConstruct
    assert(opcode_words(typed,61,true)==opcode_words(words,61,true));     // OpLoad
    const auto before=histogram(words),after=histogram(typed);
    for(const auto &entry:before){
        const uint32_t op=entry.first;
        const int expected=entry.second+(op==25?1:op==32?1:0);
        assert(after.count(op)&&after.at(op)==expected);
    }
    assert(after.size()==before.size());
    // The new ids are inside the module's bound.
    assert(typed[3]>words[3]);
    printf("shared image type of bindings %u and %u: the type is split and every binding declares its own format PASS\n",first,second);
    if(argc>=3)assert(save(argv[2],typed));
    return 0;
}
