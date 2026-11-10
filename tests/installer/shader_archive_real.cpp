#include "../../src/graphics/nvn_vulkan/engine/xbg_shader_archive.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>
int main(int argc,char **argv) {
    if(argc!=4) return 2;
    XbgShaderArchive archive; if(!archive.open(argv[1])) return 3;
    std::ifstream list(argv[2]); if(!list) return 4;
    std::string line; size_t count=0;
    while(std::getline(list,line)) {
        auto tab=line.find('\t'); if(tab==std::string::npos) return 5;
        auto path=line.substr(0,tab); if(path.size()!=28 || path.substr(0,8)!="shaders/" || path.substr(24)!=".xgs") return 6;
        std::string key=path.substr(8,16); std::ifstream src(std::string(argv[3])+"/"+path,std::ios::binary);
        if(!src) return 7;
        std::vector<uint8_t> expected((std::istreambuf_iterator<char>(src)),std::istreambuf_iterator<char>()),actual;
        if(archive.read(key,actual)!=XbgShaderArchive::Result::Found || actual!=expected) {std::cerr<<"mismatch "<<key<<'\n';return 8;}
        ++count;
    }
    if(count!=73495) return 9;
    std::cout<<"real archive bytes matched "<<count<<" shaders; host only\n";
}
