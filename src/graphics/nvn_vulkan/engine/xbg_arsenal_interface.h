#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

// These observed VS/FS pairs omit one VS varying that the FS reads.
// Match only the exact original bundles. Initialize the added output as the
// pinned translator's linked-stage initialization policy: xyz=0, w=1.
// All original instructions remain unchanged. Never strip a fragment input.
// Not a generic "make invalid shaders compile" or missing-file fallback.
inline bool xbg_arsenal_complete_output(std::vector<uint32_t> &w,const char *key){
    struct Shape{const char *key;size_t words;uint64_t hash;uint32_t location;};
    static const Shape shapes[]={
        {"17c6ba1e659fec3a",65386,0x24a65713f5370720ull,7},
        {"5a97a7e1a99cce45",70003,0xc42fa989d385136full,7},
        {"3bce5482c09be3fb",52935,0x3e5b43b3160cd51dull,4},
        {"0d79843412724995",73201,0xf40106f006d0215dull,7}
    };
    const Shape *shape=nullptr;for(const auto &s:shapes)if(key&&!std::strcmp(key,s.key)){shape=&s;break;}
    if(!shape || w.size()!=shape->words || w[0]!=0x07230203u || w[3]>UINT32_MAX-4)return false;
    uint64_t hash=1469598103934665603ull;
    for(uint32_t word:w)for(unsigned b=0;b<4;++b){hash^=uint8_t(word>>(b*8));hash*=1099511628211ull;}
    if(hash!=shape->hash)return false;
    size_t entry=0,types=0,functions=0,store_at=0;uint32_t main=0,fp32=0,vec4=0,outptr=0;
    bool in_main=false,in_first_block=false;
    for(size_t i=5;i<w.size();){
        uint32_t n=w[i]>>16,op=w[i]&0xffffu;if(!n || i+n>w.size())return false;
        if(op==15 && n>=5 && w[i+1]==0){if(entry)return false;entry=i;main=w[i+2];}
        if(op>=19 && op<=39 && !types)types=i;
        if(op==22 && n==3 && w[i+2]==32)fp32=w[i+1];
        if(op==23 && n==4 && w[i+2]==fp32 && w[i+3]==4)vec4=w[i+1];
        if(op==32 && n==4 && w[i+2]==3 && w[i+3]==vec4)outptr=w[i+1];
        if(op==71 && n==4 && w[i+2]==30 && w[i+3]==shape->location)return false;
        if(op==54){if(!functions)functions=i;in_main=n==5&&w[i+2]==main;in_first_block=false;}
        if(in_main && op==248 && !store_at)in_first_block=true;
        else if(in_main && in_first_block && !store_at && op!=59 && op!=8 && op!=317)store_at=i;
        if(op==56)in_main=false;
        i+=n;
    }
    if(!entry||!types||!functions||!store_at||!fp32||!vec4||!outptr)return false;
    const uint32_t zero=w[3],one=zero+1,value=zero+2,output=zero+3;
    std::vector<uint32_t> out;out.reserve(w.size()+27);out.insert(out.end(),w.begin(),w.begin()+5);out[3]=zero+4;
    for(size_t i=5;i<w.size();){
        uint32_t n=w[i]>>16;
        if(i==types)out.insert(out.end(),{(4u<<16)|71u,output,30,shape->location});
        if(i==functions){
            out.insert(out.end(),{(4u<<16)|43u,fp32,zero,0,(4u<<16)|43u,fp32,one,0x3f800000u,
                (7u<<16)|44u,vec4,value,zero,zero,zero,one,(4u<<16)|59u,outptr,output,3});
        }
        if(i==store_at)out.insert(out.end(),{(3u<<16)|62u,output,value});
        size_t at=out.size();out.insert(out.end(),w.begin()+i,w.begin()+i+n);
        if(i==entry){out[at]=((n+1)<<16)|15u;out.push_back(output);}
        i+=n;
    }
    w.swap(out);return true;
}
