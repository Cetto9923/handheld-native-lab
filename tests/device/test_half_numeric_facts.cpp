// Exercise the real per-binding rewrite over compiled SPIR-V, not a table-only
// assertion. Half's original float fetch and other bindings' raw uint fetches
// must coexist, with the same three fetch operations and legal unique types.
#include "../../src/graphics/nvn_vulkan/engine/xbg_spirv_stencil.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <string>
struct Type {uint32_t op;std::vector<uint32_t> args;};
std::map<uint32_t,uint32_t> sampled_types(const std::vector<uint32_t>& w){
    std::map<uint32_t,Type> types;std::map<uint32_t,uint32_t> bindings,variables,result;
    for(size_t i=5;i<w.size();){uint32_t n=w[i]>>16,op=w[i]&65535u;assert(n && n<=w.size()-i);
        if(op==21 || op==22 || op==25 || op==27 || op==32)types[w[i+1]]={op,{w.begin()+i+2,w.begin()+i+n}};
        if(op==59)variables[w[i+2]]=w[i+1];if(op==71 && n==4 && w[i+2]==33)bindings[w[i+1]]=w[i+3];i+=n;
    }
    for(auto b:bindings){uint32_t t=variables.at(b.first);assert(types.at(t).op==32);t=types.at(t).args.at(1);assert(types.at(t).op==27);t=types.at(t).args.at(0);assert(types.at(t).op==25 && types.at(t).args.at(1)==5);t=types.at(t).args.at(0);result[b.second]=types.at(t).op;}
    return result;
}
int main(int argc,char**argv){
    assert(argc==4);const char *keys[]={"26aef111ba3b3c4e","8b27994fdc0e32c9","9528f530e1c95ce5","be4e89a0687ae37c","431a6183c3565f66","1e222af40b26bd25","b0284572b5f180f2","6018ff2b44b763d7"};
    for(unsigned k=0;k<8;k++){
        unsigned base=k<3?12:18;std::ifstream file(argv[k<3?1:2],std::ios::binary);std::vector<char> bytes((std::istreambuf_iterator<char>(file)),{});assert(bytes.size()>=20 && !(bytes.size()%4));std::vector<uint32_t> w(bytes.size()/4);memcpy(w.data(),bytes.data(),bytes.size());
        auto original=sampled_types(w);assert(original.at(base)==22 && original.at(base+6)==22 && original.at(base+12)==22);
        int changed=xbg_spirv_ffrs_stencil(w,0,keys[k]);auto after=sampled_types(w);
        if(changed!=2 || after.at(base)!=21 || after.at(base+6)!=22 || after.at(base+12)!=21){std::cerr<<"Half numeric binding incorrectly rewritten: "<<keys[k]<<" changed="<<changed<<"\n";return 4;}
        unsigned fetches=0;for(size_t i=5;i<w.size();i+=w[i]>>16)fetches+=(w[i]&65535u)==95;assert(fetches==3);
        assert(xbg_ffrs_integer_fact(0,keys[k],base)==1 && xbg_ffrs_integer_fact(0,keys[k],base+6)==0 && xbg_ffrs_integer_fact(0,keys[k],base+12)==1);
        assert(xbg_ffrs_integer_fact(4,keys[k],base)==0);
        std::ofstream out(std::string(argv[3])+"/"+keys[k]+".spv",std::ios::binary);out.write(reinterpret_cast<const char*>(w.data()),w.size()*4);assert(out.good());
    }
    assert(xbg_ffrs_integer_fact(0,"fe033d948255d9ff",24)==1); // unobserved binding untouched
    assert(xbg_ffrs_integer_fact(0,"unknown",18)==0);
    std::cout<<"Eight half-float fetches preserved, raw integer bindings/stages unchanged: PASS\n";
}
