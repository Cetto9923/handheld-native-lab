// Probe: for every formatless storage descriptor, list the variables that
// share its OpTypeImage, with each sharer's binding number and storage class.
// This is the evidence for the dominant refusal reason.
#include "../../src/graphics/nvn_vulkan/engine/xbg_spirv_typed_storage.h"
#include "../../src/graphics/nvn_vulkan/engine/xbg_spirv_stencil.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

static constexpr uint32_t OP_TYPE_IMAGE = 25;
static constexpr uint32_t OP_TYPE_POINTER = 32;
static constexpr uint32_t OP_VARIABLE = 59;
static constexpr uint32_t OP_DECORATE = 71;

int main(){
    auto read_u32=[&](uint32_t &v)->bool{
        uint8_t raw[4]; size_t got=0;
        while(got<4){ size_t n=fread(raw+got,1,4-got,stdin); if(!n) return false; got+=n; }
        memcpy(&v,raw,4); return true;
    };
    for(;;){
        uint32_t key_len=0, bundle_len=0;
        if(!read_u32(key_len)) break;
        std::string key(key_len,0);
        if(key_len && fread(&key[0],1,key_len,stdin)!=key_len) break;
        if(!read_u32(bundle_len)) break;
        std::vector<uint8_t> bytes(bundle_len);
        if(bundle_len && fread(bytes.data(),1,bundle_len,stdin)!=bundle_len) break;
        if(bytes.size()<32) continue;
        uint32_t h[8]; memcpy(h,bytes.data(),sizeof(h));
        if(h[0]!=0x31534758u||h[1]!=1u) continue;
        const uint32_t binding_count=h[3], input_count=h[4], c1=h[5], spirv=h[6];
        size_t offset=32+size_t(binding_count)*32+size_t(input_count)*4+c1;
        if(offset+spirv>bytes.size()) continue;
        std::vector<uint32_t> words(spirv/4);
        memcpy(words.data(),bytes.data()+offset,spirv);
        const uint32_t stage=h[2];
        xbg_spirv_ffrs_stencil(words,stage,key.c_str());

        std::unordered_map<uint32_t,uint32_t> binding_of;
        std::unordered_map<uint32_t,size_t> def;
        std::unordered_map<uint32_t,uint32_t> ptr_pointee;
        std::unordered_map<uint32_t,uint32_t> img_sampled;
        std::unordered_map<uint32_t,uint32_t> img_format;
        std::unordered_map<uint32_t,uint32_t> img_dim;
        std::unordered_map<uint32_t,uint32_t> var_type;
        std::unordered_map<uint32_t,uint32_t> var_storage;
        std::vector<uint32_t> var_order;
        for(size_t i=5;i<words.size();){
            const uint32_t n=words[i]>>16, op=words[i]&0xffffu;
            if(!n||i+n>words.size()) break;
            if(op==OP_DECORATE && n==4 && words[i+2]==33) binding_of[words[i+1]]=words[i+3];
            if(op>=19&&op<=33 && n>=2) def[words[i+1]]=i;
            if(op==OP_VARIABLE && n>=4){ var_type[words[i+2]]=words[i+1]; var_storage[words[i+2]]=words[i+3]; var_order.push_back(words[i+2]); }
            i+=n;
        }
        for(const auto &entry : def){
            const size_t at=entry.second;
            const uint32_t op=words[at]&0xffffu;
            if(op==OP_TYPE_POINTER && at+3<words.size()){
                ptr_pointee[entry.first]=words[at+3];
            } else if(op==OP_TYPE_IMAGE && at+8<words.size()){
                img_sampled[entry.first]=words[at+7]; img_format[entry.first]=words[at+8]; img_dim[entry.first]=words[at+3];
            }
        }
        printf("{\"key\":\"%s\",\"groups\":[",key.c_str());
        bool first_group=true;
        for(uint32_t var : var_order){
            const auto vt=var_type.find(var); if(vt==var_type.end()) continue;
            const auto pp=ptr_pointee.find(vt->second); if(pp==ptr_pointee.end()) continue;
            const uint32_t image=pp->second;
            const auto isamp=img_sampled.find(image); if(isamp==img_sampled.end()) continue;
            if(isamp->second!=2) continue;             // not a storage image
            if(img_format.at(image)!=0) continue;     // already typed
            std::vector<uint32_t> sharers;
            for(uint32_t other : var_order){
                const auto ot=var_type.find(other); if(ot==var_type.end()) continue;
                const auto op2=ptr_pointee.find(ot->second); if(op2==ptr_pointee.end()) continue;
                if(op2->second!=image) continue;
                sharers.push_back(other);
            }
            if(sharers.size()<2) continue;
            if(!first_group) printf(",");
            first_group=false;
            printf("{\"binding\":%u,\"image_type\":%u,\"dim\":%u,\"sharers\":[",
                   binding_of.count(var)?binding_of[var]:0u, image, img_dim.at(image));
            for(size_t i=0;i<sharers.size();++i){
                const uint32_t s=sharers[i];
                if(i) printf(",");
                printf("{\"var\":%u,\"binding\":%u,\"storage_class\":%u}",
                       s, binding_of.count(s)?binding_of[s]:0u, var_storage[s]);
            }
            printf("]}");
        }
        printf("]}\n");
        fflush(stdout);
    }
    return 0;
}
