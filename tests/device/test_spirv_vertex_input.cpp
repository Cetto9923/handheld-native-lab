#include "xbg_spirv_vertex_input.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

using Requests=std::vector<std::pair<uint32_t,uint32_t>>;
static std::vector<uint32_t> load(const std::string &path) {
    std::ifstream f(path,std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(f)),{});
    assert(!bytes.empty() && bytes.size()%4==0);
    std::vector<uint32_t> words(bytes.size()/4);std::memcpy(words.data(),bytes.data(),bytes.size());return words;
}
static void save(const std::string &path,const std::vector<uint32_t> &words) {
    std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<const char*>(words.data()),words.size()*4);assert(f.good());
}
static size_t find(const std::vector<uint32_t> &w,uint32_t op) {
    for(size_t at=5;at<w.size();at+=w[at]>>16)if((w[at]&65535)==op)return at;
    assert(false);return 0;
}
static uint32_t variable(const std::vector<uint32_t> &w,uint32_t location) {
    for(size_t at=5;at<w.size();at+=w[at]>>16)
        if((w[at]&65535)==71 && w[at+2]==30 && w[at+3]==location) {
            for(size_t decl=5;decl<w.size();decl+=w[decl]>>16)
                if((w[decl]&65535)==59 && w[decl+2]==w[at+1] && w[decl+3]==1)return w[at+1];
        }
    return 0;
}
static uint32_t input_class(const std::vector<uint32_t> &w,uint32_t location) {
    uint32_t id=variable(w,location),type=0;
    std::unordered_map<uint32_t,size_t> types;
    for(size_t at=5;at<w.size();at+=w[at]>>16) {
        uint32_t op=w[at]&65535;
        if(op==21 || op==22 || op==23 || op==32)types[w[at+1]]=at;
        if(op==59 && w[at+2]==id)type=w[at+1];
    }
    assert(type);size_t at=types.at(type);assert((w[at]&65535)==32 && w[at+2]==1);
    at=types.at(w[at+3]);if((w[at]&65535)==23)at=types.at(w[at+2]);
    assert(w[at+2]==32);return (w[at]&65535)==22?0:w[at+3]?1:2;
}
static std::vector<uint32_t> body(const std::vector<uint32_t> &w) {
    const size_t at=find(w,54);return {w.begin()+at,w.end()};
}
static size_t verify_body(const std::vector<uint32_t> &old,const std::vector<uint32_t> &now) {
    auto original=body(old);std::vector<uint32_t> restored;
    size_t loads=0;
    for(size_t at=find(now,54);at<now.size();) {
        const uint32_t length=now[at]>>16,op=now[at]&65535;
        if(op==61 && now[at+2]>=old[3]) {
            const size_t cast=at+length;
            assert(length==4 && now[cast]==(4u<<16|124u) && now[cast+2]<old[3] && now[cast+3]==now[at+2]);
            restored.insert(restored.end(),{4u<<16|61u,now[cast+1],now[cast+2],now[at+3]});
            at=cast+4;++loads;continue;
        }
        size_t offset=restored.size();restored.insert(restored.end(),now.begin()+at,now.begin()+at+length);
        if(op==65 || op==66) {
            assert(offset<original.size() && (original[offset]&65535)==op);
            restored[offset+1]=original[offset+1];
        }
        at+=length;
    }
    assert(restored==original);return loads;
}
static void rejected(std::vector<uint32_t> w,const Requests &r) {
    const auto before=w;assert(xbg_spirv_vertex_input(w,r)==-1 && w==before);
}
static void run(const std::vector<uint32_t> &original,const Requests &requests,
                const std::string &prefix,size_t expected_loads) {
    auto w=original;assert(xbg_spirv_vertex_input(w,requests)==1);
    const size_t count=verify_body(original,w);assert(count==expected_loads);
    for(const auto &r:requests)if(variable(w,r.first))assert(input_class(w,r.first)==r.second);
    save(prefix+"-rewritten.spv",w);const auto once=w;
    assert(xbg_spirv_vertex_input(w,requests)==0 && w==once);save(prefix+"-idempotent.spv",w);
    std::printf("vertex-input PASS loads=%zu bound=%u->%u body_restored_exact=1 idempotent=1 file=%s\n",count,original[3],w[3],prefix.c_str());
}
int main(int argc,char **argv) {
    assert(argc==3 || argc==4);const std::string directory=argv[2];const auto original=load(argv[1]);
    const Requests both{{0,2},{1,1},{2,0},{3,2}};
    run(original,{{0,2}},directory+"/partial",2);
    run(original,both,directory+"/mixed",4);
    run(original,{{0,1},{1,2},{3,1}},directory+"/signed-unsigned",4);
    auto w=original;assert(xbg_spirv_vertex_input(w,both)==1);
    save(directory+"/integer-source.spv",w);
    run(w,{{0,0},{1,0},{3,0}},directory+"/integer-to-float",4);
    w=original;assert(xbg_spirv_vertex_input(w,{{0,0},{1,0}})==0 && w==original);
    assert(xbg_spirv_vertex_input(w,{})==0 && w==original);
    assert(xbg_spirv_vertex_input(w,{{99,2}})==0 && w==original);
    run(original,{{0,2},{0,2}},directory+"/duplicate-request",2);
    rejected(original,{{0,1},{0,2}});rejected(original,{{0,3}});

    // The graph checks above require a typed integer load followed by Bitcast,
    // which preserves these integer fetch bits through the old float register.
    for(uint32_t bits:{0u,1u,127u,255u,0xffffff80u,0xffffffffu,0x80000000u}) {
        float reg;std::memcpy(&reg,&bits,4);uint32_t recovered;std::memcpy(&recovered,&reg,4);assert(recovered==bits);
        int32_t signed_value;std::memcpy(&signed_value,&recovered,4);
        if(bits==0xffffff80u)assert(signed_value==-128);
        if(bits==0xffffffffu)assert(signed_value==-1);
    }
    w=original;uint32_t local=0;
    for(size_t at=find(w,54);at<w.size();at+=w[at]>>16)
        if((w[at]&65535)==59 && w[at+3]==7){local=w[at+2];break;}
    assert(local);const uint32_t input1=variable(w,1);
    const uint32_t copy[]={3u<<16|63u,local,input1};
    w.insert(w.begin()+find(w,61),std::begin(copy),std::end(copy));
    save(directory+"/copy-escape-source.spv",w);rejected(w,both);save(directory+"/copy-escape-unchanged.spv",w);

    w=original;const uint32_t packed=variable(w,0);
    const uint32_t component[]={4u<<16|71u,packed,31,0};
    w.insert(w.begin()+find(w,19),std::begin(component),std::end(component));
    save(directory+"/component-source.spv",w);rejected(w,both);
    w=original;const uint32_t group=w[3]++;
    for(size_t at=5;at<w.size();at+=w[at]>>16)if((w[at]&65535)==71 && w[at+1]==packed && w[at+2]==30){w.erase(w.begin()+at,w.begin()+at+4);break;}
    const uint32_t grouped[]={2u<<16|73u,group,4u<<16|71u,group,30,0,3u<<16|74u,group,packed};
    w.insert(w.begin()+find(w,19),std::begin(grouped),std::end(grouped));
    save(directory+"/group-source.spv",w);rejected(w,both);

    w=original;w.resize(4);rejected(w,both);
    w=original;w[3]=0;rejected(w,both);
    w=original;w[3]=(1u<<20)+1;rejected(w,both);
    w=original;w[3]=1u<<20;rejected(w,both);
    w=original;w[5]=0;rejected(w,both);
    w=original;w[5]=0xffff0011;rejected(w,both);
    w=original;w.pop_back();rejected(w,both);
    w=original;w[find(w,61)+2]=w[3];rejected(w,both);
    w=original;w[find(w,65)+3]=w[3];rejected(w,both);
    w=original;w[find(w,65)]=4u<<16|65u;rejected(w,both);
    w=original;w[find(w,15)+1]=5;rejected(w,both);
    if(argc==4) {
        const char *keys[]={"11320c7ea662abd3","a4554215780cb394","00758c2888655e0b"};
        for(unsigned i=0;i<3;++i) {
            auto real=load(std::string(argv[3])+"/"+keys[i]+".spv");
            run(real,{{0,2},{1,2}},directory+"/real-"+keys[i],i==2?4:8);
            run(real,{{0,2}},directory+"/real-partial-"+keys[i],4);
        }
    }
    std::puts("vertex-input contracts PASS numeric_classes=0float_1sint_2uint scalar_vector=1 mixed_float=1 pointer_escape_transactional=1 malformed_transactional=1 bit_patterns=1");
}
