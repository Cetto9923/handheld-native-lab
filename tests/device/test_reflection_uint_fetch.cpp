#include "../../src/graphics/nvn_vulkan/engine/xbg_spirv_stencil.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <map>

static uint32_t scalar_type(const std::vector<uint32_t>& w,uint32_t binding){
    std::map<uint32_t,size_t> definitions;uint32_t variable=0;
    for(size_t i=5;i<w.size();i+=w[i]>>16){uint32_t n=w[i]>>16,op=w[i]&65535u;assert(n && n<=w.size()-i);
        if(op>=19 && op<=33)definitions[w[i+1]]=i;
        if(op==59)definitions[w[i+2]]=i;
        if(op==71 && n==4 && w[i+2]==33 && w[i+3]==binding)variable=w[i+1];
    }
    size_t v=definitions.at(variable),p=definitions.at(w[v+1]),s=definitions.at(w[p+3]);
    size_t image=definitions.at(w[s+2]);return w[definitions.at(w[image+2])]&65535u;
}
int main(int argc,char**argv){
    assert(argc==3);std::ifstream in(argv[1],std::ios::binary|std::ios::ate);assert(in.good());
    auto bytes=in.tellg();assert(bytes>=20 && bytes%4==0);in.seekg(0);std::vector<uint32_t> original(size_t(bytes)/4);in.read(reinterpret_cast<char*>(original.data()),bytes);assert(in.good());
    const char *key="97ae64dbc5e7ff26";
    auto w=original;assert(scalar_type(w,64)==22 && scalar_type(w,70)==22);
    assert(xbg_spirv_ffrs_stencil(w,4,key)==2);
    assert(scalar_type(w,64)==21 && scalar_type(w,70)==22);
    unsigned fetches=0,casts=0;for(size_t i=5;i<w.size();i+=w[i]>>16){fetches+=(w[i]&65535u)==95;casts+=(w[i]&65535u)==124;}
    assert(fetches==2 && casts==2); // retain every fetch and its original float-bit consumers
    assert(xbg_ffrs_integer_fact(4,key,64)==0); // leave native R16_UINT format/range intact
    for(unsigned stage:{0u,5u}){auto a=original;assert(xbg_spirv_ffrs_stencil(a,stage,key)==0 && a==original);}
    auto a=original;assert(xbg_spirv_ffrs_stencil(a,4,"unrelated")==0 && a==original);
    a=original;for(size_t i=5;i<a.size();i+=a[i]>>16)if((a[i]&65535u)==71 && a[i]>>16==4 && a[i+2]==33 && a[i+3]==64)a[i+3]=65;
    assert(xbg_spirv_ffrs_stencil(a,4,key)<0); // refuse a matching key with an unfamiliar binding
    std::ofstream out(argv[2],std::ios::binary);out.write(reinterpret_cast<const char*>(w.data()),w.size()*4);assert(out.good());
    std::cout<<"Reflection uint fetch: two reads preserved, shared float binding untouched, native view preserved, negative controls PASS\n";
}
