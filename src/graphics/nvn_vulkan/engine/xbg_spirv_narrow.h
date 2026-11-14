#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>
#include "third_party/droidplay/spv_narrow.h"

namespace xbg_narrow_detail {
struct Function {
    uint32_t id;
    size_t begin, end;
    bool shuffle=false, xor_index=false, add_index=false;
};

// Prove only the translator's single-block SHFL.BFLY helper. Bitcasts here
// preserve 32-bit bits, and Function temporaries preserve the last stored fact.
// Unrelated XORs cannot qualify a different shuffle index as a butterfly.
inline bool butterfly_helper(const std::vector<uint32_t> &words,const Function &fn,
                             uint32_t lane,const std::vector<uint8_t> &scalar32) {
    enum { unknown, value, offset, lane_value, index, shuffled, selected,
           clamp, mask31, shift8, zero, minus_one, clamp_low, clamp_shift,
           segment, segment_not, lane_min, clamp_bits, lane_max, valid, predicate };
    std::vector<uint8_t> facts(words[3]);
    for(size_t at=5;at<fn.begin;at+=words[at]>>16) {
        const uint32_t *x=words.data()+at;
        if((x[0]&0xffff)==43 && (x[0]>>16)==4 && x[1]<scalar32.size() &&
           scalar32[x[1]] && x[2]<facts.size()) {
            if(x[3]==31)facts[x[2]]=mask31;
            else if(x[3]==8)facts[x[2]]=shift8;
            else if(x[3]==0)facts[x[2]]=zero;
            else if(x[3]==0xffffffffu)facts[x[2]]=minus_one;
        }
    }
    std::vector<uint32_t> parameters,locals;
    unsigned blocks=0,shuffles=0,xors=0,returns=0;
    for(size_t at=fn.begin;at<fn.end;at+=words[at]>>16) {
        const uint32_t *x=words.data()+at;
        uint32_t length=x[0]>>16,op=x[0]&0xffff;
        if(op!=54 && op!=55 && parameters.size()!=4)return false;
        auto fact=[&](uint32_t id){return id<facts.size()?facts[id]:uint8_t(unknown);};
        auto pair=[&](uint8_t a,uint8_t b){return (fact(x[3])==a && fact(x[4])==b) || (fact(x[3])==b && fact(x[4])==a);};
        switch(op) {
        case 54:case 56:break;
        case 55:
            if(length!=3 || parameters.size()==4)return false;
            parameters.push_back(x[2]);break;
        case 248:
            if(++blocks!=1 || parameters.size()!=4)return false;
            break;
        case 59:
            if(length!=4 || x[3]!=7)return false;
            locals.push_back(x[2]);break;
        case 61:
            if(length!=4 || x[2]>=facts.size() || x[1]>=scalar32.size())return false;
            facts[x[2]]=unknown;
            if(std::find(locals.begin(),locals.end(),x[3])!=locals.end())facts[x[2]]=fact(x[3]);
            else if(scalar32[x[1]]) {
                if(x[3]==parameters[0])facts[x[2]]=value;
                else if(x[3]==parameters[1])facts[x[2]]=offset;
                else if(x[3]==parameters[2])facts[x[2]]=clamp;
                else if(x[3]==lane)facts[x[2]]=lane_value;
            }
            break;
        case 62:
            if(length!=3 || x[1]>=facts.size())return false;
            if(std::find(locals.begin(),locals.end(),x[1])!=locals.end())facts[x[1]]=fact(x[2]);
            else if(x[1]!=parameters[3] || fact(x[2])!=predicate)return false;
            break;
        case 83:case 124:
            if(length!=4 || x[2]>=facts.size() || x[1]>=scalar32.size())return false;
            facts[x[2]]=scalar32[x[1]]?fact(x[3]):uint8_t(unknown);
            break;
        case 198:
            if(length!=5 || x[2]>=facts.size() || ++xors!=1)return false;
            facts[x[2]]=pair(lane_value,offset)?index:unknown;
            break;
        case 199:
            if(length!=5 || x[2]>=facts.size())return false;
            if(pair(clamp,mask31))facts[x[2]]=clamp_low;
            else if(pair(clamp_shift,mask31))facts[x[2]]=segment;
            else if(pair(lane_value,segment))facts[x[2]]=lane_min;
            else if(pair(clamp_low,segment_not))facts[x[2]]=clamp_bits;
            break;
        case 194:
            if(length!=5 || x[2]>=facts.size())return false;
            if(fact(x[3])==clamp && fact(x[4])==shift8)facts[x[2]]=clamp_shift;
            break;
        case 200:
            if(length!=4 || x[2]>=facts.size())return false;
            if(fact(x[3])==segment)facts[x[2]]=segment_not;
            break;
        case 197:
            if(length!=5 || x[2]>=facts.size())return false;
            if(pair(clamp_bits,lane_min))facts[x[2]]=lane_max;
            break;
        case 178:
            if(length!=5 || x[2]>=facts.size())return false;
            if(fact(x[3])==index && fact(x[4])==lane_max)facts[x[2]]=valid;
            break;
        case 345:
            if(length!=6 || x[2]>=facts.size() || ++shuffles!=1 ||
               fact(x[4])!=value || fact(x[5])!=index)return false;
            facts[x[2]]=shuffled;break;
        case 169:
            if(length!=6 || x[2]>=facts.size())return false;
            if(fact(x[3])==valid && fact(x[4])==shuffled && fact(x[5])==value)facts[x[2]]=selected;
            else if(fact(x[3])==valid && fact(x[4])==minus_one && fact(x[5])==zero)facts[x[2]]=predicate;
            break;
        case 254:
            if(length!=2 || fact(x[1])!=selected || ++returns!=1)return false;
            break;
        // Clamp/segment arithmetic and the predicate output are retained.
        // No calls, pointer aliases, extra blocks or external stores are accepted.
        default:return false;
        }
    }
    return blocks==1 && shuffles==1 && xors==1 && returns==1;
}

inline bool supported_helpers(const std::vector<uint32_t> &words) {
    std::vector<Function> functions;
    std::vector<uint8_t> scalar32(words[3]);
    std::vector<uint8_t> function_pointer(words[3]);
    std::vector<uint32_t> result_type(words[3]);
    uint32_t lane=0,model=99,uint32_type=0;
    size_t entry=0;
    unsigned entries=0;
    for(size_t at=5;at<words.size();at+=words[at]>>16) {
        const uint32_t *x=words.data()+at;
        uint32_t length=x[0]>>16,op=x[0]&0xffff;
        if(op==15 && length>=4){entry=at;model=x[1];++entries;}
        if(op==71 && length==4 && x[2]==11 && x[3]==41)lane=x[1];
        if((op==21 || op==22) && length>=3 && x[1]<scalar32.size() && x[2]==32)scalar32[x[1]]=1;
        if(op==21 && length==4 && x[2]==32 && x[3]==0)uint32_type=x[1];
        if(op==32 && length==4 && x[1]<function_pointer.size() && x[2]==7)function_pointer[x[1]]=1;
        // Only these established index producers are accepted. Unknown SSA
        // producers keep type zero and fail the shuffle's type check below.
        if((op==43 || op==55 || op==59 || op==61 || op==83 || op==124) && length>=3 && x[2]<result_type.size())result_type[x[2]]=x[1];
        if(op==54 && length==5)functions.push_back({x[2],at,0});
        if(!functions.empty() && !functions.back().end) {
            auto &fn=functions.back();
            if(op==345)fn.shuffle=true;
            if(op==198)fn.xor_index=true;
            if(op==128)fn.add_index=true;
            if(op==56)fn.end=at+length;
        }
    }
    if(entries!=1 || model!=5 || !lane || !uint32_type)return false;
    for(size_t at=5;at<words.size();at+=words[at]>>16)
        if((words[at]&0xffff)==345 && ((words[at]>>16)!=6 ||
           words[at+5]>=result_type.size() || result_type[words[at+5]]!=uint32_type))return false;
    // A new lane load must already belong to the entry point's interface.
    const size_t entry_end=entry+(words[entry]>>16);
    size_t interface=entry+3;
    for(;interface<entry_end;++interface) {
        uint32_t name=words[interface];
        if(!(name&0xff) || !(name&0xff00) || !(name&0xff0000) || !(name&0xff000000))break;
    }
    if(interface==entry_end || std::find(words.begin()+interface+1,words.begin()+entry_end,lane)==words.begin()+entry_end)return false;
    std::vector<uint32_t> butterflies;
    for(const auto &fn:functions) {
        if(!fn.end)return false;
        // The upstream scan recognizes literal local-variable IDs. Refuse all
        // derived Function pointers, including aliases stored after a shuffle.
        for(size_t at=fn.begin;at<fn.end;at+=words[at]>>16) {
            uint32_t length=words[at]>>16,op=words[at]&0xffff;
            if(length>=3 && words[at+1]<function_pointer.size() &&
               function_pointer[words[at+1]] && op!=55 && op!=59)return false;
            // Extended instructions can write an out-pointer (Frexp/Modf),
            // which the upstream caller scan also cannot treat as a Store.
            if(op==12)for(size_t argument=5;argument<length;++argument) {
                uint32_t id=words[at+argument];
                if(id<result_type.size() && result_type[id]<function_pointer.size() &&
                   function_pointer[result_type[id]])return false;
            }
            if(((op>=227 && op<=242) || op==318 || op==319) && length>=4) {
                uint32_t id=words[at+((op==228 || op==319)?1:3)];
                if(id<result_type.size() && result_type[id]<function_pointer.size() &&
                   function_pointer[result_type[id]])return false;
            }
        }
        if(!fn.shuffle)continue;
        if(!fn.end || fn.add_index)return false; // DOWN is deliberately unsupported.
        if(fn.xor_index) {
            if(!butterfly_helper(words,fn,lane,scalar32))return false;
            butterflies.push_back(fn.id);
        }
    }
    // The helper writes its predicate through argument four. It must not
    // overwrite the caller's reduction value through an aliased local pointer.
    std::vector<uint32_t> origin(words[3]),last(words[3]),stored_block(words[3]);
    std::vector<uint8_t> local(words[3]);
    uint32_t block=0;
    for(size_t at=5;at<words.size();at+=words[at]>>16) {
        const uint32_t *x=words.data()+at;
        uint32_t length=x[0]>>16,op=x[0]&0xffff;
        if(op==248)++block;
        else if(op==59 && length>=4 && x[2]<local.size() && x[3]==7)local[x[2]]=1;
        else if(op==61 && length>=4 && x[2]<origin.size())origin[x[2]]=x[3];
        else if(op==124 && length==4 && x[2]<origin.size() && x[3]<origin.size())origin[x[2]]=origin[x[3]];
        else if(op==62 && length>=3 && x[1]<last.size()) {last[x[1]]=x[2];stored_block[x[1]]=block;}
        else if(op==57 && length>=4 && std::find(butterflies.begin(),butterflies.end(),x[3])!=butterflies.end()) {
            if(length!=8)return false;
            for(unsigned argument=4;argument<8;++argument)
                if(x[argument]>=local.size() || !local[x[argument]])return false;
            for(unsigned argument=4;argument<7;++argument) {
                uint32_t pointer=x[argument];
                if(pointer==x[7])return false;
                if(stored_block[pointer]==block && last[pointer]<origin.size()) {
                    uint32_t source=origin[last[pointer]];
                    if(source && (source>=local.size() || !local[source] || source==x[7]))return false;
                }
            }
        }
    }
    return true;
}
} // namespace xbg_narrow_detail

