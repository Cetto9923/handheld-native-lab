#include "../../src/graphics/nvn_vulkan/engine/xbg_shader_identity.h"
#include "../../src/graphics/nvn_vulkan/engine/xbg_spirv_integer_buffer.h"
#include <cassert>
#include <fstream>
#include <iostream>

using Formats=std::vector<std::pair<uint32_t,uint32_t>>;
static const std::string key="5bf9a939f45a8bbf";

static std::string canonical(const std::string &base,const Formats &integer,const Formats &typed,const Formats &vertex={}){
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
    append(":vin1:",vertex);
    return result;
}

static void accepts(const std::string &identity,const Formats &expected_integer,const Formats &expected_typed,
                    const Formats &expected_vertex={}){
    std::string base="unchanged";
    Formats integer={{999,98}},typed={{999,100}},vertex={{999,2}};
    assert(xbg_shader_identity(identity,base,integer,&typed,&vertex));
    assert(base==identity.substr(0,16));
    assert(integer==expected_integer && typed==expected_typed && vertex==expected_vertex);
    assert(canonical(base,integer,typed,vertex)==identity);

    // Older callers must explicitly request the new recipe before consuming it.
    base="unchanged";
    integer={{999,98}};
    typed={{999,100}};
    if(expected_vertex.empty()){
        assert(xbg_shader_identity(identity,base,integer,&typed));
        assert(base==identity.substr(0,16) && integer==expected_integer && typed==expected_typed);
    }else{
        assert(!xbg_shader_identity(identity,base,integer,&typed));
        assert(base=="unchanged" && (integer==Formats{{999,98}}) && (typed==Formats{{999,100}}));
    }
    base="unchanged";
    integer={{999,98}};
    if(expected_vertex.empty()){
        assert(xbg_shader_identity(identity,base,integer));
        assert(base==identity.substr(0,16) && integer==expected_integer);
    }else{
        assert(!xbg_shader_identity(identity,base,integer));
        assert(base=="unchanged" && (integer==Formats{{999,98}}));
    }
    base="unchanged";
    integer={{999,98}};
    vertex={{999,2}};
    assert(xbg_shader_identity(identity,base,integer,nullptr,&vertex));
    assert(base==identity.substr(0,16) && integer==expected_integer && vertex==expected_vertex);
}

static void rejects(const std::string &identity){
    std::string base="unchanged";
    Formats integer={{999,98}},typed={{999,100}},vertex={{999,2}};
    const auto original_integer=integer,original_typed=typed,original_vertex=vertex;
    assert(!xbg_shader_identity(identity,base,integer,&typed,&vertex));
    assert(base=="unchanged" && integer==original_integer && typed==original_typed && vertex==original_vertex);
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

    const Formats integer_recipe={{6,98}},typed_recipe={{41,98}},vertex_recipe={{0,2},{1,2}};
    for(unsigned mask=0;mask<8;++mask){
        const Formats integer=mask&1?integer_recipe:Formats{};
        const Formats typed=mask&2?typed_recipe:Formats{};
        const Formats vertex=mask&4?vertex_recipe:Formats{};
        accepts(canonical(key,integer,typed,vertex),integer,typed,vertex);
    }
    accepts(key+":vin1:0_0,7_1,15_2",{},{},{{0,0},{7,1},{15,2}});
    Formats all_locations;
    for(uint32_t location=0;location<16;++location)all_locations.push_back({location,location%3});
    accepts(canonical(key,{}, {},all_locations),{},{},all_locations);

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
        ":ib1:1_98,:stb1:2_100",":ib1:1_98,2_100",":stb1:1_0",
        ":vin1:",":vin1:0_1:ib1:1_98",":vin1:0_1:stb1:1_100",
        ":stb1:1_100:vin1:0_1:ib1:1_98",":ib1:1_98:vin1:0_1:stb1:1_100",
        ":vin1:0_1:vin1:1_1",":ib1:1_98:stb1:2_100:vin1:0_1:vin1:1_1",
        ":vin1:1_1,0_1",":vin1:1_1,1_2",":vin1:16_0",":vin1:4294967295_0",
        ":vin1:0_3",":vin1:0_4294967295",":vin1:00_1",":vin1:01_1",":vin1:0_01",
        ":vin1:4294967296_1",":vin1:0_4294967296",":vin1:-1_0",":vin1:+1_0",
        ":vin1:0_-1",":vin1:0_+1",":vin1:_1",":vin1:0_",":vin1:0__1",
        ":vin1:0_1,",":vin1:0_1,,1_1",":vin1:0_1x",":vin1:0_1:",":vin1:0_1 ",
        ":vin1:0_1\n",":VIN1:0_1",":vin2:0_1",":vin1:0_1,:stb1:1_100",
        ":ib1:1_98:stb1:2_100:vin1:0_1,2_3"
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
    rejects(key+":vin1:0_1"+std::string(1,'\0'));
    rejects(key+":vin1:"+std::string(200,'9')+"_1");
    rejects(key+":vin1:0_"+std::string(200,'9'));
    all_locations.push_back({16,0});
    rejects(canonical(key,{}, {},all_locations));

    Formats boundary;
    for(uint32_t binding=0;binding<35;++binding)boundary.push_back({binding,100});
    const std::string maximum=canonical(key,{},boundary);
    assert(maximum.size()==256);
    accepts(maximum,{},boundary);
    boundary.back().first=100;
    const std::string excessive=canonical(key,{},boundary);
    assert(excessive.size()==257);
    rejects(excessive);

    boundary.resize(33);
    boundary.back().first=3200000;
    const std::string maximum_vertex=canonical(key,{},boundary,{{0,1}});
    assert(maximum_vertex.size()==256);
    accepts(maximum_vertex,{},boundary,{{0,1}});
    boundary.back().first=32000000;
    const std::string excessive_vertex=canonical(key,{},boundary,{{0,1}});
    assert(excessive_vertex.size()==257);
    rejects(excessive_vertex);

    std::string base="unchanged";
    Formats alias={{999,98}};
    assert(!xbg_shader_identity(key,base,alias,&alias));
    assert(base=="unchanged" && (alias==Formats{{999,98}}));
    Formats distinct={{888,100}};
    for(const auto &identity:{key,key+":ib1:6_98:stb1:41_98:vin1:0_2,1_2"}){
        assert(!xbg_shader_identity(identity,base,alias,&alias,&distinct));
        assert(!xbg_shader_identity(identity,base,alias,&distinct,&alias));
        assert(!xbg_shader_identity(identity,base,alias,nullptr,&alias));
        assert(!xbg_shader_identity(identity,base,alias,&distinct,&distinct));
        assert(!xbg_shader_identity(identity,base,alias,&alias,&alias));
        assert(base=="unchanged" && (alias==Formats{{999,98}}) && (distinct==Formats{{888,100}}));
    }

    // An input string may also be the base output. Parsing must finish first.
    std::string input=key+":ib1:1_98:stb1:2_100";
    Formats integer,typed;
    assert(xbg_shader_identity(input,input,integer,&typed));
    assert(input==key && (integer==Formats{{1,98}}) && (typed==Formats{{2,100}}));
    input=key+":ib1:1_98:stb1:2_100:vin1:0_1,15_2";
    Formats vertex;
    assert(xbg_shader_identity(input,input,integer,&typed,&vertex));
    assert(input==key && (integer==Formats{{1,98}}) && (typed==Formats{{2,100}}) && (vertex==Formats{{0,1},{15,2}}));
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
    std::cout<<"Shader identity integer/typed/vertex canonical forms, strict lists, transactional rejection and 256-byte boundary PASS\n";
}
