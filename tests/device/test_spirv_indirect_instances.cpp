#include "../../src/graphics/nvn_vulkan/engine/xbg_spirv_indirect_instances.h"
#include "../../src/graphics/nvn_vulkan/engine/xbg_spirv_temps.h"
#include "../../src/graphics/nvn_vulkan/engine/xbg_spirv_ldc.h"
#include "../../src/graphics/nvn_vulkan/engine/xbg_spirv_warp.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>

static void emit(std::vector<uint32_t> &words,uint32_t opcode,std::initializer_list<uint32_t> operands){
    words.push_back((uint32_t(operands.size()+1)<<16)|opcode);
    words.insert(words.end(),operands.begin(),operands.end());
}
static std::vector<uint32_t> fixture(bool local=false){
    std::vector<uint32_t> w={0x07230203u,0x00010300u,0,64,0};
    emit(w,17,{1});emit(w,17,{47}); // Shader, ImageBuffer
    emit(w,14,{0,1});
    emit(w,15,{5,17,0x6e69616d,0,2});emit(w,16,{17,17,8,8,1});
    emit(w,71,{1,34,0});emit(w,71,{1,33,77});emit(w,71,{2,11,26});
    emit(w,21,{3,32,0});emit(w,22,{4,32});emit(w,23,{5,3,3});
    emit(w,25,{6,3,5,0,0,0,2,33});
    emit(w,32,{7,0,6});emit(w,32,{8,11,3});emit(w,32,{9,1,5});emit(w,32,{10,1,3});
    emit(w,19,{11});emit(w,33,{12,11});emit(w,32,{25,7,4});
    emit(w,43,{3,13,0});emit(w,43,{3,14,1});emit(w,43,{3,15,5});emit(w,43,{3,16,2});
    emit(w,59,{7,1,0});emit(w,59,{9,2,1});
    emit(w,54,{11,17,0,12});emit(w,248,{18});
    if(local)emit(w,59,{25,26,7});
    emit(w,65,{10,19,2,16});emit(w,61,{3,20,19});emit(w,132,{3,21,20,15});
    if(local){emit(w,124,{4,27,21});emit(w,62,{26,27});emit(w,61,{4,28,26});emit(w,124,{3,29,28});}
    emit(w,128,{3,22,local?29u:21u,14});emit(w,60,{8,23,1,22,13});
    emit(w,234,{3,24,23,14,13,14});emit(w,253,{});emit(w,56,{});
    return w;
}
static size_t find(const std::vector<uint32_t> &w,uint32_t opcode){
    for(size_t i=5;i<w.size();i+=w[i]>>16)if((w[i]&65535u)==opcode)return i;
    assert(false);return 0;
}
static void accepted(const std::vector<uint32_t> &w,uint32_t binding,uint32_t axis){
    std::vector<XbgIndirectInstanceField> facts;
    assert(xbg_spirv_indirect_instances(w,facts)==1 && facts.size()==1);
    assert(facts[0].binding==binding && facts[0].axis==axis);
}
static void rejected(const std::vector<uint32_t> &w){
    std::vector<XbgIndirectInstanceField> facts={{999,999}};
    assert(xbg_spirv_indirect_instances(w,facts)<=0 && facts.empty());
}
static std::vector<uint32_t> read_module(const char *path){
    std::ifstream f(path,std::ios::binary);assert(f.good());
    std::vector<char> bytes((std::istreambuf_iterator<char>(f)),{});assert(bytes.size()>=20 && bytes.size()%4==0);
    std::vector<uint32_t> w(bytes.size()/4);memcpy(w.data(),bytes.data(),bytes.size());
    if(w[0]==0x31534758u){
        assert(w.size()>=8);size_t offset=32+size_t(w[3])*32+size_t(w[4])*4+w[5];
        assert(offset<=bytes.size() && bytes.size()-offset==w[6]);
        std::vector<uint32_t> spv(w[6]/4);memcpy(spv.data(),bytes.data()+offset,w[6]);return spv;
    }
    return w;
}
int main(int argc,char **argv){
    assert(argc==2 || argc==3);
    auto actual=read_module(argv[1]);accepted(actual,29,2);
    auto remapped=actual;
    for(size_t i=5;i<remapped.size();i+=remapped[i]>>16)
        if((remapped[i]&65535u)==71 && (remapped[i]>>16)==4 && remapped[i+2]==33 && remapped[i+3]==29)remapped[i+3]=77;
    accepted(remapped,77,2);
    std::vector<uint32_t> forwarded;
    assert(xbg_spirv_forward_temps(actual.data(),actual.size(),forwarded)>0);accepted(forwarded,29,2);
    // The engine's compute pass order after optional helper inlining.
    for(uint32_t subgroup:{32u,64u,128u}){
        auto optimized=actual;std::vector<uint32_t> next;
        if(subgroup>32 && xbg_spirv_warp32(optimized.data(),optimized.size(),subgroup,next))optimized.swap(next);
        if(xbg_spirv_ldc_fold(optimized.data(),optimized.size(),next))optimized.swap(next);
        if(xbg_spirv_forward_temps(optimized.data(),optimized.size(),next))optimized.swap(next);
        accepted(optimized,29,2);
    }
    auto w=fixture();accepted(w,77,2);accepted(fixture(true),77,2);
    if(argc==3){std::ofstream f(argv[2],std::ios::binary);f.write(reinterpret_cast<const char*>(w.data()),w.size()*4);assert(f.good());}
    // Arbitrary Workgroup axes and commuted integer multiplication/addition.
    auto axis=w;
    for(size_t i=5;i<axis.size();i+=axis[i]>>16)if((axis[i]&65535u)==43 && axis[i+2]==16)axis[i+3]=0;
    accepted(axis,77,0);
    auto commuted=w;size_t mul=find(commuted,132),add=find(commuted,128);
    std::swap(commuted[mul+3],commuted[mul+4]);std::swap(commuted[add+3],commuted[add+4]);accepted(commuted,77,2);
    auto unsafe=w;unsafe[find(unsafe,128)+4]=15;rejected(unsafe); // word 5, not instanceCount
    unsafe=w;unsafe[find(unsafe,132)+4]=14;rejected(unsafe); // row stride 1
    unsafe=w;unsafe[find(unsafe,60)+4]=13;rejected(unsafe); // fixed address
    auto extra=w;size_t ret=find(extra,253);std::vector<uint32_t> op;
    emit(op,229,{3,30,23,14,13,14});extra.insert(extra.begin()+ret,op.begin(),op.end());rejected(extra); // AtomicExchange
    auto alias=w;ret=find(alias,253);op.clear();emit(op,83,{8,30,23});alias.insert(alias.begin()+ret,op.begin(),op.end());rejected(alias);
    auto image_alias=w;ret=find(image_alias,253);op.clear();emit(op,61,{6,30,1});image_alias.insert(image_alias.begin()+ret,op.begin(),op.end());rejected(image_alias);
    auto image_write=w;size_t function=find(image_write,54);op.clear();emit(op,23,{31,3,4});image_write.insert(image_write.begin()+function,op.begin(),op.end());
    ret=find(image_write,253);op.clear();emit(op,61,{6,30,1});emit(op,80,{31,32,14,14,14,14});emit(op,99,{30,22,32});
    image_write.insert(image_write.begin()+ret,op.begin(),op.end());rejected(image_write);
    auto escape=w;ret=find(escape,253);op.clear();emit(op,57,{11,30,31,23});escape.insert(escape.begin()+ret,op.begin(),op.end());rejected(escape);
    auto local_alias=fixture(true);ret=find(local_alias,253);op.clear();emit(op,83,{25,30,26});local_alias.insert(local_alias.begin()+ret,op.begin(),op.end());rejected(local_alias);
    auto conflict=fixture(true);ret=find(conflict,253);op.clear();emit(op,62,{26,28});conflict.insert(conflict.begin()+ret,op.begin(),op.end());rejected(conflict);
    auto malformed=w;malformed.push_back(0);rejected(malformed);
    malformed=w;malformed.pop_back();rejected(malformed); // Unterminated function
    malformed={0x07230203u,0x00010300u,0,64,0,(2u<<16)|60u,1};rejected(malformed);
    for(size_t size=0;size<w.size();++size){auto truncated=w;truncated.resize(size);std::vector<XbgIndirectInstanceField> facts;xbg_spirv_indirect_instances(truncated,facts);}
    std::cout<<"Indirect instance-field structure PASS, actual module, forwarded locals, arbitrary binding/axis, unsafe writes/aliases/escape and malformed controls\n";
}
