#pragma once

#include <vector>
#include "third_party/droidplay/spv_precise.h"

// One known vertex-producing entry point. Decoration groups are retained
// unchanged because the upstream scan only recognizes direct decorations.
// Return the added decorations, or zero with the original module unchanged.
inline size_t xbg_spirv_precise(std::vector<uint32_t> &words) {
    if(words.size()<5 || words[0]!=0x07230203 || !words[3] ||
       words[3]>(1u<<20) || words[4] || words[1]<0x00010000 ||
       words[1]>0x00010600 || (words[1]&0xff))return 0;
    const uint32_t bound=words[3];
    uint32_t model=UINT32_MAX;
    unsigned entries=0;
    size_t candidates=0;
    bool in_function=false;
    for(size_t at=5;at<words.size();) {
        const uint32_t *x=words.data()+at;
        const uint32_t length=x[0]>>16,op=x[0]&0xffff;
        if(!length || length>words.size()-at)return 0;
        if(op==73 || op==74 || op==75)return 0;
        if(op==15) {
            if(length<4 || ++entries!=1 || !x[2] || x[2]>=bound)return 0;
            model=x[1];
            size_t name=3;
            while(name<length && (x[name]&0xff) && (x[name]&0xff00) &&
                  (x[name]&0xff0000) && (x[name]&0xff000000))++name;
            if(name==length)return 0;
            for(size_t i=name+1;i<length;++i)if(!x[i] || x[i]>=bound)return 0;
        }
        if(op==71) {
            if(length<3 || !x[1] || x[1]>=bound)return 0;
            if(x[2]==42 && length!=3)return 0;
        }
        if(op==54) {
            if(length!=5 || in_function || !x[1] || x[1]>=bound ||
               !x[2] || x[2]>=bound || !x[4] || x[4]>=bound)return 0;
            in_function=true;
        }
        if(op==56) {
            if(length!=1 || !in_function)return 0;
            in_function=false;
        }
        if(spv_precise_op(op)) {
            if(!in_function || length!=(op==127?4u:5u) || ++candidates>=bound)return 0;
            for(size_t i=1;i<length;++i)if(!x[i] || x[i]>=bound)return 0;
        }
        at+=length;
    }
    if(in_function || entries!=1 || !spv_precise_model(model) || !candidates)return 0;

    std::vector<uint32_t> seen((bound+31)/32);
    size_t required=0;
    int marked=0;
    spv_precise(words.data(),words.size(),seen.data(),nullptr,0,&required,&marked);
    if(!required || marked<=0 || size_t(marked)>candidates ||
       required!=words.size()+3*size_t(marked))return 0;
    std::vector<uint32_t> output(required);
    const size_t written=spv_precise(words.data(),words.size(),seen.data(),output.data(),
                                   output.size(),&required,&marked);
    if(written!=output.size() || required!=output.size())return 0;
    words.swap(output);
    return size_t(marked);
}
