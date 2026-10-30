#include "../../src/graphics/nvn_vulkan/engine/xbg_shader_archive.h"
#include <atomic>
#include <cstdio>
#include <thread>

int main(int argc,char **argv) {
    if (argc!=3) return 2;
    XbgShaderArchive archive;
    bool ok=archive.open(argv[1]);
    if (std::string(argv[2])=="reject") return ok ? 1 : 0;
    if (!ok) return 3;
    std::vector<uint8_t> b;
    if (archive.read("0000000000000001",b)!=XbgShaderArchive::Result::Found || b.size()!=40 || b[0]!=0x11 || b.back()!=0x11) return 4;
    b.assign(1,0x5a);
    if (archive.read("0000000000000002",b)!=XbgShaderArchive::Result::Missing || b.size()!=1 || b[0]!=0x5a) return 5;
    if (archive.read("invalid",b)!=XbgShaderArchive::Result::Invalid) return 6;
    std::atomic<bool> failed{false}; std::vector<std::thread> threads;
    for(int i=0;i<8;i++) threads.emplace_back([&]{for(int j=0;j<300;j++){std::vector<uint8_t> x; if(archive.read("ffffffffffffffff",x)!=XbgShaderArchive::Result::Found || x.size()!=64 || x.front()!=0x22 || x.back()!=0x22) failed=true;}});
    for(auto &t:threads)t.join();
    if(failed) return 7;
    if (std::string(argv[2])=="truncate") {
        if (::truncate(argv[1],80)!=0) return 8;
        if (archive.read("ffffffffffffffff",b)!=XbgShaderArchive::Result::Invalid) return 9;
    }
    return 0;
}
