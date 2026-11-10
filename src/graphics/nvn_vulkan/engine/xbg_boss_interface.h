#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

// Add an unwritten output to the exact observed vertex module. This makes its
// location-9 interface available to the fragment stage without inventing a
// value or changing any existing calculations. Return false without mutation
// if the bundle shape differs or already contains that output.
inline bool xbg_boss_add_unwritten_location9(std::vector<uint32_t> &w){
    if(w.size()!=78860 || w[0]!=0x07230203u || w[3]==0 || w[3]==UINT32_MAX)return false;
    size_t entry=0,annotation=0,function=0;bool vec=false,ptr=false,var84=false,var86=false,loc8=false,loc10=false;
    for(size_t i=5;i<w.size();){
        const uint32_t n=w[i]>>16,op=w[i]&0xffffu;if(!n || i+n>w.size())return false;
        if(op==15 && n==20 && w[i+1]==0 && w[i+2]==100 && w[i+3]==0x6e69616du && w[i+4]==0)entry=i;
        if(op==71 && n==4 && w[i+2]==30){
            if(w[i+3]==9)return false;
            if(w[i+1]==84 && w[i+3]==8){loc8=true;annotation=i;}
            if(w[i+1]==86 && w[i+3]==10)loc10=true;
        }
        if(op==23 && n==4 && w[i+1]==5 && w[i+2]==4 && w[i+3]==4)vec=true;
        if(op==32 && n==4 && w[i+1]==70 && w[i+2]==3 && w[i+3]==5)ptr=true;
        if(op==59 && n==4 && w[i+1]==70 && w[i+3]==3){if(w[i+2]==84)var84=true;if(w[i+2]==86)var86=true;}
        if(op==54 && !function)function=i;
        i+=n;
    }
    if(!entry || !annotation || !function || !vec || !ptr || !var84 || !var86 || !loc8 || !loc10)return false;
    const uint32_t id=w[3];std::vector<uint32_t> out;out.reserve(w.size()+9);out.insert(out.end(),w.begin(),w.begin()+5);out[3]=id+1;
    for(size_t i=5;i<w.size();){
        const uint32_t n=w[i]>>16;
        if(i==function)out.insert(out.end(),{(4u<<16)|59u,70,id,3});
        const size_t at=out.size();out.insert(out.end(),w.begin()+i,w.begin()+i+n);
        if(i==entry){out[at]=((n+1)<<16)|15u;out.push_back(id);}
        if(i==annotation)out.insert(out.end(),{(4u<<16)|71u,id,30,9});
        i+=n;
    }
    w.swap(out);return true;
}
