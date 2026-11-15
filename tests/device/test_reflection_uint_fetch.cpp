#include "../../src/graphics/nvn_vulkan/engine/xbg_spirv_integer_buffer.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <map>
#include <string>

using Words=std::vector<uint32_t>;

static Words read_words(const char *path){
    std::ifstream in(path,std::ios::binary|std::ios::ate);assert(in.good());
    auto bytes=in.tellg();assert(bytes>=20 && bytes%4==0);in.seekg(0);
    Words words(size_t(bytes)/4);in.read(reinterpret_cast<char*>(words.data()),bytes);assert(in.good());
    return words;
}
static void write_words(const std::string &path,const Words &words){
    std::ofstream out(path,std::ios::binary);
    out.write(reinterpret_cast<const char*>(words.data()),words.size()*4);assert(out.good());
}
static xbg_integer_buffer_detail::Analysis parsed(const Words &words){
    xbg_integer_buffer_detail::Analysis a;assert(xbg_integer_buffer_detail::parse(words,a));return a;
}
static void rejected(const Words &original,uint32_t binding){
    auto words=original;assert(xbg_spirv_integer_buffer_rewrite(words,binding)==0 && words==original);
    auto candidates=xbg_spirv_integer_buffer_candidates(original);
    assert(std::find(candidates.begin(),candidates.end(),binding)==candidates.end());
}

static void check_binding_order(const Words &original,const Words &expected){
    auto words=original;
    std::vector<size_t> decorations;
    for(size_t i=5;i<words.size();i+=words[i]>>16)
        if((words[i]&65535u)==71 && words[i]>>16==4 && words[i+2]==33)decorations.push_back(i);
    for(size_t i=0;i<decorations.size()/2;++i){
        size_t first=decorations[i],last=decorations[decorations.size()-1-i];
        std::swap_ranges(words.begin()+first,words.begin()+first+4,words.begin()+last);
    }
    // Failed, strict and castback analyses must not leave state for the next binding.
    assert(xbg_spirv_integer_buffer_candidates(words)==expected);
}

// Every old function instruction survives. Only opaque load/image types and
// each fetch's type/result ID may change, followed by its restoring bitcast.
static void check_castback_functions(const Words &before,const Words &after,uint32_t binding){
    using namespace xbg_integer_buffer_detail;
    auto a=parsed(before);assert(analyze_castback(before,binding,a));
    auto result=parsed(after);
    size_t next=5;
    while((after[next]&65535u)!=54)next+=after[next]>>16;
    bool in_function=false;
    for(const auto &in:a.instructions){
        size_t at=in.at;
        if(in.op==54)in_function=true;
        if(!in_function)continue;
        Words expected(before.begin()+at,before.begin()+at+in.count);
        assert(next<after.size() && (after[next]>>16)==in.count);
        if(in.op==61 && a.loads.count(before[at+2]))expected[1]=after[next+1];
        if(in.op==100 && a.images.count(before[at+2]))expected[1]=after[next+1];
        bool fetch=in.op==95 && a.fetches.count(before[at+2]);
        if(fetch){
            size_t vector_at=result.definitions.at(after[next+1]);
            assert((after[vector_at]&65535u)==23 && after[vector_at+3]==4);
            size_t scalar_at=result.definitions.at(after[vector_at+2]);
            assert((after[scalar_at]&65535u)==21 && after[scalar_at+2]==32 && after[scalar_at+3]==0);
            assert(after[next+2]>=before[3]);
            expected[1]=after[next+1];expected[2]=after[next+2];
        }
        assert(std::equal(expected.begin(),expected.end(),after.begin()+next));next+=in.count;
        if(fetch){
            Words bitcast={(4u<<16)|124u,before[at+1],before[at+2],expected[2]};
            assert(next+4<=after.size() && std::equal(bitcast.begin(),bitcast.end(),after.begin()+next));next+=4;
        }
    }
    assert(next==after.size());
}

