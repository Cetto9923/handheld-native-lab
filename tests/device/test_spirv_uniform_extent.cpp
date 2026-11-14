#include "../../src/graphics/nvn_vulkan/engine/xbg_spirv_uniform_extent.h"
#include <cassert>
#include <fstream>
#include <iostream>

static size_t instruction(const std::vector<uint32_t> &words,uint32_t opcode) {
    for(size_t at=5;at<words.size();at+=words[at]>>16)
        if((words[at]&0xffffu)==opcode)return at;
    assert(false);return 0;
}

static std::vector<uint32_t> read_module(const char *path) {
    std::ifstream file(path,std::ios::binary|std::ios::ate);assert(file);
    const auto bytes=file.tellg();assert(bytes>=20 && bytes%4==0);
    std::vector<uint32_t> words(size_t(bytes)/4);file.seekg(0);
    file.read(reinterpret_cast<char*>(words.data()),bytes);assert(file);return words;
}

static uint32_t extent(const std::vector<uint32_t> &words) {
    const auto result=xbg_spirv_uniform_extent(words,{1});
    const auto found=result.find(1);return found==result.end()?0:found->second;
}

int main(int argc,char **argv) {
    assert(argc>=2);
    const auto original=read_module(argv[1]);
    const size_t access=instruction(original,65),type_array=instruction(original,28);
    const size_t member=instruction(original,72),variable=instruction(original,59);
    const uint32_t variable_id=original[variable+2];
    assert(variable_id==10 && extent(original)==64);
    assert(xbg_spirv_uniform_extent(original,{2}).empty());
    assert(xbg_spirv_uniform_extent(original,{}).empty());

    // An OpConstant literal equals the uniform ID, and is unrelated to its use.
    bool collision=false;
    for(size_t at=5;at<original.size();at+=original[at]>>16)
        if((original[at]&0xffffu)==43 && original[at+3]==variable_id)collision=true;
    assert(collision);
    auto words=original;
    assert(words[instruction(words,12)+4]==variable_id);
    words[access]=(6u<<16)|66u;assert(extent(words)==64);
    words=original;
    const size_t computed=instruction(words,128);
    words[access+5]=words[computed+2];assert(extent(words)==0);
    // A smaller declared array still keeps dynamic indexing on the full path.
    const uint32_t length_id=words[type_array+3];
    for(size_t at=5;at<words.size();at+=words[at]>>16)
        if((words[at]&0xffffu)==43 && words[at+2]==length_id)words[at+3]=16;
    assert(extent(words)==0);
    words=original;
    for(size_t at=5;at<words.size();at+=words[at]>>16)
        if((words[at]&0xffffu)==43 && words[at+2]==words[access+5])words[at]=(4u<<16)|50u;
    assert(extent(words)==0); // a specialization index can change after analysis

    words=original;words[member+4]=16;assert(extent(words)==0);
    words=original;words[member+3]=34;assert(extent(words)==0); // no known Offset
    words=original;
    for(size_t at=5;at<words.size();at+=words[at]>>16)
        if((words[at]&0xffffu)==71 && words[at+2]==6)words[at+3]=8;
    assert(extent(words)==0);
    words=original;words[instruction(words,23)+3]=2;assert(extent(words)==0);
    words=original;words[instruction(words,22)+2]=16;assert(extent(words)==0);
    words=original;words[type_array]=(4u<<16)|25u;assert(extent(words)==0);
    words=original;words[variable+3]=12;assert(extent(words)==0);
    words=original;
    for(size_t at=5;at<words.size();at+=words[at]>>16)
        if((words[at]&0xffffu)==71 && words[at+2]==2)words[at+2]=3;
    assert(extent(words)==0);

    // Accessing the complete block, or escaping its pointer, keeps all bytes.
    words=original;words[access]=(6u<<16)|83u;assert(extent(words)==0);
    words=original;words[access]=(6u<<16)|65000u;assert(extent(words)==0);
    words=original;words[access]=(6u<<16)|67u;assert(extent(words)==0);
    words=original;words[access]=(6u<<16)|70u;assert(extent(words)==0);
    words=original;words[access]=(6u<<16)|74u;assert(extent(words)==0);
    words=original;words[access+4]=original[access+5];assert(extent(words)==0);
    words=original;
    const uint32_t element_id=words[access+5];
    for(size_t at=5;at<words.size();at+=words[at]>>16)
        if((words[at]&0xffffu)==43 && words[at+2]==element_id)words[at+3]=4096;
    assert(extent(words)==0);

    // Unused valid blocks retain a nonzero descriptor range.
    words=original;words.erase(words.begin()+access,words.begin()+access+6);
    const size_t load=instruction(words,61);words.erase(words.begin()+load,words.begin()+load+4);
    const size_t ext_inst=instruction(words,12);words.erase(words.begin()+ext_inst,words.begin()+ext_inst+6);
    assert(extent(words)==16);
    words=original;words.pop_back();words[5]=0;assert(extent(words)==0);
    words=original;words[access]=0;assert(extent(words)==0);
    words=original;words[access]=(0xffffu<<16)|65u;assert(extent(words)==0);
    words=original;words[0]=0;assert(extent(words)==0);
    words=original;words[3]=(1u<<20)+1;assert(extent(words)==0);
    words=original;words[3]=0;assert(extent(words)==0);
    std::cout<<"uniform extent regression cases passed\n";

    // Optional local real modules stay outside Git. Report every Uniform binding.
    for(int argument=2;argument<argc;++argument){
        words=read_module(argv[argument]);std::vector<uint32_t> bindings;
        for(size_t at=5;at<words.size();at+=words[at]>>16)
            if((words[at]&0xffffu)==71 && (words[at]>>16)==4 && words[at+2]==33)bindings.push_back(words[at+3]);
        const auto result=xbg_spirv_uniform_extent(words,bindings);
        for(const auto &entry:result)std::cout<<argv[argument]<<'\t'<<entry.first<<'\t'<<entry.second<<'\n';
    }
}
