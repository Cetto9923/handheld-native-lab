#include "src/graphics/nvn_vulkan/engine/xbg_boss_interface.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <vector>

// The game-derived input and output are local artifacts, not repository data.
int main(int argc,char **argv){
    assert(argc==3);std::ifstream in(argv[1],std::ios::binary);assert(in);
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)),{});assert(bytes.size()%4==0);
    std::vector<uint32_t> original(bytes.size()/4);
    for(size_t i=0;i<bytes.size();++i)original[i/4]|=uint32_t(uint8_t(bytes[i]))<<(8*(i%4));
    auto bad=original;bad[0]=0;auto saved=bad;assert(!xbg_boss_add_unwritten_location9(bad) && bad==saved);
    auto patched=original;assert(xbg_boss_add_unwritten_location9(patched));assert(patched.size()==original.size()+9);
    saved=patched;assert(!xbg_boss_add_unwritten_location9(patched) && patched==saved);
    std::ofstream out(argv[2],std::ios::binary);out.write(reinterpret_cast<const char*>(patched.data()),std::streamsize(patched.size()*4));assert(out);
}