static Words mixed_consumers(const Words &original){
    Words words=original;uint32_t scalar=0;
    for(size_t i=5;i<words.size();i+=words[i]>>16){
        uint32_t op=words[i]&65535u;
        if(op==81 && !scalar)scalar=words[i+2];
        if(op==112){
            assert(scalar && (words[i]>>16)==4);
            words[i]=(5u<<16)|129u;words[i+3]=scalar;words.insert(words.begin()+i+4,scalar);return words;
        }
    }
    assert(false);return {};
}

static Words opaque_copy(const Words &original,bool pointer){
    using namespace xbg_integer_buffer_detail;
    auto a=parsed(original);assert(analyze_opaque(original,64,a));
    Words words=original;
    for(const auto &in:a.instructions){
        size_t at=in.at;
        bool selected=pointer?in.op==61 && original[at+3]==a.descriptor:
                              in.op==100 && a.images.count(original[at+2]);
        if(!selected)continue;
        uint32_t source=original[at+3],type=original[a.variable_at+1];
        if(!pointer){
            for(const auto &load:a.instructions)
                if(load.op==61 && original[load.at+2]==source)type=original[load.at+1];
        }
        uint32_t alias=words[3]++;words[at+3]=alias;
        Words copy={(4u<<16)|83u,type,alias,source};words.insert(words.begin()+at,copy.begin(),copy.end());return words;
    }
    assert(false);return {};
}

