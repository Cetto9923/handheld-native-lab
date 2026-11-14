#include "../../src/graphics/nvn_vulkan/engine/xbg_shader_identity.h"
#include "../../src/graphics/nvn_vulkan/engine/xbg_spirv_integer_buffer.h"
#include <cassert>
#include <fstream>
#include <iostream>

using Formats=std::vector<std::pair<uint32_t,uint32_t>>;
static const std::string key="5bf9a939f45a8bbf";

static std::string canonical(const std::string &base,const Formats &integer,const Formats &typed){
    std::string result=base;
    auto append=[&](const char *prefix,const Formats &formats){
        if(formats.empty())return;
        result+=prefix;
        for(size_t i=0;i<formats.size();++i){
            if(i)result+=',';
            result+=std::to_string(formats[i].first)+'_'+std::to_string(formats[i].second);
        }
    };
    append(":ib1:",integer);
    append(":stb1:",typed);
    return result;
}

static void accepts(const std::string &identity,const Formats &expected_integer,const Formats &expected_typed){
    std::string base="unchanged";
    Formats integer={{999,98}},typed={{999,100}};
    assert(xbg_shader_identity(identity,base,integer,&typed));
    assert(base==identity.substr(0,16));
    assert(integer==expected_integer && typed==expected_typed);
    assert(canonical(base,integer,typed)==identity);
    base="unchanged";
    integer={{999,98}};
    assert(xbg_shader_identity(identity,base,integer));
    assert(base==identity.substr(0,16) && integer==expected_integer);
}

static void rejects(const std::string &identity){
    std::string base="unchanged";
    Formats integer={{999,98}},typed={{999,100}};
    const auto original_integer=integer,original_typed=typed;
    assert(!xbg_shader_identity(identity,base,integer,&typed));
    assert(base=="unchanged" && integer==original_integer && typed==original_typed);
    assert(!xbg_shader_identity(identity,base,integer));
    assert(base=="unchanged" && integer==original_integer);
}

static void parser_contract(){
    accepts(key,{},{});
    accepts("0000000000000000",{},{});
    accepts("ffffffffffffffff",{},{});
    accepts(key+":ib1:0_74,17_98",{{0,74},{17,98}},{});
    accepts(key+":ib1:4294967295_98",{{UINT32_MAX,98}},{});
    accepts(key+":stb1:0_100,35_107",{},{{0,100},{35,107}});
    accepts(key+":stb1:4294967295_98",{},{{UINT32_MAX,98}});
    accepts(key+":ib1:0_74,17_98:stb1:0_98,29_100",{{0,74},{17,98}},{{0,98},{29,100}});

    for(uint32_t format=0;format<=300;++format){
        const std::string typed=key+":stb1:1_"+std::to_string(format);
        if(xbg_spirv_image_format(format))accepts(typed,{},{{1,format}});
        else rejects(typed);
        const std::string integer=key+":ib1:1_"+std::to_string(format);
        if(format==74 || format==98)accepts(integer,{{1,format}},{});
        else rejects(integer);
    }

    for(const char *suffix:{
        ":ib1:",":stb1:",":ib1:1_98:stb1:",":stb1:1_100:ib1:1_98",
        ":ib1:1_98:ib1:2_98",":stb1:1_100:stb1:2_100",
        ":ib1:1_98:stb1:2_100:ib1:3_98",":ib1:1_98:stb1:2_100:stb1:3_100",
        ":ib1:2_98,1_98",":ib1:1_98,1_74",":stb1:2_100,1_100",":stb1:1_100,1_98",
        ":ib1:1_98:stb1:2_100,1_100",":ib1:1_98:stb1:1_100,1_98",
        ":ib1:01_98",":stb1:00_100",":ib1:1_098",":stb1:1_0100",
        ":ib1:-1_98",":stb1:+1_100",":ib1:1_+98",":stb1:1_-100",
        ":ib1:4294967296_98",":stb1:4294967296_100",":ib1:1_4294967296",":stb1:1_4294967296",
        ":ib1:1_4294967295",":stb1:1_4294967295",
        ":ib1:1_98,",":stb1:1_100,",":ib1:1_98,",":ib1:1_98,:stb1:1_100",
        ":ib1:1_98x",":stb1:1_100x",":ib1:1_98:",":stb1:1_100:",
        ":ib1:1_98 ",":stb1:1_100\n",":IB1:1_98",":stb2:1_100",
        ":ib1:_98",":stb1:1_",":ib1:1__98",":stb1:1_100,,2_100",
        ":ib1:1_98,:stb1:2_100",":ib1:1_98,2_100",":stb1:1_0"
    })rejects(key+suffix);
    rejects("");
    rejects(key.substr(1));
    rejects(key+"0");
    rejects("5BF9a939f45a8bbf");
    rejects("gbf9a939f45a8bbf");
    rejects(key+":ib1:"+std::string(200,'9')+"_98");
    rejects(key+":stb1:1_"+std::string(200,'9'));
    rejects(key+std::string("\0:ib1:1_98",10));
    rejects(key+":stb1:1_100"+std::string(1,'\0'));

    Formats boundary;
    for(uint32_t binding=0;binding<35;++binding)boundary.push_back({binding,100});
    const std::string maximum=canonical(key,{},boundary);
    assert(maximum.size()==256);
    accepts(maximum,{},boundary);
    boundary.back().first=100;
    const std::string excessive=canonical(key,{},boundary);
    assert(excessive.size()==257);
    rejects(excessive);

    std::string base="unchanged";
    Formats alias={{999,98}};
    assert(!xbg_shader_identity(key,base,alias,&alias));
    assert(base=="unchanged" && (alias==Formats{{999,98}}));

    // An input string may also be the base output. Parsing must finish first.
    std::string input=key+":ib1:1_98:stb1:2_100";
    Formats integer,typed;
    assert(xbg_shader_identity(input,input,integer,&typed));
    assert(input==key && (integer==Formats{{1,98}}) && (typed==Formats{{2,100}}));
}

