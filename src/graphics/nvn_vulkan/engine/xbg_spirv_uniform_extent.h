#pragma once
#include "third_party/droidplay/spv_lit.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

// Adapted from DroidPlay spv_cb_ext2 at a72a85c59cac54f3428827cfb72e1adf63b1521a.
// MIT copyright and complete license are in third_party/droidplay/LICENSE.
// Only direct accesses to Block { vec4[N] } at Offset 0 / ArrayStride 16 are bounded.
// An absent result keeps the caller's complete bound range. Guest bytes remain live.
inline std::map<uint32_t,uint32_t> xbg_spirv_uniform_extent(
        const std::vector<uint32_t> &words,const std::vector<uint32_t> &bindings) {
    if(bindings.empty() || words.size()<5 || words[0]!=0x07230203u || !words[3] || words[4])return {};
    const uint32_t bound=words[3];
    // Bound analysis memory before allocating metadata for untrusted module IDs.
    if(bound>(1u<<20))return {};
    struct Id {
        uint32_t op=0,a=0,b=0,stride=0,binding=UINT32_MAX,offset=UINT32_MAX;
        uint32_t value=0;bool constant=false,block=false;
    };
    std::vector<Id> ids(bound);
    for(size_t at=5;at<words.size();){
        const uint32_t count=words[at]>>16,op=words[at]&0xffffu;
        if(!count || count>words.size()-at)return {};
        if((op>=73 && op<=75) || op==67 || op==70)return {};
        if((op==54 && count!=5) || (op==56 && count!=1) || ((op==65 || op==66) && count<5))return {};
        switch(op){
        case 71:
            if(count<3 || words[at+1]>=bound)return {};
            if(words[at+2]==2)ids[words[at+1]].block=true;
            if(words[at+2]==33 || words[at+2]==6){
                if(count!=4)return {};
                if(words[at+2]==33)ids[words[at+1]].binding=words[at+3];
                else ids[words[at+1]].stride=words[at+3];
            }
            break;
        case 72:
            if(count<4 || words[at+1]>=bound)return {};
            if(words[at+2]==0 && words[at+3]==35){
                if(count!=5)return {};
                ids[words[at+1]].offset=words[at+4];
            }
            break;
        case 21:case 22:case 23:case 28:case 30:case 32:{
            uint32_t minimum=op==22 || op==30?3:4;
            if(count<minimum || !words[at+1] || words[at+1]>=bound)return {};
            Id &id=ids[words[at+1]];
            if(id.op)return {};
            id.op=op;id.a=words[at+2];
            if(op==23 || op==28 || op==32)id.b=words[at+3];
            break;
        }
        case 43:
            if(count<4 || words[at+1]>=bound || !words[at+2] || words[at+2]>=bound)return {};
            if(count==4 && ids[words[at+1]].op==21 && ids[words[at+1]].a==32){
                ids[words[at+2]].constant=true;ids[words[at+2]].value=words[at+3];
            }
            break;
        case 59:{
            if(count<4 || !words[at+2] || words[at+2]>=bound)return {};
            Id &id=ids[words[at+2]];if(id.op)return {};
            id.op=op;id.a=words[at+1];id.b=words[at+3];break;
        }
        default:break;
        }
        at+=count;
    }
    struct Extent {uint32_t elements=0,bytes=16;bool full=false,found=false;};
    std::map<uint32_t,Extent> extents;
    for(uint32_t binding:bindings)extents.emplace(binding,Extent{});
    for(const Id &variable:ids){
        auto found=extents.find(variable.binding);
        if(variable.op!=59 || found==extents.end())continue;
        Extent &extent=found->second;extent.found=true;
        const Id *pointer=variable.a<bound?&ids[variable.a]:nullptr;
        const Id *structure=pointer && pointer->op==32 && pointer->b<bound?&ids[pointer->b]:nullptr;
        const Id *array=structure && structure->op==30 && structure->a<bound?&ids[structure->a]:nullptr;
        const Id *vector=array && array->op==28 && array->a<bound?&ids[array->a]:nullptr;
        const Id *scalar=vector && vector->op==23 && vector->a<bound?&ids[vector->a]:nullptr;
        const Id *length=array && array->b<bound?&ids[array->b]:nullptr;
        if(variable.b!=2 || !pointer || pointer->a!=2 || !structure || !structure->block || structure->offset!=0 ||
           !array || array->stride!=16 || !vector || vector->b!=4 || !scalar ||
           (scalar->op!=21 && scalar->op!=22) || scalar->a!=32 || !length || !length->constant || !length->value){
            extent.full=true;continue;
        }
        extent.elements=extent.elements?std::min(extent.elements,length->value):length->value;
    }
    bool function=false;
    for(size_t at=5;at<words.size();){
        const uint32_t count=words[at]>>16,op=words[at]&0xffffu;
        if(op==54)function=true;
        if(!function){
            if(op==59 && count>=5 && words[at+4]<bound){
                auto found=extents.find(ids[words[at+4]].binding);
                if(ids[words[at+4]].op==59 && found!=extents.end())found->second.full=true;
            }
        }else for(uint32_t operand=1;operand<count;++operand){
            const uint32_t variable=words[at+operand];
            if(variable>=bound || ids[variable].op!=59 || spv_lit(op,operand) || (op==59 && operand==2))continue;
            auto found=extents.find(ids[variable].binding);if(found==extents.end())continue;
            Extent &extent=found->second;
            if((op!=65 && op!=66) || operand!=3 || count<6){extent.full=true;continue;}
            const uint32_t member=words[at+4],element=words[at+5];
            if(member>=bound || !ids[member].constant || ids[member].value!=0 || element>=bound ||
               !ids[element].constant || !extent.elements || ids[element].value>=extent.elements || ids[element].value>=4096){
                extent.full=true;continue;
            }
            extent.bytes=std::max(extent.bytes,(ids[element].value+1)*16);
        }
        at+=count;
    }
    std::map<uint32_t,uint32_t> result;
    for(const auto &entry:extents)
        if(entry.second.found && !entry.second.full && entry.second.bytes<65536)result.emplace(entry.first,entry.second.bytes);
    return result;
}
