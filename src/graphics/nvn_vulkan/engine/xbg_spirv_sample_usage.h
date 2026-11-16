#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <vector>

// Only prove bindings whose image accesses ignore sampler filtering. Unknown
// uses, calls, merges and ordinary sampling keep the original sampler contract.
inline std::set<uint32_t> xbg_spirv_filter_independent(const std::vector<uint32_t> &words) {
    if(words.size()<5 || words[0]!=0x07230203 || !words[3])return {};
    std::vector<size_t> instructions;
    std::map<uint32_t,uint32_t> bindings,sets,value_types,sampled_images;
    std::set<uint32_t> images_2d;
    for(size_t i=5;i<words.size();) {
        const uint32_t count=words[i]>>16,op=words[i]&65535;
        if(!count || count>words.size()-i)return {};
        instructions.push_back(i);
        if(op==25 && count>=9 && words[i+3]==1)images_2d.insert(words[i+1]);
        if(op==27 && count==3)sampled_images[words[i+1]]=words[i+2];
        if(count>=4 && (op==61 || op==83 || op==86 || op==100))value_types[words[i+2]]=words[i+1];
        if(op==71 && count==4) {
            if(words[i+2]==33)bindings[words[i+1]]=words[i+3];
            if(words[i+2]==34)sets[words[i+1]]=words[i+3];
        }
        i+=count;
    }
    using Roots=std::set<uint32_t>;
    std::map<uint32_t,Roots> roots;
    for(size_t i:instructions) {
        if((words[i]&65535)==59 && (words[i]>>16)>=4 && words[i+3]==0) {
            uint32_t id=words[i+2];
            auto binding=bindings.find(id),set=sets.find(id);
            if(binding!=bindings.end() && set!=sets.end() && set->second==0)
                roots[id].insert(binding->second);
        }
    }
    bool changed=true;
    while(changed) {
        changed=false;
        for(size_t i:instructions) {
            const uint32_t op=words[i]&65535,count=words[i]>>16;
            if(count<4 || !(op==61 || op==65 || op==66 || op==67 || op==83 || op==86 || op==100))continue;
            Roots inputs;
            const unsigned end=op==86?5:4;
            if(count<end)return {};
            for(unsigned j=3;j<end;++j) {
                auto source=roots.find(words[i+j]);
                if(source!=roots.end())inputs.insert(source->second.begin(),source->second.end());
            }
            if(inputs.empty())continue;
            Roots &result=roots[words[i+2]];
            size_t before=result.size();result.insert(inputs.begin(),inputs.end());
            changed|=before!=result.size();
        }
    }
    Roots accessed,unsafe;
    for(size_t i:instructions) {
        const uint32_t op=words[i]&65535,count=words[i]>>16;
        // Debug, annotations and declarations do not consume image values.
        if((op<54 && op!=12) || op==59 || op==71 || op==72)continue;
        const bool alias=op==61 || op==65 || op==66 || op==67 || op==83 || op==86 || op==100;
        const bool unfiltered=op==95 || op==96 || op==97 || op==101 || op==102 || op==103 || op==104 || op==106 || op==107;
        for(unsigned j=1;j<count;++j) {
            auto source=roots.find(words[i+j]);
            if(source==roots.end())continue;
            if(alias && (j==2 || j==3 || (op==86 && j==4)))continue;
            if(unfiltered && j==3) {
                auto type=value_types.find(words[i+j]);
                auto sampled=type==value_types.end()?sampled_images.end():sampled_images.find(type->second);
                const bool gather_2d=sampled!=sampled_images.end() && images_2d.count(sampled->second);
                if((op==96 || op==97) && !gather_2d)unsafe.insert(source->second.begin(),source->second.end());
                else accessed.insert(source->second.begin(),source->second.end());
                continue;
            }
            unsafe.insert(source->second.begin(),source->second.end());
        }
    }
    for(uint32_t binding:unsafe)accessed.erase(binding);
    return accessed;
}