static void castback_contracts(const Words &original,const std::string &output){
    const auto mixed=mixed_consumers(original);
    auto a=parsed(mixed);assert(!xbg_integer_buffer_detail::analyze(mixed,64,a));
    assert(xbg_spirv_integer_buffer_candidates(mixed)==std::vector<uint32_t>{64});
    auto words=mixed;assert(xbg_spirv_integer_buffer_rewrite(words,64)==2);
    check_castback_functions(mixed,words,64);
    write_words(output+".mixed-original.spv",mixed);write_words(output+".mixed.spv",words);
    rejected(words,64);
    for(bool pointer:{false,true}){
        auto negative=opaque_copy(mixed,pointer);rejected(negative,64);
        write_words(output+(pointer?".pointer-copy.spv":".sampled-copy.spv"),negative);
    }
    auto component=mixed;
    for(size_t i=5;i<component.size();i+=component[i]>>16)
        if((component[i]&65535u)==81){component[i+4]=3;break;}
    rejected(component,64);write_words(output+".component-three.spv",component);

    // A second binding shares the original float image/sampled/pointer types.
    // Replace its whole-vector consumer with component-zero extraction.
    auto shared=mixed;uint32_t fetched=0,float_type=0,vector_type=0;
    for(size_t i=5;i<shared.size();i+=shared[i]>>16){
        if((shared[i]&65535u)==22)float_type=shared[i+1];
        if((shared[i]&65535u)==95){fetched=shared[i+2];vector_type=shared[i+1];}
    }
    bool inserted=false;
    for(size_t i=5;i<shared.size();i+=shared[i]>>16){
        if((shared[i]&65535u)!=129 || shared[i+1]!=vector_type || shared[i+4]!=fetched)continue;
        uint32_t scalar=shared[3]++,vector=shared[3]++;shared[i+4]=vector;
        Words extraction={(5u<<16)|81u,float_type,scalar,fetched,0,
                          (7u<<16)|80u,vector_type,vector,scalar,scalar,scalar,scalar};
        shared.insert(shared.begin()+i,extraction.begin(),extraction.end());inserted=true;break;
    }
    assert(inserted);
    write_words(output+".multi-original.spv",shared);
    assert(xbg_spirv_integer_buffer_candidates(shared)==(std::vector<uint32_t>{64,70}));
    check_binding_order(shared,{64,70});
    for(uint32_t binding:{64u,70u}){
        auto before=shared;assert(xbg_spirv_integer_buffer_rewrite(shared,binding)>0);
        check_castback_functions(before,shared,binding);
    }
    write_words(output+".multi.spv",shared);

    // Existing matching integer types may be declared after the descriptor.
    a=parsed(mixed);assert(xbg_integer_buffer_detail::analyze_opaque(mixed,64,a));
    auto later=mixed;uint32_t image=later[3]++,sampled=later[3]++,pointer=later[3]++;
    Words types(mixed.begin()+a.image_at,mixed.begin()+a.image_at+9);types[1]=image;types[2]=a.uint_type;
    types.insert(types.end(),{(3u<<16)|27u,sampled,image,(4u<<16)|32u,pointer,0,sampled});
    inserted=false;
    for(size_t i=5;i<later.size();i+=later[i]>>16){
        if((later[i]&65535u)!=54)continue;
        later.insert(later.begin()+i,types.begin(),types.end());inserted=true;break;
    }
    assert(inserted);write_words(output+".later-original.spv",later);
    auto before=later;assert(xbg_spirv_integer_buffer_rewrite(later,64)==2);
    check_castback_functions(before,later,64);write_words(output+".later.spv",later);
}

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
    if(argc>=5 && std::string(argv[1])=="--module"){
        auto words=read_words(argv[2]);
        for(int argument=4;argument<argc;++argument){
            uint32_t binding=uint32_t(std::stoul(argv[argument]));
            auto before=words;auto a=parsed(words);
            bool strict=xbg_integer_buffer_detail::analyze(words,binding,a);
            auto candidates=xbg_spirv_integer_buffer_candidates(words);
            assert(std::find(candidates.begin(),candidates.end(),binding)!=candidates.end());
            int count=xbg_spirv_integer_buffer_rewrite(words,binding);assert(count>0);
            if(!strict)check_castback_functions(before,words,binding);
            rejected(words,binding);
            std::cout<<"binding="<<binding<<" fetches="<<count<<" strict="<<strict<<"\n";
        }
        write_words(argv[3],words);return 0;
    }
    assert(argc==3);std::ifstream in(argv[1],std::ios::binary|std::ios::ate);assert(in.good());
    auto bytes=in.tellg();assert(bytes>=20 && bytes%4==0);in.seekg(0);std::vector<uint32_t> original(size_t(bytes)/4);in.read(reinterpret_cast<char*>(original.data()),bytes);assert(in.good());
    auto w=original;assert(scalar_type(w,64)==22 && scalar_type(w,70)==22);
    assert(xbg_spirv_integer_buffer_candidates(w)==std::vector<uint32_t>{64});
    check_binding_order(w,{64});
    assert(xbg_spirv_integer_buffer_rewrite(w,64)==2);
    assert(scalar_type(w,64)==21 && scalar_type(w,70)==22);
    unsigned fetches=0;for(size_t i=5;i<w.size();i+=w[i]>>16)fetches+=(w[i]&65535u)==95;
    assert(fetches==3); // two integer fetches and the shared-type floating consumer
    auto a=original;assert(xbg_spirv_integer_buffer_rewrite(a,70)==0 && a==original);
    assert(xbg_spirv_integer_buffer_rewrite(a,999)==0 && a==original);
    for(size_t i=5;i<a.size();i+=a[i]>>16)if((a[i]&65535u)==71 && a[i]>>16==4 && a[i+2]==33 && a[i+3]==64)a[i+3]=77;
    assert(xbg_spirv_integer_buffer_candidates(a)==std::vector<uint32_t>{77});
    assert(xbg_spirv_integer_buffer_rewrite(a,77)==2 && scalar_type(a,77)==21);
    a=original;a[5]=0;auto malformed=a;
    assert(xbg_spirv_integer_buffer_rewrite(a,64)<0 && a==malformed);
    castback_contracts(original,argv[2]);
    std::ofstream out(argv[2],std::ios::binary);out.write(reinterpret_cast<const char*>(w.data()),w.size()*4);assert(out.good());
    std::cout<<"Integer buffer fetch: direct and local consumers, shared floating binding, arbitrary binding and rejection controls PASS\n";
}