static std::vector<uint32_t> load(const std::string &path){
    std::ifstream file(path,std::ios::binary|std::ios::ate);
    assert(file.good());
    const auto bytes=file.tellg();
    assert(bytes>=20 && bytes%4==0);
    std::vector<uint32_t> words(size_t(bytes)/4);
    file.seekg(0);
    file.read(reinterpret_cast<char *>(words.data()),bytes);
    assert(file.good());
    return words;
}

static void save(const std::string &path,const std::vector<uint32_t> &words){
    std::ofstream file(path,std::ios::binary);
    file.write(reinterpret_cast<const char *>(words.data()),std::streamsize(words.size()*4));
    assert(file.good());
}

static void specialize(std::vector<uint32_t> &words,const Formats &integer,const Formats &typed){
    for(const auto &entry:integer)assert(xbg_spirv_integer_buffer_rewrite(words,entry.first)>0);
    std::vector<XbgTypedStorageRequest> requests;
    for(const auto &entry:typed)requests.push_back({entry.first,xbg_spirv_image_format(entry.second)});
    if(requests.empty())return;
    size_t failed=0;
    int shared=1;
    std::vector<uint32_t> split;
    assert(xbg_typed_storage_declare(words,requests,failed,shared,split,true)==1);
}

static void real_recipe(const std::string &directory,const std::string &output,const std::string &identity,
                        const Formats &runtime_integer,const Formats &runtime_typed){
    std::string base;
    Formats integer,typed;
    assert(xbg_shader_identity(identity,base,integer,&typed));
    assert(integer==runtime_integer && typed==runtime_typed);
    const auto original=load(directory+'/'+base+"-original.spv");
    auto warm=original,runtime=original,legacy_typed=original;
    specialize(warm,integer,typed);
    specialize(runtime,runtime_integer,runtime_typed);
    specialize(legacy_typed,{},runtime_typed);
    assert(warm==runtime);
    assert(warm!=legacy_typed);
    save(output+'/'+base+"-combined.spv",warm);
    save(output+'/'+base+"-typed-only.spv",legacy_typed);
    std::cout<<"Real combined recipe "<<identity<<" words="<<warm.size()<<" PASS\n";
}

int main(int argc,char **argv){
    assert(argc==1 || argc==3);
    parser_contract();
    if(argc==3){
        real_recipe(argv[1],argv[2],key+":ib1:17_98:stb1:29_100",{{17,98}},{{29,100}});
        real_recipe(argv[1],argv[2],"d56a8ffe79793ace:ib1:11_98,17_98,23_98,29_98:stb1:35_100",
                    {{11,98},{17,98},{23,98},{29,98}},{{35,100}});
    }
    std::cout<<"Shader identity canonical forms, strict lists, transactional rejection and 256-byte boundary PASS\n";
}
