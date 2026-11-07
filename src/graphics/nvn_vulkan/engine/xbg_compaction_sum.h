#pragma once
#include <cstdint>
#include <vector>

// The captured FFRS compaction kernel has two Maxwell warps in one 64-lane
// Vulkan subgroup. On Adreno 735, its shared atomic reduction returned 14 for
// warp sums 7 and 0 (and produced 132 instances from 100 live particles).
// Give each warp its own existing shared word, retain the execution barrier,
// and add the two words at the original final read. No particle is capped or
// discarded. This runs after warp32 rewriting; reject unfamiliar modules.
inline bool xbg_ffrs_compaction_sum(std::vector<uint32_t> &w) {
    if (w.size()!=32684 || w[0]!=0x07230203u || w[3]!=7554) return false;
    size_t atomic=size_t(-1), load=size_t(-1);
    bool local=false, lane=false, array=false, array_size=false;
    bool variable=false, pointer=false, shift=false, one=false, zero=false;
    unsigned barriers=0;
    for (size_t i=5;i<w.size();) {
        const uint32_t n=w[i]>>16, op=w[i]&65535u;
        if (!n || i+n>w.size()) return false;
        if (op==16 && n==6 && w[i+1]==61 && w[i+2]==17 &&
            w[i+3]==64 && w[i+4]==1 && w[i+5]==1) local=true;
        if (op==71 && n==4 && w[i+1]==58 && w[i+2]==11 && w[i+3]==41) lane=true;
        if (op==28 && n==4 && w[i+1]==25 && w[i+2]==1 && w[i+3]==24) array=true;
        if (op==43 && n==4 && w[i+1]==1) {
            if (w[i+2]==24 && w[i+3]==4) array_size=true;
            if (w[i+2]==7530 && w[i+3]==5) shift=true;
            if (w[i+2]==7116 && w[i+3]==1) one=true;
            if (w[i+2]==2804 && w[i+3]==0) zero=true;
        }
        if (op==59 && n==4 && w[i+1]==26 && w[i+2]==28 && w[i+3]==4) variable=true;
        if (op==32 && n==4 && w[i+1]==2754 && w[i+2]==4 && w[i+3]==1) pointer=true;
        if (op==224) ++barriers;
        // The old shared atomic return is stored in this dead local only.
        if (op==61 && n>=4 && w[i+3]==2719) return false;
        if (op==234 && n==7 && w[i+1]==1 && w[i+2]==7117 &&
            w[i+3]==7113 && w[i+4]==7116 && w[i+5]==2804 && w[i+6]==7115) {
            if (atomic!=size_t(-1)) return false;
            atomic=i;
        }
        if (op==61 && n==4 && w[i+1]==1 && w[i+2]==7124 && w[i+3]==7123) {
            if (load!=size_t(-1)) return false;
            load=i;
        }
        i+=n;
    }
    if (!local || !lane || !array || !array_size || !variable || !pointer ||
        !shift || !one || !zero || barriers!=2 || atomic==size_t(-1) ||
        load==size_t(-1) || load<=atomic) return false;
    const uint32_t leader_store[]={
        4u<<16|61u,1,7554,58,
        5u<<16|194u,1,7555,7554,7530,
        5u<<16|65u,2754,7556,28,7555,
        3u<<16|62u,7556,7115,
        4u<<16|83u,1,7117,2804
    };
    const uint32_t sum_load[]={
        4u<<16|61u,1,7557,7123,
        5u<<16|65u,2754,7558,28,7116,
        4u<<16|61u,1,7559,7558,
        5u<<16|128u,1,7124,7557,7559
    };
    std::vector<uint32_t> out;
    out.reserve(w.size()+28);
    out.insert(out.end(),w.begin(),w.begin()+atomic);
    out.insert(out.end(),leader_store,leader_store+21);
    out.insert(out.end(),w.begin()+atomic+7,w.begin()+load);
    out.insert(out.end(),sum_load,sum_load+18);
    out.insert(out.end(),w.begin()+load+4,w.end());
    out[3]=7560;
    w.swap(out);
    return true;
}