// The caller supplies the device's actual subgroup size and supported stages.
// Modules without shuffles avoid scratch allocation. The input changes only
// after the upstream rewrite produces a complete result.
inline int xbg_spirv_narrow(std::vector<uint32_t> &words, uint32_t subgroup_size,
                            uint32_t supported_stages) {
    if (subgroup_size < 4 || subgroup_size >= 32 ||
        (subgroup_size & (subgroup_size - 1)) || words.size() < 5 ||
        words[0] != 0x07230203 || !words[3] || words[3] > (1u << 20)) return 0;
    for (size_t i = 5; i < words.size();) {
        size_t length = words[i] >> 16;
        if (!length || length > words.size() - i) return 0;
        // The upstream reduction window does not track writes through copies.
        if ((words[i]&0xffff)==63 || (words[i]&0xffff)==64) return 0;
        // The upstream import-name check reads its first five bytes.
        if ((words[i] & 0xffff) == 11 && length < 4) return 0;
        i += length;
    }
    if (!spv_narrow_wanted(words.data(), words.size())) return 0;
    if (!xbg_narrow_detail::supported_helpers(words)) return 0;

    const size_t sites = words.size() / 4;
    std::vector<uint32_t> scratch(9 * size_t(words[3]) + 2 * sites);
    size_t required = 0;
    int flags = 0;
    spv_narrow(words.data(), words.size(), supported_stages, subgroup_size,
               scratch.data(), sites, nullptr, 0, &required, &flags);
    if (!required) return 0;
    // Limit skipped combines to the reduction operators established by the
    // standalone contract and the real compute modules. Other uses stay intact.
    for(size_t site=0;site<sites;++site) {
        const size_t at=scratch[9*size_t(words[3])+2*site];
        if(!at)break;
        if(at>=words.size())return 0;
        const uint32_t op=words[at]&0xffff;
        if(op!=128 && op!=197 && !(op==12 && (words[at]>>16)==7 &&
                                  (words[at+4]==38 || words[at+4]==41)))return 0;
    }
    std::vector<uint32_t> output(required);
    size_t written = spv_narrow(words.data(), words.size(), supported_stages,
                                subgroup_size, scratch.data(), sites,
                                output.data(), output.size(), &required, &flags);
    if (!written || written>output.size()) return 0;
    output.resize(written);
    words.swap(output);
    return flags;
}
