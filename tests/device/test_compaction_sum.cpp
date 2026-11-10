#include "src/graphics/nvn_vulkan/engine/xbg_compaction_sum.h"
#include <cassert>
#include <cstring>
#include <fstream>
#include <iterator>

int main(int argc,char **argv) {
    assert(argc==3);
    std::ifstream f(argv[1],std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(f)),{});
    assert(bytes.size()==130736);
    std::vector<uint32_t> original(bytes.size()/4);
    std::memcpy(original.data(),bytes.data(),bytes.size());
    auto patched=original;
    assert(xbg_ffrs_compaction_sum(patched));
    assert(patched.size()==original.size()+28 && patched[3]==7560);
    // Reconstruct the fixture by removing only the two new instruction groups.
    std::vector<uint32_t> restored(patched.begin(),patched.begin()+5);
    unsigned stores=0, sums=0;
    for(size_t i=5;i<patched.size();) {
        unsigned n=patched[i]>>16, op=patched[i]&65535;
        if(op==61 && n==4 && patched[i+2]==7554) {
            const uint32_t old[]={7u<<16|234u,1,7117,7113,7116,2804,7115};
            restored.insert(restored.end(),old,old+7);i+=21;++stores;
        } else if(op==61 && n==4 && patched[i+2]==7557) {
            const uint32_t old[]={4u<<16|61u,1,7124,7123};
            restored.insert(restored.end(),old,old+4);i+=18;++sums;
        } else {
            restored.insert(restored.end(),patched.begin()+i,patched.begin()+i+n);i+=n;
        }
    }
    restored[3]=original[3];
    assert(stores==1 && sums==1 && restored==original);
    auto again=patched;assert(!xbg_ffrs_compaction_sum(again) && again==patched);
    auto wrong=original;wrong[3]++;auto before=wrong;
    assert(!xbg_ffrs_compaction_sum(wrong) && wrong==before);
    wrong=original;
    for(size_t i=5;i<wrong.size();i+=wrong[i]>>16) {
        if((wrong[i]&65535)==234 && wrong[i+2]==7117) { wrong[i+6]++;break; }
    }
    before=wrong;assert(!xbg_ffrs_compaction_sum(wrong) && wrong==before);
    std::ofstream out(argv[2],std::ios::binary);
    out.write(reinterpret_cast<const char*>(patched.data()),patched.size()*4);
    return !out.good();
}
